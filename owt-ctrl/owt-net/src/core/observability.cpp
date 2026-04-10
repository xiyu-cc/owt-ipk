#include "service/observability.h"

#include "control/control_protocol.h"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

namespace service {

namespace {

constexpr std::size_t kMaxAlerts = 500;

std::atomic<uint64_t> g_http_requests_total{0};
std::atomic<uint64_t> g_auth_failed_total{0};
std::atomic<uint64_t> g_rate_limited_total{0};
std::atomic<uint64_t> g_command_push_total{0};
std::atomic<uint64_t> g_command_retry_total{0};
std::atomic<uint64_t> g_command_retry_exhausted_total{0};
std::atomic<uint64_t> g_command_succeeded_total{0};
std::atomic<uint64_t> g_command_failed_total{0};
std::atomic<uint64_t> g_command_timed_out_total{0};

std::mutex g_alerts_mutex;
std::vector<alert_record> g_alerts;
int64_t g_alert_seq = 0;

void push_alert_locked(alert_record alert) {
  if (g_alerts.size() >= kMaxAlerts) {
    g_alerts.erase(g_alerts.begin());
  }
  g_alerts.push_back(std::move(alert));
}

} // namespace

void record_http_request() {
  g_http_requests_total.fetch_add(1, std::memory_order_relaxed);
}

void record_auth_failed(const std::string& actor_id) {
  g_auth_failed_total.fetch_add(1, std::memory_order_relaxed);
  append_alert(
      "warn",
      "AUTH_FAILED",
      "unauthorized request rejected",
      std::string("{\"actor_id\":\"") + actor_id + "\"}");
}

void record_rate_limited(const std::string& actor_id, int64_t retry_after_ms) {
  g_rate_limited_total.fetch_add(1, std::memory_order_relaxed);
  append_alert(
      "warn",
      "RATE_LIMITED",
      "request rate limited",
      std::string("{\"actor_id\":\"") + actor_id +
          "\",\"retry_after_ms\":" + std::to_string(retry_after_ms) + "}");
}

void record_command_push() {
  g_command_push_total.fetch_add(1, std::memory_order_relaxed);
}

void record_command_retry(const std::string& command_id, int retry_count, const std::string& reason) {
  g_command_retry_total.fetch_add(1, std::memory_order_relaxed);
  append_alert(
      "info",
      "COMMAND_RETRY",
      "command scheduled for retry",
      std::string("{\"command_id\":\"") + command_id +
          "\",\"retry_count\":" + std::to_string(retry_count) +
          ",\"reason\":" + "\"" + reason + "\"}");
}

void record_command_retry_exhausted(const std::string& command_id, const std::string& reason) {
  g_command_retry_exhausted_total.fetch_add(1, std::memory_order_relaxed);
  append_alert(
      "error",
      "COMMAND_RETRY_EXHAUSTED",
      "command retry exhausted",
      std::string("{\"command_id\":\"") + command_id +
          "\",\"reason\":" + "\"" + reason + "\"}");
}

void record_command_terminal_status(
    const std::string& command_id,
    const std::string& status,
    const std::string& detail_json) {
  if (status == "SUCCEEDED") {
    g_command_succeeded_total.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  if (status == "FAILED") {
    g_command_failed_total.fetch_add(1, std::memory_order_relaxed);
    append_alert("warn", "COMMAND_FAILED", "command finished with failure", detail_json);
    return;
  }
  if (status == "TIMED_OUT") {
    g_command_timed_out_total.fetch_add(1, std::memory_order_relaxed);
    append_alert(
        "warn",
        "COMMAND_TIMED_OUT",
        "command finished with timeout",
        detail_json.empty() ? (std::string("{\"command_id\":\"") + command_id + "\"}") : detail_json);
  }
}

void append_alert(
    const std::string& level,
    const std::string& alert_type,
    const std::string& message,
    const std::string& detail_json) {
  std::lock_guard<std::mutex> lock(g_alerts_mutex);
  alert_record alert;
  alert.id = ++g_alert_seq;
  alert.level = level;
  alert.alert_type = alert_type;
  alert.message = message;
  alert.detail_json = detail_json;
  alert.created_at_ms = control::unix_time_ms_now();
  push_alert_locked(std::move(alert));
}

metrics_snapshot snapshot_metrics() {
  metrics_snapshot snapshot;
  snapshot.http_requests_total = g_http_requests_total.load(std::memory_order_relaxed);
  snapshot.auth_failed_total = g_auth_failed_total.load(std::memory_order_relaxed);
  snapshot.rate_limited_total = g_rate_limited_total.load(std::memory_order_relaxed);
  snapshot.command_push_total = g_command_push_total.load(std::memory_order_relaxed);
  snapshot.command_retry_total = g_command_retry_total.load(std::memory_order_relaxed);
  snapshot.command_retry_exhausted_total =
      g_command_retry_exhausted_total.load(std::memory_order_relaxed);
  snapshot.command_succeeded_total = g_command_succeeded_total.load(std::memory_order_relaxed);
  snapshot.command_failed_total = g_command_failed_total.load(std::memory_order_relaxed);
  snapshot.command_timed_out_total = g_command_timed_out_total.load(std::memory_order_relaxed);
  return snapshot;
}

void list_alerts(std::vector<alert_record>& out, int limit) {
  std::lock_guard<std::mutex> lock(g_alerts_mutex);
  const int safe_limit = std::max(1, std::min(limit, 500));
  out.clear();
  out.reserve(static_cast<std::size_t>(safe_limit));
  for (auto it = g_alerts.rbegin(); it != g_alerts.rend() && static_cast<int>(out.size()) < safe_limit;
       ++it) {
    out.push_back(*it);
  }
}

} // namespace service
