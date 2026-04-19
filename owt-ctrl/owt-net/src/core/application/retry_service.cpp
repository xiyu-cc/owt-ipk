#include "ctrl/application/retry_service.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

namespace ctrl::application {

RetryService::RetryService(
    ports::ICommandRepository& commands,
    ports::IAgentChannel& channel,
    ports::IStatusPublisher& publisher,
    ports::IMetrics& metrics,
    const ports::IClock& clock)
    : commands_(commands),
      channel_(channel),
      publisher_(publisher),
      metrics_(metrics),
      clock_(clock) {}

void RetryService::tick_once(int limit) {
  const auto now_ms = clock_.now_ms();
  std::vector<domain::CommandSnapshot> due;
  std::string error;
  if (!commands_.list_retry_ready(now_ms, limit, due, error)) {
    throw std::runtime_error("list retry-ready commands failed: " + error);
  }
  for (const auto& row : due) {
    process_retry_command(row, now_ms);
  }
}

void RetryService::recover_inflight_on_boot() {
  int recovered_count = 0;
  std::string error;
  if (!commands_.recover_inflight(clock_.now_ms(), recovered_count, error)) {
    throw std::runtime_error("recover inflight commands failed: " + error);
  }
}

int64_t RetryService::compute_retry_delay_ms(int retry_count) {
  const int bounded = std::max(0, std::min(retry_count - 1, 5));
  const int64_t delay = 1000LL * (1LL << bounded);
  return std::min<int64_t>(delay, 30000LL);
}

void RetryService::process_retry_command(const domain::CommandSnapshot& row, int64_t now_ms) {
  if (row.spec.expires_at_ms > 0 && now_ms >= row.spec.expires_at_ms) {
    bool applied = false;
    std::string error;
    const auto result = nlohmann::json{
        {"error_code", "command_expired"},
        {"message", "command expired before retry dispatch"},
        {"expires_at_ms", row.spec.expires_at_ms},
        {"checked_at_ms", now_ms},
    };
    if (!commands_.update_terminal_state_once(
            row.spec.command_id,
            domain::CommandState::TimedOut,
            result,
            now_ms,
            applied,
            error)) {
      throw std::runtime_error("mark command retry expired failed: " + error);
    }
    if (applied) {
      auto snapshot = row;
      snapshot.state = domain::CommandState::TimedOut;
      snapshot.result = result;
      snapshot.updated_at_ms = now_ms;
      const auto event = append_event(
          row.spec.command_id,
          "command_retry_expired",
          domain::CommandState::TimedOut,
          nlohmann::json{
              {"retry_count", row.retry_count},
              {"max_retry", row.spec.max_retry},
              {"trace_id", row.spec.trace_id},
          },
          now_ms);
      publisher_.publish_command_event(snapshot, event);
      metrics_.record_command_terminal_status(
          row.spec.command_id,
          domain::CommandState::TimedOut,
          result);
      publisher_.publish_snapshot("command_retry_expired", row.agent.mac);
    }
    return;
  }

  std::string dispatch_error;
  const bool online = channel_.is_online(row.agent.mac);
  const bool sent = online && channel_.send_command(row.agent.mac, row.spec, dispatch_error);
  if (sent) {
    bool applied = false;
    std::string error;
    if (!commands_.update_state_if_not_terminal(
            row.spec.command_id,
            domain::CommandState::Dispatched,
            nlohmann::json::object(),
            now_ms,
            applied,
            error)) {
      throw std::runtime_error("mark retry dispatched failed: " + error);
    }
    if (applied) {
      auto snapshot = row;
      snapshot.state = domain::CommandState::Dispatched;
      snapshot.updated_at_ms = now_ms;
      const auto event = append_event(
          row.spec.command_id,
          "command_retry_dispatched",
          domain::CommandState::Dispatched,
          nlohmann::json{
              {"retry_count", row.retry_count},
              {"max_retry", row.spec.max_retry},
              {"trace_id", row.spec.trace_id},
          },
          now_ms);
      publisher_.publish_command_event(snapshot, event);
      publisher_.publish_snapshot("command_retry_dispatched", row.agent.mac);
    }
    return;
  }

  if (dispatch_error.empty()) {
    dispatch_error = online ? "send failed" : "agent not connected";
  }

  const int next_retry_count = row.retry_count + 1;
  if (next_retry_count >= row.spec.max_retry) {
    bool applied = false;
    std::string error;
    const auto result = nlohmann::json{
        {"error_code", "DISPATCH_RETRY_EXHAUSTED"},
        {"message", dispatch_error},
        {"retry_count", next_retry_count},
        {"max_retry", row.spec.max_retry},
    };
    if (!commands_.update_terminal_state_once(
            row.spec.command_id,
            domain::CommandState::Failed,
            result,
            now_ms,
            applied,
            error)) {
      throw std::runtime_error("mark retry exhausted failed: " + error);
    }
    if (applied) {
      auto snapshot = row;
      snapshot.state = domain::CommandState::Failed;
      snapshot.result = result;
      snapshot.updated_at_ms = now_ms;
      const auto event = append_event(
          row.spec.command_id,
          "command_retry_exhausted",
          domain::CommandState::Failed,
          nlohmann::json{
              {"retry_count", next_retry_count},
              {"max_retry", row.spec.max_retry},
              {"error", dispatch_error},
              {"trace_id", row.spec.trace_id},
          },
          now_ms);
      publisher_.publish_command_event(snapshot, event);
      metrics_.record_command_retry_exhausted(row.spec.command_id, dispatch_error);
      metrics_.record_command_terminal_status(row.spec.command_id, domain::CommandState::Failed, result);
      publisher_.publish_snapshot("command_retry_exhausted", row.agent.mac);
    }
    return;
  }

  const int64_t next_retry_at_ms = now_ms + compute_retry_delay_ms(next_retry_count);
  std::string error;
  if (!commands_.update_retry_state(
          row.spec.command_id,
          domain::CommandState::RetryPending,
          next_retry_count,
          next_retry_at_ms,
          dispatch_error,
          now_ms,
          error)) {
    throw std::runtime_error("update retry state failed: " + error);
  }
  auto snapshot = row;
  snapshot.state = domain::CommandState::RetryPending;
  snapshot.retry_count = next_retry_count;
  snapshot.next_retry_at_ms = next_retry_at_ms;
  snapshot.last_error = dispatch_error;
  snapshot.updated_at_ms = now_ms;
  const auto event = append_event(
      row.spec.command_id,
      "command_retry_scheduled",
      domain::CommandState::RetryPending,
      nlohmann::json{
          {"retry_count", next_retry_count},
          {"max_retry", row.spec.max_retry},
          {"next_retry_at_ms", next_retry_at_ms},
          {"error", dispatch_error},
          {"trace_id", row.spec.trace_id},
      },
      now_ms);
  publisher_.publish_command_event(snapshot, event);
  metrics_.record_command_retry(row.spec.command_id, next_retry_count, dispatch_error);
}

domain::CommandEvent RetryService::append_event(
    std::string_view command_id,
    std::string_view event_type,
    domain::CommandState state,
    const nlohmann::json& detail,
    int64_t created_at_ms) {
  domain::CommandEvent event;
  event.command_id = std::string(command_id);
  event.type = std::string(event_type);
  event.state = state;
  event.detail = detail;
  event.created_at_ms = created_at_ms;
  std::string error;
  if (!commands_.append_event(event, error)) {
    throw std::runtime_error("append retry event failed: " + error);
  }
  return event;
}

} // namespace ctrl::application
