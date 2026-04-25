#include "ctrl/adapters/control_ws_use_cases.h"
#include "owt/protocol/v5/contract.h"

#include <stdexcept>
#include <string>

namespace ctrl::adapters {

ControlWsUseCases::ControlWsUseCases(
    application::AgentRegistryService& registry,
    application::AgentMessageService& messages)
    : registry_(registry), messages_(messages) {}

void ControlWsUseCases::on_open(std::string_view session_token) {
  if (session_token.empty()) {
    return;
  }
  std::lock_guard<std::mutex> lk(mutex_);
  session_agents_[std::string(session_token)] = "";
}

void ControlWsUseCases::on_text(const WsInboundMessage& in, std::vector<WsOutboundMessage>& out) {
  if (in.session_token.empty()) {
    throw std::invalid_argument("session_token is required");
  }

  switch (in.kind) {
    case WsMessageKind::Register: {
      if (in.agent_mac.empty()) {
        out.push_back(WsOutboundMessage{
            in.session_token,
            std::string(owt::protocol::v5::agent::kErrorServerError),
            in.trace_id,
            in.agent_id,
            nlohmann::json{
                {"code", std::string(owt::protocol::v5::error_code::kBadEnvelope)},
                {"message", "register payload missing agent_mac"},
            }});
        return;
      }
      {
        std::lock_guard<std::mutex> lk(mutex_);
        session_agents_[in.session_token] = in.agent_mac;
      }
      registry_.bind_session(in.agent_mac, in.session_token);
      messages_.on_agent_registered(in.agent_mac, in.agent_id, in.payload);
      out.push_back(WsOutboundMessage{
          in.session_token,
          std::string(owt::protocol::v5::agent::kEventAgentRegistered),
          in.trace_id,
          in.agent_id,
          nlohmann::json{{"ok", true}, {"message", "registered"}},
      });
      return;
    }
    case WsMessageKind::Heartbeat: {
      if (in.agent_mac.empty()) {
        return;
      }
      const int64_t heartbeat_at_ms =
          (in.payload.contains("heartbeat_at_ms") && in.payload["heartbeat_at_ms"].is_number_integer())
              ? in.payload["heartbeat_at_ms"].get<int64_t>()
              : 0;
      nlohmann::json stats = nlohmann::json::object();
      if (in.payload.contains("stats") && in.payload["stats"].is_object()) {
        stats = in.payload["stats"];
      }
      messages_.on_agent_heartbeat(in.agent_mac, stats, heartbeat_at_ms);
      return;
    }
    case WsMessageKind::CommandAck:
      messages_.on_command_ack(
          in.agent_mac,
          in.command_id,
          in.command_state,
          in.trace_id,
          in.payload.value("message", ""));
      return;
    case WsMessageKind::CommandResult:
      messages_.on_command_result(
          in.agent_mac,
          in.command_id,
          in.command_state,
          in.exit_code,
          (in.payload.contains("result") && in.payload["result"].is_object())
              ? in.payload["result"]
              : in.payload,
          in.trace_id);
      return;
    case WsMessageKind::Unknown:
      out.push_back(WsOutboundMessage{
          in.session_token,
          std::string(owt::protocol::v5::agent::kErrorServerError),
          in.trace_id,
          in.agent_id,
          nlohmann::json{
              {"code", std::string(owt::protocol::v5::error_code::kMethodNotFound)},
              {"message", "unsupported message type"},
          }});
      return;
  }
}

void ControlWsUseCases::on_close(std::string_view session_token) {
  if (session_token.empty()) {
    return;
  }
  std::string agent_mac;
  {
    std::lock_guard<std::mutex> lk(mutex_);
    const auto it = session_agents_.find(std::string(session_token));
    if (it == session_agents_.end()) {
      return;
    }
    agent_mac = it->second;
    session_agents_.erase(it);
  }
  if (!agent_mac.empty()) {
    registry_.on_disconnect(agent_mac, session_token);
  }
}

} // namespace ctrl::adapters
