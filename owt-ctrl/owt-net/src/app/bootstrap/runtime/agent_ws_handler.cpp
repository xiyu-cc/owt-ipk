#include "internal.h"

#include "detail/runtime/log.h"

#include <exception>

namespace app::bootstrap::runtime_internal {

AgentWsHandler::AgentWsHandler(RuntimeImplState& state) : state_(state) {}

void AgentWsHandler::on_join(ws_deal::ws_hub_api& hub, std::string_view session_id) {
  state_.status_publisher.set_hub(&hub);
  state_.agent_channel.set_hub(&hub);
  state_.control_ws_use_cases.on_open(session_id);
}

void AgentWsHandler::on_leave(ws_deal::ws_hub_api& hub, std::string_view session_id) {
  (void)hub;
  state_.control_ws_use_cases.on_close(session_id);
  state_.agent_channel.unbind_session(session_id);
}

void AgentWsHandler::on_message(ws_deal::ws_hub_api& hub, ws_deal::inbound_message message) {
  state_.status_publisher.set_hub(&hub);
  state_.agent_channel.set_hub(&hub);
  if (!message.text) {
    return;
  }

  ws::AgentEnvelope envelope;
  std::string parse_error;
  if (!ws::parse_agent_envelope(message.payload, envelope, parse_error)) {
    state_.send_agent_envelope(
        hub,
        message.session_id,
        "error",
        state_.next_trace_id(),
        "",
        nlohmann::json{{"code", "bad_message_format"}, {"message", parse_error}});
    return;
  }

  if (envelope.meta.version != kProtocolVersion) {
    state_.send_agent_envelope(
        hub,
        message.session_id,
        "error",
        envelope.meta.trace_id,
        envelope.meta.agent_id,
        nlohmann::json{{"code", "unsupported_protocol_version"}, {"message", "unsupported version"}});
    return;
  }

  ctrl::adapters::WsInboundMessage in;
  in.session_token = message.session_id;
  in.trace_id = envelope.meta.trace_id;
  in.agent_id = envelope.meta.agent_id;

  const auto session_agent = state_.agent_channel.find_agent_for_session(message.session_id);

  if (envelope.type == "register") {
    if (!envelope.payload.contains("agent_mac") || !envelope.payload["agent_mac"].is_string() ||
        envelope.payload["agent_mac"].get<std::string>().empty()) {
      state_.send_agent_envelope(
          hub,
          message.session_id,
          "error",
          in.trace_id,
          in.agent_id,
          nlohmann::json{{"code", "bad_message_format"}, {"message", "agent_mac is required"}});
      return;
    }
    in.kind = ctrl::adapters::WsMessageKind::Register;
    in.agent_mac = envelope.payload["agent_mac"].get<std::string>();
    if (in.agent_id.empty()) {
      in.agent_id = envelope.payload.value("agent_id", std::string{});
    }
    in.payload = envelope.payload;
    if (!in.agent_mac.empty()) {
      state_.agent_channel.bind_session(in.agent_mac, in.agent_id, in.session_token);
    }
  } else if (envelope.type == "heartbeat") {
    in.kind = ctrl::adapters::WsMessageKind::Heartbeat;
    in.agent_mac = envelope.payload.value("agent_mac", session_agent);
    if (in.agent_mac.empty()) {
      state_.send_agent_envelope(
          hub,
          message.session_id,
          "error",
          in.trace_id,
          in.agent_id,
          nlohmann::json{{"code", "bad_message_format"}, {"message", "agent_mac is required"}});
      return;
    }
    in.payload = nlohmann::json{
        {"heartbeat_at_ms", envelope.payload.value("heartbeat_at_ms", static_cast<int64_t>(0))},
        {"stats", envelope.payload.value("stats", nlohmann::json::object())},
    };
  } else if (envelope.type == "command_ack") {
    in.kind = ctrl::adapters::WsMessageKind::CommandAck;
    in.agent_mac = envelope.payload.value("agent_mac", session_agent);
    if (in.agent_mac.empty() || !envelope.payload.contains("command_id") ||
        !envelope.payload["command_id"].is_string() || envelope.payload["command_id"].get<std::string>().empty() ||
        !envelope.payload.contains("status") || !envelope.payload["status"].is_string()) {
      state_.send_agent_envelope(
          hub,
          message.session_id,
          "error",
          in.trace_id,
          in.agent_id,
          nlohmann::json{{"code", "bad_message_format"}, {"message", "invalid command_ack payload"}});
      return;
    }
    in.command_id = envelope.payload["command_id"].get<std::string>();
    ctrl::domain::CommandState state = ctrl::domain::CommandState::Acked;
    if (!ctrl::domain::try_parse_command_state(envelope.payload["status"].get<std::string>(), state)) {
      state_.send_agent_envelope(
          hub,
          message.session_id,
          "error",
          in.trace_id,
          in.agent_id,
          nlohmann::json{{"code", "bad_message_format"}, {"message", "invalid status"}});
      return;
    }
    in.command_state = state;
    in.payload = nlohmann::json{{"message", envelope.payload.value("message", std::string{})}};
  } else if (envelope.type == "command_result") {
    in.kind = ctrl::adapters::WsMessageKind::CommandResult;
    in.agent_mac = envelope.payload.value("agent_mac", session_agent);
    if (in.agent_mac.empty() || !envelope.payload.contains("command_id") ||
        !envelope.payload["command_id"].is_string() || envelope.payload["command_id"].get<std::string>().empty() ||
        !envelope.payload.contains("final_status") || !envelope.payload["final_status"].is_string() ||
        !envelope.payload.contains("exit_code") || !envelope.payload["exit_code"].is_number_integer() ||
        !envelope.payload.contains("result") || !envelope.payload["result"].is_object()) {
      state_.send_agent_envelope(
          hub,
          message.session_id,
          "error",
          in.trace_id,
          in.agent_id,
          nlohmann::json{{"code", "bad_message_format"}, {"message", "invalid command_result payload"}});
      return;
    }
    in.command_id = envelope.payload["command_id"].get<std::string>();
    ctrl::domain::CommandState final_state = ctrl::domain::CommandState::Failed;
    if (!ctrl::domain::try_parse_command_state(envelope.payload["final_status"].get<std::string>(), final_state)) {
      state_.send_agent_envelope(
          hub,
          message.session_id,
          "error",
          in.trace_id,
          in.agent_id,
          nlohmann::json{{"code", "bad_message_format"}, {"message", "invalid final_status"}});
      return;
    }
    in.command_state = final_state;
    in.exit_code = envelope.payload["exit_code"].get<int>();
    in.payload = envelope.payload["result"];
  } else {
    in.kind = ctrl::adapters::WsMessageKind::Unknown;
    in.payload = envelope.payload;
  }

  std::vector<ctrl::adapters::WsOutboundMessage> out;
  try {
    state_.control_ws_use_cases.on_text(in, out);
  } catch (const std::exception& ex) {
    log::warn("control ws handler failed: {}", ex.what());
    out.push_back(ctrl::adapters::WsOutboundMessage{
        in.session_token,
        "error",
        in.trace_id,
        in.agent_id,
        nlohmann::json{{"code", "internal_error"}, {"message", "control handler failed"}},
    });
  }

  for (const auto& row : out) {
    state_.send_agent_envelope(
        hub,
        row.session_token,
        row.type,
        row.trace_id,
        row.agent_id,
        row.payload);
  }
}

} // namespace app::bootstrap::runtime_internal
