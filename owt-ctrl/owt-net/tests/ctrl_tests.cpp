#include "app/ws/agent_protocol.h"
#include "app/ws/jsonrpc_protocol.h"
#include "ctrl/adapters/control_ws_use_cases.h"
#include "ctrl/adapters/frontend_status_use_cases.h"
#include "ctrl/adapters/http_use_cases.h"
#include "ctrl/infrastructure/sqlite_store.h"
#include "ctrl/domain/types.h"
#include "ctrl/ports/interfaces.h"
#include "ctrl/application/agent_message_service.h"
#include "ctrl/application/agent_registry_service.h"
#include "ctrl/application/audit_query_service.h"
#include "ctrl/application/command_orchestrator.h"
#include "ctrl/application/params_service.h"
#include "ctrl/application/rate_limiter_service.h"
#include "ctrl/application/redaction_service.h"
#include "ctrl/application/retry_service.h"

#include <nlohmann/json.hpp>
#include <sqlite3.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

void require(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void sqlite_exec(sqlite3* db, const std::string& sql, const std::string& context) {
  char* err = nullptr;
  const int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err);
  if (rc != SQLITE_OK) {
    const std::string detail = err != nullptr ? err : (db != nullptr ? sqlite3_errmsg(db) : "sqlite error");
    sqlite3_free(err);
    throw std::runtime_error(context + ": " + detail);
  }
}

class test_clock final : public ctrl::ports::IClock {
public:
  int64_t now_ms() const override {
    return now_ms_;
  }

  void set(int64_t value) {
    now_ms_ = value;
  }

  void advance(int64_t delta) {
    now_ms_ += delta;
  }

private:
  int64_t now_ms_ = 0;
};

class test_id_generator final : public ctrl::ports::IIdGenerator {
public:
  std::string next_command_id() override {
    ++seq_;
    return "cmd-" + std::to_string(seq_);
  }

  std::string next_trace_id(std::string_view command_id) override {
    if (command_id.empty()) {
      ++seq_;
      return "trc-" + std::to_string(seq_);
    }
    return "trc-" + std::string(command_id);
  }

private:
  int seq_ = 0;
};

class in_memory_command_repository final : public ctrl::ports::ICommandRepository {
public:
  bool upsert(const ctrl::domain::CommandSnapshot& row, std::string& error) override {
    std::lock_guard<std::mutex> lk(mutex_);
    rows_[row.spec.command_id] = row;
    error.clear();
    return true;
  }

  bool get(
      std::string_view command_id,
      ctrl::domain::CommandSnapshot& out,
      std::string& error) const override {
    std::lock_guard<std::mutex> lk(mutex_);
    const auto it = rows_.find(std::string(command_id));
    if (it == rows_.end()) {
      error = "command not found";
      return false;
    }
    out = it->second;
    error.clear();
    return true;
  }

  bool list(
      const ctrl::domain::CommandListFilter& filter,
      ctrl::domain::ListPage<ctrl::domain::CommandSnapshot, ctrl::domain::CommandListCursor>& out,
      std::string& error) const override {
    std::lock_guard<std::mutex> lk(mutex_);
    std::vector<ctrl::domain::CommandSnapshot> sorted;
    sorted.reserve(rows_.size());
    for (const auto& [id, row] : rows_) {
      (void)id;
      if (!filter.agent_mac.empty() && row.agent.mac != filter.agent_mac) {
        continue;
      }
      if (filter.state.has_value() && row.state != *filter.state) {
        continue;
      }
      if (filter.kind.has_value() && row.spec.kind != *filter.kind) {
        continue;
      }
      if (filter.cursor.has_value()) {
        const auto& cursor = *filter.cursor;
        if (row.created_at_ms > cursor.created_at_ms) {
          continue;
        }
        if (row.created_at_ms == cursor.created_at_ms && row.spec.command_id >= cursor.command_id) {
          continue;
        }
      }
      sorted.push_back(row);
    }
    std::sort(
        sorted.begin(),
        sorted.end(),
        [](const ctrl::domain::CommandSnapshot& lhs, const ctrl::domain::CommandSnapshot& rhs) {
          if (lhs.created_at_ms == rhs.created_at_ms) {
            return lhs.spec.command_id > rhs.spec.command_id;
          }
          return lhs.created_at_ms > rhs.created_at_ms;
        });

    const int safe_limit = std::max(1, std::min(filter.limit, 500));
    out = {};
    for (size_t i = 0; i < sorted.size(); ++i) {
      if (static_cast<int>(i) < safe_limit) {
        out.items.push_back(sorted[i]);
      } else {
        out.has_more = true;
      }
    }
    if (out.has_more && !out.items.empty()) {
      const auto& last = out.items.back();
      out.next_cursor = ctrl::domain::CommandListCursor{last.created_at_ms, last.spec.command_id};
    }
    error.clear();
    return true;
  }

  bool append_event(const ctrl::domain::CommandEvent& event, std::string& error) override {
    std::lock_guard<std::mutex> lk(mutex_);
    events_[event.command_id].push_back(event);
    error.clear();
    return true;
  }

  bool list_events(
      std::string_view command_id,
      int limit,
      std::vector<ctrl::domain::CommandEvent>& out,
      std::string& error) const override {
    std::lock_guard<std::mutex> lk(mutex_);
    out.clear();
    const auto it = events_.find(std::string(command_id));
    if (it == events_.end()) {
      error.clear();
      return true;
    }
    const int safe_limit = std::max(1, std::min(limit, 500));
    for (size_t i = 0; i < it->second.size() && static_cast<int>(i) < safe_limit; ++i) {
      out.push_back(it->second[i]);
    }
    error.clear();
    return true;
  }

  bool update_state_if_not_terminal(
      std::string_view command_id,
      ctrl::domain::CommandState next_state,
      const nlohmann::json& result,
      int64_t updated_at_ms,
      bool& applied,
      std::string& error) override {
    std::lock_guard<std::mutex> lk(mutex_);
    const auto it = rows_.find(std::string(command_id));
    if (it == rows_.end()) {
      error = "command not found";
      return false;
    }
    if (ctrl::domain::is_terminal(it->second.state)) {
      applied = false;
      error.clear();
      return true;
    }
    it->second.state = next_state;
    it->second.result = result;
    it->second.next_retry_at_ms = 0;
    it->second.last_error.clear();
    it->second.updated_at_ms = updated_at_ms;
    applied = true;
    error.clear();
    return true;
  }

  bool update_terminal_state_once(
      std::string_view command_id,
      ctrl::domain::CommandState terminal_state,
      const nlohmann::json& result,
      int64_t updated_at_ms,
      bool& applied,
      std::string& error) override {
    std::lock_guard<std::mutex> lk(mutex_);
    if (!ctrl::domain::is_terminal(terminal_state)) {
      error = "terminal_state is not terminal";
      return false;
    }
    const auto it = rows_.find(std::string(command_id));
    if (it == rows_.end()) {
      error = "command not found";
      return false;
    }
    if (ctrl::domain::is_terminal(it->second.state)) {
      applied = false;
      error.clear();
      return true;
    }
    it->second.state = terminal_state;
    it->second.result = result;
    it->second.next_retry_at_ms = 0;
    it->second.last_error.clear();
    it->second.updated_at_ms = updated_at_ms;
    applied = true;
    error.clear();
    return true;
  }

  bool list_retry_ready(
      int64_t now_ms,
      int limit,
      std::vector<ctrl::domain::CommandSnapshot>& out,
      std::string& error) const override {
    std::lock_guard<std::mutex> lk(mutex_);
    out.clear();
    for (const auto& [id, row] : rows_) {
      (void)id;
      if (row.state != ctrl::domain::CommandState::RetryPending) {
        continue;
      }
      if (row.retry_count >= row.spec.max_retry) {
        continue;
      }
      if (row.next_retry_at_ms > now_ms) {
        continue;
      }
      out.push_back(row);
    }
    std::sort(
        out.begin(),
        out.end(),
        [](const ctrl::domain::CommandSnapshot& lhs, const ctrl::domain::CommandSnapshot& rhs) {
          if (lhs.next_retry_at_ms == rhs.next_retry_at_ms) {
            return lhs.updated_at_ms < rhs.updated_at_ms;
          }
          return lhs.next_retry_at_ms < rhs.next_retry_at_ms;
        });
    const int safe_limit = std::max(1, std::min(limit, 500));
    if (static_cast<int>(out.size()) > safe_limit) {
      out.resize(static_cast<size_t>(safe_limit));
    }
    error.clear();
    return true;
  }

  bool update_retry_state(
      std::string_view command_id,
      ctrl::domain::CommandState next_state,
      int retry_count,
      int64_t next_retry_at_ms,
      std::string_view last_error,
      int64_t updated_at_ms,
      std::string& error) override {
    std::lock_guard<std::mutex> lk(mutex_);
    const auto it = rows_.find(std::string(command_id));
    if (it == rows_.end() || ctrl::domain::is_terminal(it->second.state)) {
      error = "command not found or already terminal";
      return false;
    }
    it->second.state = next_state;
    it->second.retry_count = retry_count;
    it->second.next_retry_at_ms = next_retry_at_ms;
    it->second.last_error = std::string(last_error);
    it->second.updated_at_ms = updated_at_ms;
    error.clear();
    return true;
  }

  bool recover_inflight(int64_t recovered_at_ms, int& recovered_count, std::string& error) override {
    std::lock_guard<std::mutex> lk(mutex_);
    recovered_count = 0;
    for (auto& [id, row] : rows_) {
      (void)id;
      if (ctrl::domain::is_terminal(row.state)) {
        continue;
      }
      row.state = ctrl::domain::CommandState::TimedOut;
      row.result = nlohmann::json{
          {"error_code", "ctrl_restart_recovery"},
          {"message", "controller restarted before terminal result"},
      };
      row.updated_at_ms = recovered_at_ms;
      row.next_retry_at_ms = 0;
      row.last_error.clear();
      events_[row.spec.command_id].push_back(ctrl::domain::CommandEvent{
          row.spec.command_id,
          "command_recovery_timeout",
          ctrl::domain::CommandState::TimedOut,
          nlohmann::json{{"reason", "ctrl_restart_recovery"}},
          recovered_at_ms,
      });
      ++recovered_count;
    }
    error.clear();
    return true;
  }

private:
  mutable std::mutex mutex_;
  std::unordered_map<std::string, ctrl::domain::CommandSnapshot> rows_;
  std::unordered_map<std::string, std::vector<ctrl::domain::CommandEvent>> events_;
};

class in_memory_agent_repository final : public ctrl::ports::IAgentRepository {
public:
  bool upsert(const ctrl::domain::AgentState& row, std::string& error) override {
    std::lock_guard<std::mutex> lk(mutex_);
    rows_[row.agent.mac] = row;
    error.clear();
    return true;
  }

  bool get(std::string_view agent_mac, ctrl::domain::AgentState& out, std::string& error) const override {
    std::lock_guard<std::mutex> lk(mutex_);
    const auto it = rows_.find(std::string(agent_mac));
    if (it == rows_.end()) {
      error = "agent not found";
      return false;
    }
    out = it->second;
    error.clear();
    return true;
  }

  bool list(std::vector<ctrl::domain::AgentState>& out, std::string& error) const override {
    std::lock_guard<std::mutex> lk(mutex_);
    out.clear();
    for (const auto& [id, row] : rows_) {
      (void)id;
      out.push_back(row);
    }
    error.clear();
    return true;
  }

  bool mark_all_offline(int64_t updated_at_ms, std::string& error) override {
    std::lock_guard<std::mutex> lk(mutex_);
    for (auto& [id, row] : rows_) {
      (void)id;
      row.online = false;
      row.last_seen_at_ms = updated_at_ms;
    }
    error.clear();
    return true;
  }

private:
  mutable std::mutex mutex_;
  std::unordered_map<std::string, ctrl::domain::AgentState> rows_;
};

class in_memory_params_repository final : public ctrl::ports::IParamsRepository {
public:
  bool load(std::string_view agent_mac, nlohmann::json& out, std::string& error) const override {
    std::lock_guard<std::mutex> lk(mutex_);
    const auto it = rows_.find(std::string(agent_mac));
    if (it == rows_.end()) {
      error = "agent params not found";
      return false;
    }
    out = it->second;
    error.clear();
    return true;
  }

  bool save(
      std::string_view agent_mac,
      const nlohmann::json& params,
      int64_t updated_at_ms,
      std::string& error) override {
    (void)updated_at_ms;
    std::lock_guard<std::mutex> lk(mutex_);
    rows_[std::string(agent_mac)] = params;
    error.clear();
    return true;
  }

private:
  mutable std::mutex mutex_;
  std::unordered_map<std::string, nlohmann::json> rows_;
};

class in_memory_audit_repository final : public ctrl::ports::IAuditRepository {
public:
  bool append(const ctrl::domain::AuditEntry& row, std::string& error) override {
    std::lock_guard<std::mutex> lk(mutex_);
    auto copy = row;
    if (copy.id <= 0) {
      copy.id = ++seq_;
    }
    rows_.push_back(copy);
    error.clear();
    return true;
  }

  bool list(
      const ctrl::domain::AuditListFilter& filter,
      ctrl::domain::ListPage<ctrl::domain::AuditEntry, ctrl::domain::AuditListCursor>& out,
      std::string& error) const override {
    std::lock_guard<std::mutex> lk(mutex_);
    std::vector<ctrl::domain::AuditEntry> filtered;
    filtered.reserve(rows_.size());
    for (const auto& row : rows_) {
      if (filter.action.has_value() && row.action != *filter.action) {
        continue;
      }
      if (filter.actor_type.has_value() && row.actor_type != *filter.actor_type) {
        continue;
      }
      if (filter.actor_id.has_value() && row.actor_id != *filter.actor_id) {
        continue;
      }
      if (filter.resource_type.has_value() && row.resource_type != *filter.resource_type) {
        continue;
      }
      if (filter.resource_id.has_value() && row.resource_id != *filter.resource_id) {
        continue;
      }
      if (filter.cursor.has_value() && row.id >= filter.cursor->id) {
        continue;
      }
      filtered.push_back(row);
    }
    std::sort(
        filtered.begin(),
        filtered.end(),
        [](const ctrl::domain::AuditEntry& lhs, const ctrl::domain::AuditEntry& rhs) {
          return lhs.id > rhs.id;
        });

    const int safe_limit = std::max(1, std::min(filter.limit, 500));
    out = {};
    for (size_t i = 0; i < filtered.size(); ++i) {
      if (static_cast<int>(i) < safe_limit) {
        out.items.push_back(filtered[i]);
      } else {
        out.has_more = true;
      }
    }
    if (out.has_more && !out.items.empty()) {
      out.next_cursor = ctrl::domain::AuditListCursor{out.items.back().id};
    }
    error.clear();
    return true;
  }

private:
  mutable std::mutex mutex_;
  int64_t seq_ = 0;
  std::vector<ctrl::domain::AuditEntry> rows_;
};

class fake_agent_channel final : public ctrl::ports::IAgentChannel {
public:
  bool is_online(std::string_view agent_mac) const override {
    const auto it = online_.find(std::string(agent_mac));
    return it != online_.end() && it->second;
  }

  bool send_command(
      std::string_view agent_mac,
      const ctrl::domain::CommandSpec& cmd,
      std::string& error) override {
    if (!is_online(agent_mac)) {
      error = "agent not connected";
      return false;
    }
    if (fail_next_) {
      fail_next_ = false;
      error = fail_message_;
      return false;
    }
    sent_commands_.push_back({std::string(agent_mac), cmd});
    error.clear();
    return true;
  }

  void set_online(std::string agent_mac, bool online) {
    online_[std::move(agent_mac)] = online;
  }

  void fail_once(std::string error_message) {
    fail_next_ = true;
    fail_message_ = std::move(error_message);
  }

  std::vector<std::pair<std::string, ctrl::domain::CommandSpec>> sent_commands_;

private:
  std::unordered_map<std::string, bool> online_;
  bool fail_next_ = false;
  std::string fail_message_ = "send failed";
};

class fake_status_publisher final : public ctrl::ports::IStatusPublisher {
public:
  void publish_snapshot(std::string_view reason, std::string_view agent_mac) override {
    snapshots_.push_back({std::string(reason), std::string(agent_mac)});
  }

  void publish_agent(std::string_view reason, std::string_view agent_mac) override {
    agents_.push_back({std::string(reason), std::string(agent_mac)});
  }

  std::vector<std::pair<std::string, std::string>> snapshots_;
  std::vector<std::pair<std::string, std::string>> agents_;
};

class fake_metrics final : public ctrl::ports::IMetrics {
public:
  void record_http_request() override {
    ++http_requests_total_;
  }
  void record_rate_limited(std::string_view actor_id, int64_t retry_after_ms) override {
    (void)actor_id;
    (void)retry_after_ms;
    ++rate_limited_total_;
  }
  void record_command_push() override {
    ++command_push_total_;
  }
  void record_command_retry(
      std::string_view command_id,
      int retry_count,
      std::string_view reason) override {
    (void)command_id;
    (void)retry_count;
    (void)reason;
    ++command_retry_total_;
  }
  void record_command_retry_exhausted(std::string_view command_id, std::string_view reason) override {
    (void)command_id;
    (void)reason;
    ++command_retry_exhausted_total_;
  }
  void record_command_terminal_status(
      std::string_view command_id,
      ctrl::domain::CommandState state,
      const nlohmann::json& detail) override {
    (void)command_id;
    (void)detail;
    if (state == ctrl::domain::CommandState::Succeeded) {
      ++command_succeeded_total_;
    }
    if (state == ctrl::domain::CommandState::Failed) {
      ++command_failed_total_;
    }
    if (state == ctrl::domain::CommandState::TimedOut) {
      ++command_timed_out_total_;
    }
  }

  uint64_t http_requests_total_ = 0;
  uint64_t rate_limited_total_ = 0;
  uint64_t command_push_total_ = 0;
  uint64_t command_retry_total_ = 0;
  uint64_t command_retry_exhausted_total_ = 0;
  uint64_t command_succeeded_total_ = 0;
  uint64_t command_failed_total_ = 0;
  uint64_t command_timed_out_total_ = 0;
};

void test_command_orchestrator_submit() {
  test_clock clock;
  clock.set(1000);
  test_id_generator id_generator;
  in_memory_command_repository command_repo;
  in_memory_params_repository params_repo;
  in_memory_audit_repository audit_repo;
  fake_agent_channel channel;
  fake_status_publisher publisher;
  fake_metrics metrics;

  ctrl::application::ParamsService params_service(params_repo, clock);
  ctrl::application::CommandOrchestrator orchestrator(
      command_repo,
      channel,
      params_service,
      audit_repo,
      publisher,
      metrics,
      clock,
      id_generator);

  channel.set_online("AA:00:00:00:10:01", true);

  ctrl::application::SubmitCommandInput wake_input;
  wake_input.agent = {"AA:00:00:00:10:01", "agent-10-01"};
  wake_input.kind = ctrl::domain::CommandKind::WakeOnLan;
  wake_input.payload = nlohmann::json{{"mac", "AA:BB:CC:DD:EE:FF"}};
  wake_input.wait_result = false;
  wake_input.actor_type = "test";
  wake_input.actor_id = "tester";
  auto wake_output = orchestrator.submit(wake_input);
  require(
      wake_output.state == ctrl::domain::CommandState::Dispatched,
      "wake command should be dispatched");
  require(channel.sent_commands_.size() == 1, "wake command should be sent to channel");

  channel.set_online("AA:00:00:00:10:01", false);
  ctrl::application::SubmitCommandInput retry_input = wake_input;
  retry_input.kind = ctrl::domain::CommandKind::HostReboot;
  retry_input.max_retry = 2;
  retry_input.wait_result = true;
  retry_input.wait_timeout_ms = 1000;
  auto retry_output = orchestrator.submit(retry_input);
  require(
      retry_output.state == ctrl::domain::CommandState::RetryPending,
      "offline command should become retry pending");

  ctrl::application::SubmitCommandInput params_get_input = wake_input;
  params_get_input.kind = ctrl::domain::CommandKind::ParamsGet;
  params_get_input.wait_result = true;
  auto params_get_output = orchestrator.submit(params_get_input);
  require(
      params_get_output.state == ctrl::domain::CommandState::Succeeded,
      "params_get should succeed locally");
  require(params_get_output.terminal, "params_get should be terminal");
  require(params_get_output.result.is_object(), "params_get should return object");
  require(params_get_output.result.contains("wol"), "params_get result should include wol");

  channel.set_online("AA:00:00:00:10:01", true);
  ctrl::application::SubmitCommandInput params_set_input = wake_input;
  params_set_input.kind = ctrl::domain::CommandKind::ParamsSet;
  params_set_input.payload = nlohmann::json{{"ssh", {{"host", "10.0.0.9"}}}};
  params_set_input.wait_result = false;
  auto params_set_output = orchestrator.submit(params_set_input);
  require(
      params_set_output.state == ctrl::domain::CommandState::Dispatched,
      "params_set should be dispatched");

  nlohmann::json stored_params;
  std::string error;
  require(params_repo.load("AA:00:00:00:10:01", stored_params, error), "params should be persisted");
  require(
      stored_params["ssh"]["host"].get<std::string>() == "10.0.0.9",
      "params_set should persist merged params");
}

void test_agent_message_terminal_once() {
  test_clock clock;
  clock.set(2000);
  in_memory_command_repository command_repo;
  in_memory_agent_repository agent_repo;
  fake_status_publisher publisher;
  fake_metrics metrics;

  ctrl::application::AgentRegistryService registry(agent_repo, clock);
  ctrl::application::AgentMessageService service(command_repo, registry, publisher, metrics, clock);

  ctrl::domain::CommandSnapshot seed;
  seed.spec.command_id = "cmd-ack-result";
  seed.spec.trace_id = "trc-ack-result";
  seed.spec.kind = ctrl::domain::CommandKind::HostReboot;
  seed.agent = {"AA:00:00:00:20:01", "agent-20-01"};
  seed.state = ctrl::domain::CommandState::Dispatched;
  seed.created_at_ms = 2000;
  seed.updated_at_ms = 2000;
  std::string error;
  require(command_repo.upsert(seed, error), "seed command should be stored");

  service.on_command_ack(
      "AA:00:00:00:20:01",
      "cmd-ack-result",
      ctrl::domain::CommandState::Acked,
      "trc-ack-result",
      "accepted");
  ctrl::domain::CommandSnapshot row;
  require(command_repo.get("cmd-ack-result", row, error), "command should exist");
  require(row.state == ctrl::domain::CommandState::Acked, "ack should update state");

  service.on_command_result(
      "AA:00:00:00:20:01",
      "cmd-ack-result",
      ctrl::domain::CommandState::Succeeded,
      0,
      nlohmann::json{{"ok", true}},
      "trc-ack-result");
  require(command_repo.get("cmd-ack-result", row, error), "command should exist");
  require(row.state == ctrl::domain::CommandState::Succeeded, "first terminal result should apply");

  service.on_command_result(
      "AA:00:00:00:20:01",
      "cmd-ack-result",
      ctrl::domain::CommandState::Failed,
      -1,
      nlohmann::json{{"ok", false}},
      "trc-ack-result");
  require(command_repo.get("cmd-ack-result", row, error), "command should exist");
  require(
      row.state == ctrl::domain::CommandState::Succeeded,
      "duplicate terminal should not overwrite");

  std::vector<ctrl::domain::CommandEvent> events;
  require(command_repo.list_events("cmd-ack-result", 100, events, error), "list_events should succeed");
  bool has_duplicate_event = false;
  for (const auto& event : events) {
    if (event.type == "command_result_duplicate") {
      has_duplicate_event = true;
      break;
    }
  }
  require(has_duplicate_event, "duplicate terminal event should be recorded");
}

void test_retry_service() {
  test_clock clock;
  clock.set(3000);
  in_memory_command_repository command_repo;
  fake_agent_channel channel;
  fake_status_publisher publisher;
  fake_metrics metrics;
  ctrl::application::RetryService retry_service(command_repo, channel, publisher, metrics, clock);

  std::string error;
  ctrl::domain::CommandSnapshot retry_pending;
  retry_pending.spec.command_id = "cmd-retry-1";
  retry_pending.spec.trace_id = "trc-retry-1";
  retry_pending.spec.kind = ctrl::domain::CommandKind::HostProbeGet;
  retry_pending.spec.max_retry = 3;
  retry_pending.spec.expires_at_ms = 3000 + 60000;
  retry_pending.agent = {"AA:00:00:00:30:01", "agent-30-01"};
  retry_pending.state = ctrl::domain::CommandState::RetryPending;
  retry_pending.retry_count = 0;
  retry_pending.next_retry_at_ms = 3000;
  retry_pending.created_at_ms = 3000;
  retry_pending.updated_at_ms = 3000;
  require(command_repo.upsert(retry_pending, error), "seed retry row should succeed");

  channel.set_online("AA:00:00:00:30:01", false);
  retry_service.tick_once();

  ctrl::domain::CommandSnapshot row;
  require(command_repo.get("cmd-retry-1", row, error), "row should exist");
  require(row.state == ctrl::domain::CommandState::RetryPending, "row should stay retry pending");
  require(row.retry_count == 1, "retry count should increment");

  channel.set_online("AA:00:00:00:30:01", true);
  clock.set(row.next_retry_at_ms);
  retry_service.tick_once();
  require(command_repo.get("cmd-retry-1", row, error), "row should exist");
  require(row.state == ctrl::domain::CommandState::Dispatched, "row should be dispatched when online");

  ctrl::domain::CommandSnapshot exhausted;
  exhausted.spec.command_id = "cmd-retry-exhausted";
  exhausted.spec.trace_id = "trc-retry-exhausted";
  exhausted.spec.kind = ctrl::domain::CommandKind::HostProbeGet;
  exhausted.spec.max_retry = 1;
  exhausted.spec.expires_at_ms = clock.now_ms() + 60000;
  exhausted.agent = {"AA:00:00:00:30:02", "agent-30-02"};
  exhausted.state = ctrl::domain::CommandState::RetryPending;
  exhausted.retry_count = 0;
  exhausted.next_retry_at_ms = clock.now_ms();
  exhausted.created_at_ms = clock.now_ms();
  exhausted.updated_at_ms = clock.now_ms();
  require(command_repo.upsert(exhausted, error), "seed exhausted row should succeed");

  channel.set_online("AA:00:00:00:30:02", false);
  retry_service.tick_once();
  require(command_repo.get("cmd-retry-exhausted", row, error), "row should exist");
  require(
      row.state == ctrl::domain::CommandState::Failed,
      "retry exhausted row should become failed");

  ctrl::domain::CommandSnapshot inflight;
  inflight.spec.command_id = "cmd-recover";
  inflight.spec.trace_id = "trc-recover";
  inflight.spec.kind = ctrl::domain::CommandKind::HostReboot;
  inflight.spec.max_retry = 0;
  inflight.agent = {"AA:00:00:00:30:03", "agent-30-03"};
  inflight.state = ctrl::domain::CommandState::Running;
  inflight.created_at_ms = clock.now_ms();
  inflight.updated_at_ms = clock.now_ms();
  require(command_repo.upsert(inflight, error), "seed inflight row should succeed");

  retry_service.recover_inflight_on_boot();
  require(command_repo.get("cmd-recover", row, error), "recovered row should exist");
  require(
      row.state == ctrl::domain::CommandState::TimedOut,
      "recover should convert inflight row to timed out");
}

void test_params_rate_limiter_redaction() {
  test_clock clock;
  clock.set(4000);
  in_memory_params_repository params_repo;
  ctrl::application::ParamsService params_service(params_repo, clock);

  const auto params = params_service.load_or_init("AA:00:00:00:40:01");
  require(params.contains("wol"), "default params should contain wol");
  require(params.contains("ssh"), "default params should contain ssh");

  const auto merged = params_service.merge_and_validate(
      "AA:00:00:00:40:01",
      nlohmann::json{{"ssh", {{"host", "10.0.0.2"}, {"timeout_ms", 7000}}}});
  require(
      merged["ssh"]["host"].get<std::string>() == "10.0.0.2",
      "merge should update ssh host");

  bool invalid_throw = false;
  try {
    (void)params_service.merge_and_validate(
        "AA:00:00:00:40:01",
        nlohmann::json{{"ssh", {{"port", "not-int"}}}});
  } catch (const std::invalid_argument&) {
    invalid_throw = true;
  }
  require(invalid_throw, "invalid patch should throw invalid_argument");

  ctrl::application::RateLimiterService limiter;
  limiter.configure(true, 1, 1);
  int64_t retry_after_ms = 0;
  require(limiter.allow("actor-1", 1000, retry_after_ms), "first request should pass");
  require(!limiter.allow("actor-1", 1000, retry_after_ms), "second request should be limited");
  require(retry_after_ms > 0, "retry_after_ms should be positive");
  require(limiter.allow("actor-1", 2000, retry_after_ms), "request after refill should pass");

  ctrl::application::RedactionService redaction;
  const auto redacted =
      redaction.redact_text(R"({"password":"my-secret","token":"abc","keep":"ok"})");
  require(redacted.find("my-secret") == std::string::npos, "password should be redacted");
  require(redacted.find("abc") == std::string::npos, "token should be redacted");
}

void test_model_types_snake_case_mapping() {
  using ctrl::domain::CommandKind;
  using ctrl::domain::CommandState;

  require(
      ctrl::domain::to_string(CommandKind::WakeOnLan) == "wol_wake",
      "command kind string should be snake_case");
  require(
      ctrl::domain::to_string(CommandState::RetryPending) == "retry_pending",
      "command state string should be snake_case");

  CommandKind kind = CommandKind::HostProbeGet;
  require(
      ctrl::domain::try_parse_command_kind("host_reboot", kind) && kind == CommandKind::HostReboot,
      "snake_case command kind parsing should succeed");
  require(
      ctrl::domain::try_parse_command_kind("HOST_REBOOT", kind) && kind == CommandKind::HostReboot,
      "legacy uppercase command kind parsing should still succeed");

  CommandState state = CommandState::Created;
  require(
      ctrl::domain::try_parse_command_state("succeeded", state) && state == CommandState::Succeeded,
      "snake_case command state parsing should succeed");
  require(
      ctrl::domain::try_parse_command_state("SUCCEEDED", state) && state == CommandState::Succeeded,
      "legacy uppercase command state parsing should still succeed");
}

void test_agent_envelope_v3_codec() {
  app::ws::AgentEnvelope envelope;
  envelope.type = "heartbeat";
  envelope.meta.version = "v3";
  envelope.meta.trace_id = "trc-1";
  envelope.meta.ts_ms = 123456;
  envelope.meta.agent_id = "agent-1";
  envelope.payload = nlohmann::json{{"heartbeat_at_ms", 123450}, {"stats", nlohmann::json::object()}};

  const auto encoded = app::ws::encode_agent_envelope(envelope);
  app::ws::AgentEnvelope decoded;
  std::string error;
  require(
      app::ws::parse_agent_envelope(encoded, decoded, error),
      std::string("agent envelope roundtrip should parse: ") + error);
  require(decoded.type == "heartbeat", "decoded type mismatch");
  require(decoded.meta.version == "v3", "decoded version mismatch");
  require(decoded.meta.trace_id == "trc-1", "decoded trace_id mismatch");
  require(decoded.meta.agent_id == "agent-1", "decoded agent_id mismatch");
  require(decoded.payload["heartbeat_at_ms"].get<int64_t>() == 123450, "decoded payload mismatch");

  std::string invalid_error;
  app::ws::AgentEnvelope ignored;
  require(
      !app::ws::parse_agent_envelope(R"({"type":"heartbeat","payload":{}})", ignored, invalid_error),
      "missing meta should fail");
  require(
      !app::ws::parse_agent_envelope(
          R"({"type":"heartbeat","meta":{"version":"v3","trace_id":"t","ts_ms":1},"payload":{},"x":1})",
          ignored,
          invalid_error),
      "unknown envelope field should fail");
}

void test_jsonrpc_request_validation() {
  app::ws::JsonRpcRequest req;
  std::string error;
  int error_code = 0;

  require(
      app::ws::parse_jsonrpc_request(
          R"({"jsonrpc":"2.0","id":"1","method":"agent_list","params":{"include_offline":true}})",
          req,
          error,
          error_code),
      std::string("valid jsonrpc request should parse: ") + error);
  require(!req.notification, "request with id should not be notification");
  require(req.method == "agent_list", "method parse mismatch");
  require(req.params["include_offline"].get<bool>(), "params parse mismatch");

  require(
      !app::ws::parse_jsonrpc_request(
          R"({"jsonrpc":"2.0","id":"1","method":"agent_list","extra":1})",
          req,
          error,
          error_code),
      "request with unknown field should fail");
  require(error_code == -32600, "unknown field should map to invalid request");

  require(
      !app::ws::parse_jsonrpc_request(
          R"({"jsonrpc":"2.0","id":"1","method":"agent_list","params":[1,2]})",
          req,
          error,
          error_code),
      "request with non-object params should fail");
  require(error_code == -32602, "invalid params should map to -32602");

  const auto result_text = app::ws::jsonrpc_result(
      "1",
      nlohmann::json{{"ok", true}},
      nlohmann::json{{"reason", "test"}});
  const auto result_json = nlohmann::json::parse(result_text, nullptr, false);
  require(result_json.is_object(), "jsonrpc_result should return object json");
  require(result_json.value("jsonrpc", "") == "2.0", "jsonrpc_result version mismatch");
  require(result_json.contains("result"), "jsonrpc_result should contain result");
  require(result_json["result"].contains("resource"), "jsonrpc_result should contain resource");
  require(result_json["result"].contains("meta"), "jsonrpc_result should contain meta");

  const auto error_text = app::ws::jsonrpc_error(
      "1",
      -32602,
      "invalid params",
      nlohmann::json{{"field", "params"}});
  const auto error_json = nlohmann::json::parse(error_text, nullptr, false);
  require(error_json.is_object(), "jsonrpc_error should return object json");
  require(error_json["error"]["code"].get<int>() == -32602, "jsonrpc_error code mismatch");
  require(error_json["error"]["data"].is_object(), "jsonrpc_error data should be object");
}

void test_ws_frontend_http_adapters() {
  test_clock clock;
  clock.set(5000);
  test_id_generator id_generator;
  in_memory_command_repository command_repo;
  in_memory_agent_repository agent_repo;
  in_memory_params_repository params_repo;
  in_memory_audit_repository audit_repo;
  fake_agent_channel channel;
  fake_status_publisher publisher;
  fake_metrics metrics;

  ctrl::application::ParamsService params_service(params_repo, clock);
  ctrl::application::AgentRegistryService registry(agent_repo, clock);
  ctrl::application::CommandOrchestrator orchestrator(
      command_repo,
      channel,
      params_service,
      audit_repo,
      publisher,
      metrics,
      clock,
      id_generator);
  ctrl::application::AgentMessageService message_service(
      command_repo,
      registry,
      publisher,
      metrics,
      clock);
  ctrl::application::AuditQueryService audit_query(audit_repo);

  ctrl::adapters::ControlWsUseCases ws_use_cases(registry, message_service);
  ctrl::adapters::FrontendStatusUseCases frontend_use_cases;
  ctrl::adapters::HttpUseCases http_use_cases(registry, orchestrator, audit_query);

  ws_use_cases.on_open("sess-1");
  std::vector<ctrl::adapters::WsOutboundMessage> ws_out;
  ws_use_cases.on_text(
      ctrl::adapters::WsInboundMessage{
          "sess-1",
          ctrl::adapters::WsMessageKind::Register,
          "trc-register",
          "AA:00:00:00:50:01",
          "agent-50-01",
          ctrl::domain::CommandState::Created,
          "",
          0,
          nlohmann::json{{"site_id", "lab-50"}, {"capabilities", {"host_probe_get"}}}},
      ws_out);
  require(!ws_out.empty(), "register should produce reply");
  require(ws_out.front().type == "register_ack", "reply should be register_ack");

  ctrl::domain::AgentState agent;
  require(
      registry.get_agent("AA:00:00:00:50:01", agent) && agent.online,
      "registered agent should be online");

  frontend_use_cases.subscribe_list("ui-list-1");
  frontend_use_cases.subscribe_agent("ui-agent-1", "AA:00:00:00:50:01");
  const auto snapshots = frontend_use_cases.trigger_snapshot(
      "agent_register",
      "AA:00:00:00:50:01",
      nlohmann::json{{"total_count", 1}});
  require(snapshots.size() == 1, "snapshot should be published to list subscriber");
  const auto updates = frontend_use_cases.trigger_agent(
      "agent_heartbeat",
      "AA:00:00:00:50:01",
      nlohmann::json{{"online", true}});
  require(updates.size() == 1, "agent update should be published to observer");

  channel.set_online("AA:00:00:00:50:01", true);
  ctrl::adapters::PushCommandRequest req;
  req.input.agent = {"AA:00:00:00:50:01", "agent-50-01"};
  req.input.kind = ctrl::domain::CommandKind::WakeOnLan;
  req.input.payload = nlohmann::json{{"mac", "AA:BB:CC:DD:EE:FF"}};
  req.input.wait_result = false;
  req.input.actor_type = "test";
  req.input.actor_id = "tester";

  const auto push_result = http_use_cases.push_command(req);
  require(push_result.ok, "http push should succeed");
  const auto command_id = push_result.data.output.command_id;
  require(!command_id.empty(), "command_id should be returned");

  const auto command_detail = http_use_cases.get_command(command_id, 100);
  require(command_detail.ok, "http get_command should succeed");
  require(
      command_detail.data.command.state == ctrl::domain::CommandState::Dispatched,
      "command should be dispatched");

  ctrl::domain::CommandListFilter command_filter;
  const auto command_list = http_use_cases.list_commands(command_filter);
  require(command_list.ok, "http list_commands should succeed");
  require(!command_list.data.items.empty(), "http list_commands should return at least one row");

  ctrl::domain::AuditListFilter audit_filter;
  const auto audit_list = http_use_cases.list_audits(audit_filter);
  require(audit_list.ok, "http list_audits should succeed");
  require(!audit_list.data.items.empty(), "http list_audits should return at least one row");

  const auto get_agent_result = http_use_cases.get_agent("AA:00:00:00:50:01");
  require(get_agent_result.ok, "http get_agent should succeed");

  ws_use_cases.on_close("sess-1");
  require(
      registry.get_agent("AA:00:00:00:50:01", agent) && !agent.online,
      "ws close should set agent offline");
}

void test_command_e2e_submit_ack_terminal_audit_query() {
  test_clock clock;
  clock.set(6000);
  test_id_generator id_generator;
  in_memory_command_repository command_repo;
  in_memory_agent_repository agent_repo;
  in_memory_params_repository params_repo;
  in_memory_audit_repository audit_repo;
  fake_agent_channel channel;
  fake_status_publisher publisher;
  fake_metrics metrics;

  ctrl::application::ParamsService params_service(params_repo, clock);
  ctrl::application::AgentRegistryService registry(agent_repo, clock);
  ctrl::application::CommandOrchestrator orchestrator(
      command_repo,
      channel,
      params_service,
      audit_repo,
      publisher,
      metrics,
      clock,
      id_generator);
  ctrl::application::AgentMessageService message_service(
      command_repo,
      registry,
      publisher,
      metrics,
      clock);
  ctrl::application::AuditQueryService audit_query(audit_repo);

  ctrl::adapters::ControlWsUseCases ws_use_cases(registry, message_service);
  ctrl::adapters::HttpUseCases http_use_cases(registry, orchestrator, audit_query);

  ws_use_cases.on_open("sess-e2e");
  std::vector<ctrl::adapters::WsOutboundMessage> ws_out;
  ws_use_cases.on_text(
      ctrl::adapters::WsInboundMessage{
          "sess-e2e",
          ctrl::adapters::WsMessageKind::Register,
          "trc-register-e2e",
          "AA:00:00:00:60:01",
          "agent-60-01",
          ctrl::domain::CommandState::Created,
          "",
          0,
          nlohmann::json{{"site_id", "lab-60"}, {"capabilities", {"host_reboot"}}}},
      ws_out);
  require(!ws_out.empty(), "register should return ack");

  channel.set_online("AA:00:00:00:60:01", true);

  ctrl::adapters::PushCommandRequest req;
  req.input.agent = {"AA:00:00:00:60:01", "agent-60-01"};
  req.input.kind = ctrl::domain::CommandKind::HostReboot;
  req.input.payload = nlohmann::json{
      {"host", "10.0.0.60"},
      {"port", 22},
      {"user", "root"},
  };
  req.input.wait_result = false;
  req.input.actor_type = "test";
  req.input.actor_id = "tester";

  const auto push_result = http_use_cases.push_command(req);
  require(push_result.ok, "push command should succeed");
  const auto command_id = push_result.data.output.command_id;
  require(!command_id.empty(), "command_id should not be empty");

  ws_out.clear();
  ws_use_cases.on_text(
      ctrl::adapters::WsInboundMessage{
          "sess-e2e",
          ctrl::adapters::WsMessageKind::CommandAck,
          "trc-ack-e2e",
          "AA:00:00:00:60:01",
          "agent-60-01",
          ctrl::domain::CommandState::Acked,
          command_id,
          0,
          nlohmann::json{{"message", "accepted"}}},
      ws_out);

  ws_use_cases.on_text(
      ctrl::adapters::WsInboundMessage{
          "sess-e2e",
          ctrl::adapters::WsMessageKind::CommandResult,
          "trc-result-e2e",
          "AA:00:00:00:60:01",
          "agent-60-01",
          ctrl::domain::CommandState::Succeeded,
          command_id,
          0,
          nlohmann::json{{"ok", true}, {"exit_status", 0}}},
      ws_out);

  const auto command_detail = http_use_cases.get_command(command_id, 100);
  require(command_detail.ok, "get command should succeed");
  require(
      command_detail.data.command.state == ctrl::domain::CommandState::Succeeded,
      "command should reach succeeded terminal");

  bool has_ack_event = false;
  bool has_result_event = false;
  for (const auto& event : command_detail.data.events) {
    if (event.type == "command_ack_received") {
      has_ack_event = true;
    }
    if (event.type == "command_result_received") {
      has_result_event = true;
    }
  }
  require(has_ack_event, "ack event should be persisted");
  require(has_result_event, "result event should be persisted");

  ws_use_cases.on_text(
      ctrl::adapters::WsInboundMessage{
          "sess-e2e",
          ctrl::adapters::WsMessageKind::CommandResult,
          "trc-result-e2e-dup",
          "AA:00:00:00:60:01",
          "agent-60-01",
          ctrl::domain::CommandState::Failed,
          command_id,
          -1,
          nlohmann::json{{"ok", false}, {"reason", "duplicate"}}},
      ws_out);

  const auto command_after_duplicate = http_use_cases.get_command(command_id, 100);
  require(command_after_duplicate.ok, "get command after duplicate should succeed");
  require(
      command_after_duplicate.data.command.state == ctrl::domain::CommandState::Succeeded,
      "duplicate terminal must not override stored terminal");

  bool has_duplicate_event = false;
  for (const auto& event : command_after_duplicate.data.events) {
    if (event.type == "command_result_duplicate") {
      has_duplicate_event = true;
      break;
    }
  }
  require(has_duplicate_event, "duplicate terminal event should be persisted");

  ctrl::domain::AuditListFilter filter;
  filter.resource_type = "command";
  filter.resource_id = command_id;
  const auto audit_result = http_use_cases.list_audits(filter);
  require(audit_result.ok, "list audits should succeed");
  require(!audit_result.data.items.empty(), "audit should contain push record");
  require(
      audit_result.data.items.front().action == "control_command_push",
      "audit action should be control_command_push");
}

void test_sqlite_store_repository() {
  const auto unique = std::to_string(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
  const auto db_path = std::filesystem::path("/tmp") / ("owt-net-test-" + unique + ".db");
  std::filesystem::remove(db_path);

  ctrl::infrastructure::SqliteStore store(db_path.string());
  store.migrate();

  std::string error;

  ctrl::domain::AgentState agent;
  agent.agent = {"AA:00:00:00:70:01", "agent-70-01"};
  agent.online = true;
  agent.site_id = "lab-70";
  agent.version = "test";
  agent.capabilities = {"host_reboot"};
  agent.stats = nlohmann::json{{"status", "online"}};
  agent.registered_at_ms = 7000;
  agent.last_seen_at_ms = 7000;
  agent.last_heartbeat_at_ms = 7000;
  require(store.upsert(agent, error), "sqlite upsert agent should succeed");

  ctrl::domain::AgentState loaded_agent;
  require(store.get(agent.agent.mac, loaded_agent, error), "sqlite get agent should succeed");
  require(
      loaded_agent.agent.display_id == agent.agent.display_id,
      "sqlite loaded agent id mismatch");

  ctrl::domain::CommandSnapshot cmd;
  cmd.spec.command_id = "cmd-sqlite-1";
  cmd.spec.trace_id = "trc-sqlite-1";
  cmd.spec.kind = ctrl::domain::CommandKind::HostReboot;
  cmd.spec.payload = nlohmann::json{{"host", "10.0.0.70"}};
  cmd.spec.timeout_ms = 5000;
  cmd.spec.max_retry = 2;
  cmd.spec.expires_at_ms = 7000 + 60000;
  cmd.agent = agent.agent;
  cmd.state = ctrl::domain::CommandState::Created;
  cmd.retry_count = 0;
  cmd.next_retry_at_ms = 0;
  cmd.created_at_ms = 7000;
  cmd.updated_at_ms = 7000;
  require(store.upsert(cmd, error), "sqlite upsert command should succeed");

  ctrl::domain::CommandSnapshot loaded_cmd;
  require(store.get(cmd.spec.command_id, loaded_cmd, error), "sqlite get command should succeed");
  require(loaded_cmd.spec.trace_id == cmd.spec.trace_id, "sqlite loaded command trace mismatch");

  ctrl::domain::CommandEvent event;
  event.command_id = cmd.spec.command_id;
  event.type = "command_push_sent";
  event.state = ctrl::domain::CommandState::Dispatched;
  event.detail = nlohmann::json{{"source", "test"}};
  event.created_at_ms = 7010;
  require(store.append_event(event, error), "sqlite append event should succeed");

  std::vector<ctrl::domain::CommandEvent> events;
  require(store.list_events(cmd.spec.command_id, 100, events, error), "sqlite list events should succeed");
  require(events.size() == 1, "sqlite events size mismatch");
  require(events.front().type == "command_push_sent", "sqlite event type mismatch");

  nlohmann::json params = nlohmann::json{{"ssh", {{"host", "10.0.0.70"}}}};
  require(store.save(agent.agent.mac, params, 7020, error), "sqlite save params should succeed");
  nlohmann::json loaded_params;
  require(store.load(agent.agent.mac, loaded_params, error), "sqlite load params should succeed");
  require(
      loaded_params["ssh"]["host"].get<std::string>() == "10.0.0.70",
      "sqlite loaded params mismatch");

  ctrl::domain::AuditEntry audit;
  audit.actor_type = "test";
  audit.actor_id = "tester";
  audit.action = "control_command_push";
  audit.resource_type = "command";
  audit.resource_id = cmd.spec.command_id;
  audit.summary = nlohmann::json{{"k", "v"}};
  audit.created_at_ms = 7030;
  require(store.append(audit, error), "sqlite append audit should succeed");
  ctrl::domain::AuditListFilter filter;
  auto audits = ctrl::domain::ListPage<ctrl::domain::AuditEntry, ctrl::domain::AuditListCursor>{};
  require(store.list(filter, audits, error), "sqlite list audits should succeed");
  require(!audits.items.empty(), "sqlite audits should not be empty");

  int recovered = 0;
  require(store.recover_inflight(8000, recovered, error), "sqlite recover inflight should succeed");
  require(recovered == 1, "sqlite recovered count mismatch");

  store.cleanup_retention(1, 8000 + 2LL * 24LL * 60LL * 60LL * 1000LL);

  std::filesystem::remove(db_path);
}

void test_sqlite_store_legacy_backup_and_rebuild() {
  const auto unique = std::to_string(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
  const auto db_path = std::filesystem::path("/tmp") / ("owt-net-legacy-" + unique + ".db");
  std::filesystem::remove(db_path);

  sqlite3* legacy = nullptr;
  require(
      sqlite3_open_v2(
          db_path.string().c_str(),
          &legacy,
          SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
          nullptr) == SQLITE_OK,
      "open legacy sqlite should succeed");
  sqlite_exec(
      legacy,
      "CREATE TABLE commands(command_id TEXT PRIMARY KEY,state TEXT NOT NULL);",
      "create legacy commands table");
  sqlite_exec(
      legacy,
      "INSERT INTO commands(command_id,state) VALUES('legacy-cmd-1','DISPATCHED');",
      "insert legacy row");
  sqlite3_close(legacy);
  legacy = nullptr;

  ctrl::infrastructure::SqliteStore store(db_path.string());
  store.migrate();

  const auto parent = db_path.parent_path();
  const auto base_name = db_path.filename().string() + ".bak.";
  std::vector<std::filesystem::path> backups;
  for (const auto& entry : std::filesystem::directory_iterator(parent)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    const auto file_name = entry.path().filename().string();
    if (file_name.rfind(base_name, 0) == 0) {
      backups.push_back(entry.path());
    }
  }
  require(!backups.empty(), "legacy sqlite backup file should be created");

  sqlite3* rebuilt = nullptr;
  require(
      sqlite3_open_v2(
          db_path.string().c_str(),
          &rebuilt,
          SQLITE_OPEN_READWRITE,
          nullptr) == SQLITE_OK,
      "open rebuilt sqlite should succeed");

  sqlite3_stmt* stmt = nullptr;
  require(
      sqlite3_prepare_v2(
          rebuilt,
          "SELECT value FROM schema_meta WHERE key='schema_version' LIMIT 1;",
          -1,
          &stmt,
          nullptr) == SQLITE_OK,
      "prepare schema version query should succeed");
  require(sqlite3_step(stmt) == SQLITE_ROW, "schema_version row should exist");
  require(
      std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0))) == "3",
      "schema_version should be 3");
  sqlite3_finalize(stmt);
  stmt = nullptr;

  require(
      sqlite3_prepare_v2(rebuilt, "SELECT COUNT(*) FROM commands;", -1, &stmt, nullptr) == SQLITE_OK,
      "prepare commands count query should succeed");
  require(sqlite3_step(stmt) == SQLITE_ROW, "commands count query should return row");
  require(sqlite3_column_int(stmt, 0) == 0, "rebuilt commands table should be empty");
  sqlite3_finalize(stmt);
  sqlite3_close(rebuilt);

  std::filesystem::remove(db_path);
  for (const auto& backup : backups) {
    std::filesystem::remove(backup);
  }
}

} // namespace

int main() {
  try {
    test_model_types_snake_case_mapping();
    test_agent_envelope_v3_codec();
    test_jsonrpc_request_validation();
    test_command_orchestrator_submit();
    test_agent_message_terminal_once();
    test_retry_service();
    test_params_rate_limiter_redaction();
    test_ws_frontend_http_adapters();
    test_command_e2e_submit_ack_terminal_audit_query();
    test_sqlite_store_repository();
    test_sqlite_store_legacy_backup_and_rebuild();
    std::cout << "owt-ctrl tests passed\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "owt-ctrl tests failed: " << ex.what() << '\n';
    return 1;
  }
}
