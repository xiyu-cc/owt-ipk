#include "ctrl/adapters/control_ws_use_cases.h"

#include "app/runtime_log.h"
#include "owt/protocol/v5/contract.h"

#include <stdexcept>
#include <initializer_list>
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

  auto ensure_strict_action_session_binding = [&]() {
    std::lock_guard<std::mutex> lk(mutex_);
    const auto it = session_agents_.find(in.session_token);
    if (it == session_agents_.end() || it->second.empty()) {
      throw std::invalid_argument("agent.register is required before agent actions");
    }
    if (it->second != in.agent_mac) {
      throw std::invalid_argument("payload.agent_mac does not match registered session agent");
    }
  };

  auto ensure_payload_fields = [&](std::string_view action_name, std::initializer_list<std::string_view> allowed) {
    if (!in.payload.is_object()) {
      throw std::invalid_argument("payload must be object");
    }
    for (auto it = in.payload.begin(); it != in.payload.end(); ++it) {
      const auto key = it.key();
      bool allowed_key = false;
      for (const auto candidate : allowed) {
        if (key == candidate) {
          allowed_key = true;
          break;
        }
      }
      if (!allowed_key) {
        throw std::invalid_argument(
            "unknown field in " + std::string(action_name) + " payload: " + key);
      }
    }
  };

  switch (in.kind) {
    case WsMessageKind::Register: {
      ensure_payload_fields(
          owt::protocol::v5::agent::kActionAgentRegister,
          {"agent_mac", "agent_id", "site_id", "agent_version", "capabilities"});
      if (in.agent_mac.empty()) {
        log::warn(
            "reject register: missing agent_mac, session_token={}, trace_id={}",
            in.session_token,
            in.trace_id);
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
      std::string bind_error;
      if (!registry_.bind_session(in.agent_mac, in.session_token, &bind_error)) {
        log::warn(
            "bind agent session failed: agent_mac={}, session_token={}, error={}, persist_failure_count={}",
            in.agent_mac,
            in.session_token,
            bind_error.empty() ? "unknown" : bind_error,
            registry_.persist_failure_count());
      }
      messages_.on_agent_registered(in.agent_mac, in.agent_id, in.payload);
      log::info(
          "agent register accepted: agent_mac={}, agent_id={}, session_token={}, trace_id={}",
          in.agent_mac,
          in.agent_id.empty() ? in.agent_mac : in.agent_id,
          in.session_token,
          in.trace_id);
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
      ensure_payload_fields(
          owt::protocol::v5::agent::kActionAgentHeartbeat,
          {"agent_mac", "heartbeat_at_ms", "stats"});
      if (in.agent_mac.empty()) {
        log::warn(
            "ignore heartbeat: missing agent_mac, session_token={}, trace_id={}",
            in.session_token,
            in.trace_id);
        return;
      }
      ensure_strict_action_session_binding();
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
      ensure_payload_fields(
          owt::protocol::v5::agent::kActionCommandAck,
          {"agent_mac", "command_id", "status", "message"});
      ensure_strict_action_session_binding();
      messages_.on_command_ack(
          in.agent_mac,
          in.command_id,
          in.command_state,
          in.trace_id,
          in.payload.value("message", ""));
      return;
    case WsMessageKind::CommandResult:
      ensure_payload_fields(
          owt::protocol::v5::agent::kActionCommandResult,
          {"agent_mac", "command_id", "final_status", "exit_code", "result"});
      ensure_strict_action_session_binding();
      if (!in.payload.contains("result") || !in.payload["result"].is_object()) {
        throw std::invalid_argument("invalid command.result payload");
      }
      messages_.on_command_result(
          in.agent_mac,
          in.command_id,
          in.command_state,
          in.exit_code,
          in.payload["result"],
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
    log::info(
        "agent session closing: agent_mac={}, session_token={}",
        agent_mac,
        session_token);
    std::string disconnect_error;
    if (!registry_.on_disconnect(agent_mac, session_token, &disconnect_error)) {
      log::warn(
          "unbind agent session failed: agent_mac={}, session_token={}, error={}, persist_failure_count={}",
          agent_mac,
          session_token,
          disconnect_error.empty() ? "unknown" : disconnect_error,
          registry_.persist_failure_count());
    }
  }
}

} // namespace ctrl::adapters
