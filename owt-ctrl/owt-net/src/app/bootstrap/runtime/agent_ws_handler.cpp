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
        owt::protocol::v4::agent::kTypeServerError,
        state_.next_trace_id(),
        "",
        nlohmann::json{
            {"code", std::string(owt::protocol::v4::error_code::kBadMessageFormat)},
            {"message", parse_error},
        });
    return;
  }

  if (envelope.meta.protocol != kProtocolVersion) {
    state_.send_agent_envelope(
        hub,
        message.session_id,
        owt::protocol::v4::agent::kTypeServerError,
        envelope.meta.trace_id,
        envelope.meta.agent_id,
        nlohmann::json{
            {"code", std::string(owt::protocol::v4::error_code::kUnsupportedProtocol)},
            {"message", "unsupported protocol"},
            {"protocol", envelope.meta.protocol},
            {"expected", std::string(kProtocolVersion)},
        });
    return;
  }

  ctrl::adapters::WsInboundMessage in;
  in.session_token = message.session_id;
  in.trace_id = envelope.meta.trace_id;
  in.agent_id = envelope.meta.agent_id;

  const auto session_agent = state_.agent_channel.find_agent_for_session(message.session_id);

  const auto send_bad_message = [&](std::string_view text) {
    state_.send_agent_envelope(
        hub,
        message.session_id,
        owt::protocol::v4::agent::kTypeServerError,
        in.trace_id.empty() ? state_.next_trace_id() : in.trace_id,
        in.agent_id,
        nlohmann::json{
            {"code", std::string(owt::protocol::v4::error_code::kBadMessageFormat)},
            {"message", std::string(text)},
        });
  };

  if (envelope.type == owt::protocol::v4::agent::kTypeAgentRegister) {
    if (!envelope.data.contains("agent_mac") || !envelope.data["agent_mac"].is_string() ||
        envelope.data["agent_mac"].get<std::string>().empty()) {
      send_bad_message("agent_mac is required");
      return;
    }
    in.kind = ctrl::adapters::WsMessageKind::Register;
    in.agent_mac = envelope.data["agent_mac"].get<std::string>();
    if (in.agent_id.empty()) {
      in.agent_id = envelope.data.value("agent_id", std::string{});
    }
    in.payload = envelope.data;
    if (!in.agent_mac.empty()) {
      state_.agent_channel.bind_session(in.agent_mac, in.agent_id, in.session_token);
    }
  } else if (envelope.type == owt::protocol::v4::agent::kTypeAgentHeartbeat) {
    in.kind = ctrl::adapters::WsMessageKind::Heartbeat;
    in.agent_mac = envelope.data.value("agent_mac", session_agent);
    if (in.agent_mac.empty()) {
      send_bad_message("agent_mac is required");
      return;
    }
    in.payload = nlohmann::json{
        {"heartbeat_at_ms", envelope.data.value("heartbeat_at_ms", static_cast<int64_t>(0))},
        {"stats", envelope.data.value("stats", nlohmann::json::object())},
    };
  } else if (envelope.type == owt::protocol::v4::agent::kTypeAgentCommandAck) {
    in.kind = ctrl::adapters::WsMessageKind::CommandAck;
    in.agent_mac = envelope.data.value("agent_mac", session_agent);
    if (in.agent_mac.empty() || !envelope.data.contains("command_id") ||
        !envelope.data["command_id"].is_string() ||
        envelope.data["command_id"].get<std::string>().empty() ||
        !envelope.data.contains("status") || !envelope.data["status"].is_string()) {
      send_bad_message("invalid agent.command.ack data");
      return;
    }

    in.command_id = envelope.data["command_id"].get<std::string>();
    ctrl::domain::CommandState state = ctrl::domain::CommandState::Acked;
    if (!ctrl::domain::try_parse_command_state(envelope.data["status"].get<std::string>(), state)) {
      send_bad_message("invalid command status");
      return;
    }

    in.command_state = state;
    in.payload = nlohmann::json{{"message", envelope.data.value("message", std::string{})}};
  } else if (envelope.type == owt::protocol::v4::agent::kTypeAgentCommandResult) {
    in.kind = ctrl::adapters::WsMessageKind::CommandResult;
    in.agent_mac = envelope.data.value("agent_mac", session_agent);
    if (in.agent_mac.empty() || !envelope.data.contains("command_id") ||
        !envelope.data["command_id"].is_string() ||
        envelope.data["command_id"].get<std::string>().empty() ||
        !envelope.data.contains("final_status") || !envelope.data["final_status"].is_string() ||
        !envelope.data.contains("exit_code") || !envelope.data["exit_code"].is_number_integer() ||
        !envelope.data.contains("result") || !envelope.data["result"].is_object()) {
      send_bad_message("invalid agent.command.result data");
      return;
    }

    in.command_id = envelope.data["command_id"].get<std::string>();
    ctrl::domain::CommandState final_state = ctrl::domain::CommandState::Failed;
    if (!ctrl::domain::try_parse_command_state(
            envelope.data["final_status"].get<std::string>(), final_state)) {
      send_bad_message("invalid final_status");
      return;
    }

    in.command_state = final_state;
    in.exit_code = envelope.data["exit_code"].get<int>();
    in.payload = envelope.data["result"];
  } else {
    in.kind = ctrl::adapters::WsMessageKind::Unknown;
    in.payload = envelope.data;
  }

  std::vector<ctrl::adapters::WsOutboundMessage> out;
  try {
    state_.control_ws_use_cases.on_text(in, out);
  } catch (const std::exception& ex) {
    log::warn("control ws handler failed: {}", ex.what());
    out.push_back(ctrl::adapters::WsOutboundMessage{
        in.session_token,
        std::string(owt::protocol::v4::agent::kTypeServerError),
        in.trace_id,
        in.agent_id,
        nlohmann::json{
            {"code", std::string(owt::protocol::v4::error_code::kInternalError)},
            {"message", "control handler failed"},
        },
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
