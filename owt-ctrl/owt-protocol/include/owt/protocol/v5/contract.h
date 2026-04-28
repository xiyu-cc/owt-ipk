#pragma once

#include <string_view>

namespace owt::protocol::v5 {

inline constexpr std::string_view kProtocol = "v5";

inline constexpr std::string_view kWsRouteAgent = "/ws/v5/agent";
inline constexpr std::string_view kWsRouteUi = "/ws/v5/ui";

namespace kind {
inline constexpr std::string_view kAction = "action";
inline constexpr std::string_view kResult = "result";
inline constexpr std::string_view kEvent = "event";
inline constexpr std::string_view kError = "error";
} // namespace kind

namespace ui {

inline constexpr std::string_view kActionSessionSubscribe = "session.subscribe";
inline constexpr std::string_view kActionParamsGet = "params.get";
inline constexpr std::string_view kActionParamsUpdate = "params.update";
inline constexpr std::string_view kActionCommandSubmit = "command.submit";

inline constexpr std::string_view kEventAgentSnapshot = "agent.snapshot";
inline constexpr std::string_view kEventAgentUpdate = "agent.update";
inline constexpr std::string_view kEventCommandEvent = "command.event";

} // namespace ui

namespace agent {

inline constexpr std::string_view kActionAgentRegister = "agent.register";
inline constexpr std::string_view kActionAgentHeartbeat = "agent.heartbeat";
inline constexpr std::string_view kActionCommandAck = "command.ack";
inline constexpr std::string_view kActionCommandResult = "command.result";

inline constexpr std::string_view kEventAgentRegistered = "agent.registered";
inline constexpr std::string_view kEventCommandDispatch = "command.dispatch";
inline constexpr std::string_view kErrorServerError = "server.error";

inline bool is_known_action_payload_field(
    std::string_view action_name,
    std::string_view field_name) noexcept {
  if (action_name == kActionAgentRegister) {
    return field_name == "agent_mac" || field_name == "agent_id" ||
        field_name == "site_id" || field_name == "agent_version" ||
        field_name == "capabilities";
  }
  if (action_name == kActionAgentHeartbeat) {
    return field_name == "agent_mac" || field_name == "heartbeat_at_ms" ||
        field_name == "stats";
  }
  if (action_name == kActionCommandAck) {
    return field_name == "agent_mac" || field_name == "command_id" ||
        field_name == "status" || field_name == "message";
  }
  if (action_name == kActionCommandResult) {
    return field_name == "agent_mac" || field_name == "command_id" ||
        field_name == "final_status" || field_name == "exit_code" ||
        field_name == "result";
  }
  return false;
}

} // namespace agent

namespace command {

namespace type {

inline constexpr std::string_view kWolWake = "wol_wake";
inline constexpr std::string_view kHostReboot = "host_reboot";
inline constexpr std::string_view kHostPoweroff = "host_poweroff";
inline constexpr std::string_view kMonitoringSet = "monitoring_set";
inline constexpr std::string_view kParamsSet = "params_set";

} // namespace type

namespace state {

inline constexpr std::string_view kCreated = "created";
inline constexpr std::string_view kDispatched = "dispatched";
inline constexpr std::string_view kAcked = "acked";
inline constexpr std::string_view kRunning = "running";
inline constexpr std::string_view kRetryPending = "retry_pending";
inline constexpr std::string_view kSucceeded = "succeeded";
inline constexpr std::string_view kFailed = "failed";
inline constexpr std::string_view kTimedOut = "timed_out";
inline constexpr std::string_view kCancelled = "cancelled";

} // namespace state

} // namespace command

namespace json {

inline constexpr std::string_view kVersion = "v";
inline constexpr std::string_view kKind = "kind";
inline constexpr std::string_view kName = "name";
inline constexpr std::string_view kId = "id";
inline constexpr std::string_view kTsMs = "ts_ms";
inline constexpr std::string_view kPayload = "payload";
inline constexpr std::string_view kTarget = "target";

} // namespace json

namespace error_code {

inline constexpr std::string_view kBadEnvelope = "bad_envelope";
inline constexpr std::string_view kUnsupportedVersion = "unsupported_version";
inline constexpr std::string_view kBadKind = "bad_kind";
inline constexpr std::string_view kMethodNotFound = "method_not_found";
inline constexpr std::string_view kInvalidParams = "invalid_params";
inline constexpr std::string_view kRateLimited = "rate_limited";
inline constexpr std::string_view kInternalError = "internal_error";
inline constexpr std::string_view kNotFound = "not_found";

} // namespace error_code

} // namespace owt::protocol::v5
