#include "ctrl/ports/defaults.h"

#include <chrono>
#include <string>

namespace ctrl::ports {

int64_t SystemClock::now_ms() const {
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

std::string DefaultIdGenerator::next_command_id() {
  const auto now = static_cast<uint64_t>(SystemClock().now_ms());
  const auto seq = sequence_.fetch_add(1, std::memory_order_relaxed);
  return "cmd-" + std::to_string(now) + "-" + std::to_string(seq);
}

std::string DefaultIdGenerator::next_trace_id(std::string_view command_id) {
  if (command_id.empty()) {
    return "trc-" + next_command_id();
  }
  return "trc-" + std::string(command_id);
}

void NullStatusPublisher::publish_snapshot(std::string_view reason, std::string_view agent_mac) {
  (void)reason;
  (void)agent_mac;
}

void NullStatusPublisher::publish_agent(std::string_view reason, std::string_view agent_mac) {
  (void)reason;
  (void)agent_mac;
}

void NullMetrics::record_rate_limited(std::string_view actor_id, int64_t retry_after_ms) {
  (void)actor_id;
  (void)retry_after_ms;
}

void NullMetrics::record_command_push() {}

void NullMetrics::record_command_retry(
    std::string_view command_id,
    int retry_count,
    std::string_view reason) {
  (void)command_id;
  (void)retry_count;
  (void)reason;
}

void NullMetrics::record_command_retry_exhausted(
    std::string_view command_id,
    std::string_view reason) {
  (void)command_id;
  (void)reason;
}

void NullMetrics::record_command_terminal_status(
    std::string_view command_id,
    domain::CommandState state,
    const nlohmann::json& detail) {
  (void)command_id;
  (void)state;
  (void)detail;
}

} // namespace ctrl::ports
