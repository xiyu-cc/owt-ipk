#include "service/command_retry_scheduler.h"

#include "control/control_protocol.h"
#include "log.h"
#include "service/command_store.h"
#include "service/control_hub.h"
#include "service/observability.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace service {

namespace {

std::mutex g_scheduler_mutex;
std::condition_variable g_scheduler_cv;
std::thread g_scheduler_thread;
std::atomic<bool> g_scheduler_running{false};

int64_t compute_retry_delay_ms(int retry_count) {
  const int bounded = std::max(0, std::min(retry_count - 1, 5));
  const int64_t delay = 1000LL * (1LL << bounded);
  return std::min<int64_t>(delay, 30000LL);
}

bool build_command_from_record(const command_record& row, control::command& out) {
  control::command_type parsed_type;
  if (!control::try_parse_command_type(row.command_type, parsed_type)) {
    return false;
  }

  out.command_id = row.command_id;
  out.idempotency_key = row.idempotency_key;
  out.type = parsed_type;
  out.issued_at_ms = row.issued_at_ms;
  out.expires_at_ms = row.expires_at_ms;
  out.timeout_ms = row.timeout_ms;
  out.max_retry = row.max_retry;
  out.payload_json = row.payload_json;
  return true;
}

void append_retry_event(
    const std::string& command_id,
    const std::string& event_type,
    const std::string& status,
    const nlohmann::json& detail,
    int64_t created_at_ms) {
  std::string error;
  if (!append_command_event(
          command_id,
          event_type,
          status,
          detail.dump(),
          created_at_ms,
          error)) {
    log::warn("persist retry event failed: command_id={}, event={}, err={}", command_id, event_type, error);
  }
}

void mark_terminal(
    const std::string& command_id,
    const std::string& status,
    const nlohmann::json& result,
    const std::string& event_type,
    const nlohmann::json& detail,
    int64_t now_ms) {
  std::string error;
  bool applied = false;
  if (!update_command_terminal_status_once(
          command_id,
          status,
          result.dump(),
          now_ms,
          applied,
          error)) {
    log::warn("update retry terminal status failed: command_id={}, status={}, err={}", command_id, status, error);
    return;
  }

  if (!applied) {
    return;
  }

  append_retry_event(command_id, event_type, status, detail, now_ms);
  record_command_terminal_status(command_id, status, detail.dump());
}

void process_retry_command(const command_record& row, int64_t now_ms) {
  if (row.expires_at_ms > 0 && now_ms >= row.expires_at_ms) {
    const nlohmann::json result = {
        {"error_code", "COMMAND_EXPIRED"},
        {"message", "command expired before retry dispatch"},
        {"expires_at_ms", row.expires_at_ms},
        {"checked_at_ms", now_ms},
    };
    const nlohmann::json detail = {
        {"event", "COMMAND_RETRY_EXPIRED"},
        {"retry_count", row.retry_count},
        {"max_retry", row.max_retry},
    };
    mark_terminal(row.command_id, "TIMED_OUT", result, "COMMAND_RETRY_EXPIRED", detail, now_ms);
    return;
  }

  control::command command;
  if (!build_command_from_record(row, command)) {
    const nlohmann::json result = {
        {"error_code", "INVALID_COMMAND_TYPE"},
        {"message", "command_type not recognized"},
        {"command_type", row.command_type},
    };
    const nlohmann::json detail = {
        {"event", "COMMAND_RETRY_INVALID"},
        {"command_type", row.command_type},
    };
    mark_terminal(row.command_id, "FAILED", result, "COMMAND_RETRY_INVALID", detail, now_ms);
    return;
  }

  std::string dispatch_error;
  if (push_command_to_agent(row.agent_id, command, dispatch_error)) {
    const nlohmann::json detail = {
        {"event", "COMMAND_RETRY_DISPATCHED"},
        {"retry_count", row.retry_count},
        {"max_retry", row.max_retry},
    };
    append_retry_event(row.command_id, "COMMAND_RETRY_DISPATCHED", "DISPATCHED", detail, now_ms);
    return;
  }

  const int next_retry_count = row.retry_count + 1;
  if (next_retry_count >= row.max_retry) {
    const nlohmann::json result = {
        {"error_code", "DISPATCH_RETRY_EXHAUSTED"},
        {"message", dispatch_error},
        {"retry_count", next_retry_count},
        {"max_retry", row.max_retry},
    };
    const nlohmann::json detail = {
        {"event", "COMMAND_RETRY_EXHAUSTED"},
        {"retry_count", next_retry_count},
        {"max_retry", row.max_retry},
        {"error", dispatch_error},
    };
    mark_terminal(row.command_id, "FAILED", result, "COMMAND_RETRY_EXHAUSTED", detail, now_ms);
    record_command_retry_exhausted(row.command_id, dispatch_error);
    return;
  }

  const int64_t next_retry_at_ms = now_ms + compute_retry_delay_ms(next_retry_count);
  std::string error;
  if (!update_command_retry_state(
          row.command_id,
          "RETRY_PENDING",
          next_retry_count,
          next_retry_at_ms,
          dispatch_error,
          now_ms,
          error)) {
    log::warn("update retry state failed: command_id={}, err={}", row.command_id, error);
    return;
  }

  const nlohmann::json detail = {
      {"event", "COMMAND_RETRY_SCHEDULED"},
      {"retry_count", next_retry_count},
      {"max_retry", row.max_retry},
      {"next_retry_at_ms", next_retry_at_ms},
      {"error", dispatch_error},
  };
  append_retry_event(row.command_id, "COMMAND_RETRY_SCHEDULED", "RETRY_PENDING", detail, now_ms);
  record_command_retry(row.command_id, next_retry_count, dispatch_error);
}

void retry_scheduler_loop() {
  while (g_scheduler_running.load(std::memory_order_relaxed)) {
    const auto now_ms = control::unix_time_ms_now();

    std::vector<command_record> due_commands;
    std::string error;
    if (!list_retry_ready_commands(now_ms, 100, due_commands, error)) {
      log::warn("list retry-ready commands failed: {}", error);
    } else {
      for (const auto& row : due_commands) {
        process_retry_command(row, now_ms);
      }
    }

    std::unique_lock<std::mutex> lk(g_scheduler_mutex);
    g_scheduler_cv.wait_for(lk, std::chrono::milliseconds(500), []() {
      return !g_scheduler_running.load(std::memory_order_relaxed);
    });
  }
}

} // namespace

bool start_command_retry_scheduler() {
  std::lock_guard<std::mutex> lk(g_scheduler_mutex);
  if (g_scheduler_running.load(std::memory_order_relaxed)) {
    return true;
  }

  g_scheduler_running.store(true, std::memory_order_relaxed);
  g_scheduler_thread = std::thread(retry_scheduler_loop);
  log::info("command retry scheduler started");
  return true;
}

void stop_command_retry_scheduler() {
  std::thread worker;
  {
    std::lock_guard<std::mutex> lk(g_scheduler_mutex);
    if (!g_scheduler_running.load(std::memory_order_relaxed)) {
      return;
    }

    g_scheduler_running.store(false, std::memory_order_relaxed);
    g_scheduler_cv.notify_all();
    worker = std::move(g_scheduler_thread);
  }

  if (worker.joinable()) {
    worker.join();
  }
  log::info("command retry scheduler stopped");
}

bool is_command_retry_scheduler_running() {
  return g_scheduler_running.load(std::memory_order_relaxed);
}

} // namespace service
