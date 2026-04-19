#pragma once

#include <string_view>

namespace owt::protocol::v4 {

inline constexpr std::string_view kProtocol = "v4";

inline constexpr std::string_view kWsRouteAgent = "/ws/v4/agent";
inline constexpr std::string_view kWsRouteUi = "/ws/v4/ui";

namespace ui {

inline constexpr std::string_view kMethodSessionSubscribe = "session.subscribe";
inline constexpr std::string_view kMethodSessionUnsubscribe = "session.unsubscribe";
inline constexpr std::string_view kMethodAgentList = "agent.list";
inline constexpr std::string_view kMethodAgentGet = "agent.get";
inline constexpr std::string_view kMethodParamsGet = "params.get";
inline constexpr std::string_view kMethodParamsUpdate = "params.update";
inline constexpr std::string_view kMethodCommandSubmit = "command.submit";
inline constexpr std::string_view kMethodCommandGet = "command.get";
inline constexpr std::string_view kMethodCommandList = "command.list";
inline constexpr std::string_view kMethodAuditList = "audit.list";

inline constexpr std::string_view kEventAgentSnapshot = "agent.snapshot";
inline constexpr std::string_view kEventAgentUpdate = "agent.update";
inline constexpr std::string_view kEventCommandEvent = "command.event";

} // namespace ui

namespace agent {

inline constexpr std::string_view kTypeAgentRegister = "agent.register";
inline constexpr std::string_view kTypeAgentHeartbeat = "agent.heartbeat";
inline constexpr std::string_view kTypeAgentCommandAck = "agent.command.ack";
inline constexpr std::string_view kTypeAgentCommandResult = "agent.command.result";

inline constexpr std::string_view kTypeServerRegisterAck = "server.register.ack";
inline constexpr std::string_view kTypeServerCommandDispatch = "server.command.dispatch";
inline constexpr std::string_view kTypeServerError = "server.error";

} // namespace agent

namespace json {

inline constexpr std::string_view kEnvelopeType = "type";
inline constexpr std::string_view kEnvelopeMeta = "meta";
inline constexpr std::string_view kEnvelopeData = "data";
inline constexpr std::string_view kMetaProtocol = "protocol";
inline constexpr std::string_view kMetaTraceId = "trace_id";
inline constexpr std::string_view kMetaTsMs = "ts_ms";
inline constexpr std::string_view kMetaAgentId = "agent_id";

} // namespace json

namespace error_code {

inline constexpr std::string_view kBadMessageFormat = "bad_message_format";
inline constexpr std::string_view kBadMessageType = "bad_message_type";
inline constexpr std::string_view kUnsupportedProtocol = "unsupported_protocol";
inline constexpr std::string_view kRateLimited = "rate_limited";
inline constexpr std::string_view kInternalError = "internal_error";

} // namespace error_code

} // namespace owt::protocol::v4
