#include "internal.h"

namespace app::bootstrap::runtime_internal {

void AppMetrics::record_http_request() {
  ++http_requests_total_;
}

void AppMetrics::record_rate_limited(std::string_view actor_id, int64_t retry_after_ms) {
  (void)actor_id;
  (void)retry_after_ms;
  ++rate_limited_total_;
}

void AppMetrics::record_command_push() {
  ++command_push_total_;
}

void AppMetrics::record_command_retry(
    std::string_view command_id,
    int retry_count,
    std::string_view reason) {
  (void)command_id;
  (void)retry_count;
  (void)reason;
  ++command_retry_total_;
}

void AppMetrics::record_command_retry_exhausted(
    std::string_view command_id,
    std::string_view reason) {
  (void)command_id;
  (void)reason;
  ++command_retry_exhausted_total_;
}

void AppMetrics::record_command_terminal_status(
    std::string_view command_id,
    ctrl::domain::CommandState state,
    const nlohmann::json& detail) {
  (void)command_id;
  (void)detail;
  switch (state) {
    case ctrl::domain::CommandState::Succeeded:
      ++command_succeeded_total_;
      break;
    case ctrl::domain::CommandState::Failed:
      ++command_failed_total_;
      break;
    case ctrl::domain::CommandState::TimedOut:
      ++command_timed_out_total_;
      break;
    default:
      break;
  }
}

} // namespace app::bootstrap::runtime_internal
