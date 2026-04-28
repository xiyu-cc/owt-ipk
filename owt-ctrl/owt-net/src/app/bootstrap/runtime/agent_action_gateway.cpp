#include "app/bootstrap/runtime/agent_action_gateway.h"

#include "app/runtime_log.h"
#include "app/ws/command_bus_protocol.h"
#include "owt/protocol/v5/contract.h"

#include <stdexcept>
#include <string>
#include <vector>

namespace app::bootstrap::runtime {

namespace {

std::string request_id_to_text(const nlohmann::json& request_id) {
  if (request_id.is_null()) {
    return "";
  }
  if (request_id.is_string()) {
    return request_id.get<std::string>();
  }
  if (request_id.is_number_integer()) {
    return std::to_string(request_id.get<int64_t>());
  }
  if (request_id.is_number_unsigned()) {
    return std::to_string(request_id.get<uint64_t>());
  }
  return request_id.dump();
}

std::string require_payload_agent_mac(
    const ws::BusEnvelope& req,
    const std::string& session_id,
    std::string_view action_name) {
  if (!req.payload.contains("agent_mac") || !req.payload["agent_mac"].is_string() ||
      req.payload["agent_mac"].get<std::string>().empty()) {
    log::warn(
        "reject agent action: action={}, session_id={}, req_id={}, reason=missing payload.agent_mac",
        action_name,
        session_id,
        request_id_to_text(req.id));
    throw std::invalid_argument("agent_mac is required");
  }
  return req.payload["agent_mac"].get<std::string>();
}

void reject_unknown_payload_fields(
    const nlohmann::json& payload,
    std::string_view action_name) {
  if (!payload.is_object()) {
    throw std::invalid_argument("payload must be object");
  }
  for (auto it = payload.begin(); it != payload.end(); ++it) {
    const auto key = it.key();
    if (!owt::protocol::v5::agent::is_known_action_payload_field(action_name, key)) {
      throw std::invalid_argument(
          "unknown field in " + std::string(action_name) + " payload: " + key);
    }
  }
}

void enforce_bound_agent_for_strict_action(
    AgentSessionRegistry& agent_sessions,
    std::string_view session_id,
    std::string_view payload_agent_mac,
    std::string_view action_name,
    const nlohmann::json& request_id) {
  const auto bound_agent_mac = agent_sessions.find_agent_for_session(session_id);
  if (bound_agent_mac.empty()) {
    log::warn(
        "reject agent action: action={}, session_id={}, req_id={}, reason=session not registered",
        action_name,
        session_id,
        request_id_to_text(request_id));
    throw std::invalid_argument("agent.register is required before agent actions");
  }
  if (bound_agent_mac != payload_agent_mac) {
    log::warn(
        "reject agent action: action={}, session_id={}, req_id={}, reason=agent_mac mismatch, bound_agent_mac={}, payload_agent_mac={}",
        action_name,
        session_id,
        request_id_to_text(request_id),
        bound_agent_mac,
        payload_agent_mac);
    throw std::invalid_argument("payload.agent_mac does not match registered session agent");
  }
}

} // namespace

AgentActionGateway::AgentActionGateway(
    AgentSessionRegistry& agent_sessions,
    ctrl::adapters::ControlWsUseCases& control_ws_use_cases,
    ctrl::application::RedactionService& redaction_service,
    const ctrl::ports::IClock& clock,
    TraceIdFn trace_id_fn)
    : agent_sessions_(agent_sessions),
      control_ws_use_cases_(control_ws_use_cases),
      redaction_service_(redaction_service),
      clock_(clock),
      trace_id_fn_(std::move(trace_id_fn)) {
  handlers_ = {
      {std::string(owt::protocol::v5::agent::kActionAgentRegister), [this](const auto& c, const auto& s, const auto& r) {
         handle_register(c, s, r);
       }},
      {std::string(owt::protocol::v5::agent::kActionAgentHeartbeat), [this](const auto& c, const auto& s, const auto& r) {
         handle_heartbeat(c, s, r);
       }},
      {std::string(owt::protocol::v5::agent::kActionCommandAck), [this](const auto& c, const auto& s, const auto& r) {
         handle_command_ack(c, s, r);
       }},
      {std::string(owt::protocol::v5::agent::kActionCommandResult), [this](const auto& c, const auto& s, const auto& r) {
         handle_command_result(c, s, r);
       }},
  };
}

void AgentActionGateway::send_agent_error(
    const drogon::WebSocketConnectionPtr& conn,
    const nlohmann::json& req_id,
    std::string_view code,
    std::string message) {
  conn->send(
      ws::bus_error(
          owt::protocol::v5::agent::kErrorServerError,
          req_id,
          clock_.now_ms(),
          code,
          std::move(message)),
      drogon::WebSocketMessageType::Text);
}

void AgentActionGateway::handle_register(
    const drogon::WebSocketConnectionPtr& conn,
    const std::string& session_id,
    const ws::BusEnvelope& req) {
  reject_unknown_payload_fields(
      req.payload,
      owt::protocol::v5::agent::kActionAgentRegister);
  if (!req.payload.contains("agent_mac") || !req.payload["agent_mac"].is_string() ||
      req.payload["agent_mac"].get<std::string>().empty()) {
    throw std::invalid_argument("agent_mac is required");
  }
  const auto agent_mac = req.payload["agent_mac"].get<std::string>();
  const auto agent_id = req.payload.value("agent_id", agent_mac);

  agent_sessions_.bind_agent(session_id, agent_mac, agent_id);

  ctrl::adapters::WsInboundMessage in;
  in.session_token = session_id;
  in.kind = ctrl::adapters::WsMessageKind::Register;
  in.trace_id = req.id.is_string() ? req.id.get<std::string>() : trace_id_fn_();
  in.agent_mac = agent_mac;
  in.agent_id = agent_id;
  in.payload = req.payload;

  std::vector<ctrl::adapters::WsOutboundMessage> out;
  control_ws_use_cases_.on_text(in, out);
  for (const auto& item : out) {
    if (item.type == owt::protocol::v5::agent::kEventAgentRegistered) {
      conn->send(
          ws::bus_event(
              item.type,
              clock_.now_ms(),
              item.payload,
              agent_mac),
          drogon::WebSocketMessageType::Text);
      continue;
    }
    if (item.type == owt::protocol::v5::agent::kErrorServerError) {
      const auto code = item.payload.value("code", std::string(owt::protocol::v5::error_code::kInternalError));
      const auto message = item.payload.value("message", std::string("server error"));
      const auto detail = item.payload.value("detail", nlohmann::json::object());
      conn->send(
          ws::bus_error(
              item.type,
              req.id,
              clock_.now_ms(),
              code,
              message,
              detail),
          drogon::WebSocketMessageType::Text);
    }
  }
}

void AgentActionGateway::handle_heartbeat(
    const drogon::WebSocketConnectionPtr& conn,
    const std::string& session_id,
    const ws::BusEnvelope& req) {
  (void)conn;
  reject_unknown_payload_fields(
      req.payload,
      owt::protocol::v5::agent::kActionAgentHeartbeat);
  const auto agent_mac = require_payload_agent_mac(req, session_id, owt::protocol::v5::agent::kActionAgentHeartbeat);
  enforce_bound_agent_for_strict_action(
      agent_sessions_,
      session_id,
      agent_mac,
      owt::protocol::v5::agent::kActionAgentHeartbeat,
      req.id);

  const int64_t heartbeat_at_ms =
      (req.payload.contains("heartbeat_at_ms") && req.payload["heartbeat_at_ms"].is_number_integer())
      ? req.payload["heartbeat_at_ms"].get<int64_t>()
      : 0;
  nlohmann::json stats = nlohmann::json::object();
  if (req.payload.contains("stats") && req.payload["stats"].is_object()) {
    stats = req.payload["stats"];
  }

  ctrl::adapters::WsInboundMessage in;
  in.session_token = session_id;
  in.kind = ctrl::adapters::WsMessageKind::Heartbeat;
  in.trace_id = req.id.is_string() ? req.id.get<std::string>() : trace_id_fn_();
  in.agent_mac = agent_mac;
  in.payload = req.payload;
  in.payload["heartbeat_at_ms"] = heartbeat_at_ms;
  in.payload["stats"] = stats;

  std::vector<ctrl::adapters::WsOutboundMessage> out;
  control_ws_use_cases_.on_text(in, out);
}

void AgentActionGateway::handle_command_ack(
    const drogon::WebSocketConnectionPtr& conn,
    const std::string& session_id,
    const ws::BusEnvelope& req) {
  (void)conn;
  reject_unknown_payload_fields(
      req.payload,
      owt::protocol::v5::agent::kActionCommandAck);
  const auto agent_mac = require_payload_agent_mac(req, session_id, owt::protocol::v5::agent::kActionCommandAck);
  enforce_bound_agent_for_strict_action(
      agent_sessions_,
      session_id,
      agent_mac,
      owt::protocol::v5::agent::kActionCommandAck,
      req.id);
  if (!req.payload.contains("command_id") || !req.payload["command_id"].is_string() ||
      req.payload["command_id"].get<std::string>().empty() || !req.payload.contains("status") ||
      !req.payload["status"].is_string()) {
    throw std::invalid_argument("invalid command.ack payload");
  }

  ctrl::domain::CommandState state = ctrl::domain::CommandState::Acked;
  if (!ctrl::domain::try_parse_command_state(req.payload["status"].get<std::string>(), state)) {
    throw std::invalid_argument("invalid command status");
  }

  ctrl::adapters::WsInboundMessage in;
  in.session_token = session_id;
  in.kind = ctrl::adapters::WsMessageKind::CommandAck;
  in.trace_id = req.id.is_string() ? req.id.get<std::string>() : trace_id_fn_();
  in.agent_mac = agent_mac;
  in.command_state = state;
  in.command_id = req.payload["command_id"].get<std::string>();
  in.payload = req.payload;

  std::vector<ctrl::adapters::WsOutboundMessage> out;
  control_ws_use_cases_.on_text(in, out);
}

void AgentActionGateway::handle_command_result(
    const drogon::WebSocketConnectionPtr& conn,
    const std::string& session_id,
    const ws::BusEnvelope& req) {
  (void)conn;
  reject_unknown_payload_fields(
      req.payload,
      owt::protocol::v5::agent::kActionCommandResult);
  const auto agent_mac = require_payload_agent_mac(req, session_id, owt::protocol::v5::agent::kActionCommandResult);
  enforce_bound_agent_for_strict_action(
      agent_sessions_,
      session_id,
      agent_mac,
      owt::protocol::v5::agent::kActionCommandResult,
      req.id);
  if (!req.payload.contains("command_id") || !req.payload["command_id"].is_string() ||
      req.payload["command_id"].get<std::string>().empty() || !req.payload.contains("final_status") ||
      !req.payload["final_status"].is_string() || !req.payload.contains("exit_code") ||
      !req.payload["exit_code"].is_number_integer() || !req.payload.contains("result") ||
      !req.payload["result"].is_object()) {
    throw std::invalid_argument("invalid command.result payload");
  }

  ctrl::domain::CommandState final_state = ctrl::domain::CommandState::Failed;
  if (!ctrl::domain::try_parse_command_state(req.payload["final_status"].get<std::string>(), final_state)) {
    throw std::invalid_argument("invalid final_status");
  }

  ctrl::adapters::WsInboundMessage in;
  in.session_token = session_id;
  in.kind = ctrl::adapters::WsMessageKind::CommandResult;
  in.trace_id = req.id.is_string() ? req.id.get<std::string>() : trace_id_fn_();
  in.agent_mac = agent_mac;
  in.command_state = final_state;
  in.command_id = req.payload["command_id"].get<std::string>();
  in.exit_code = req.payload["exit_code"].get<int>();
  in.payload = req.payload;

  std::vector<ctrl::adapters::WsOutboundMessage> out;
  control_ws_use_cases_.on_text(in, out);
}

void AgentActionGateway::handle(
    const drogon::WebSocketConnectionPtr& conn,
    const std::string& session_id,
    const ws::BusEnvelope& req) {
  try {
    const auto it = handlers_.find(req.name);
    if (it == handlers_.end()) {
      send_agent_error(
          conn,
          req.id,
          owt::protocol::v5::error_code::kMethodNotFound,
          "unsupported agent action");
      return;
    }
    it->second(conn, session_id, req);
  } catch (const std::invalid_argument& ex) {
    send_agent_error(
        conn,
        req.id,
        owt::protocol::v5::error_code::kInvalidParams,
        redaction_service_.redact_text(ex.what()));
  } catch (const std::exception& ex) {
    send_agent_error(
        conn,
        req.id,
        owt::protocol::v5::error_code::kInternalError,
        redaction_service_.redact_text(ex.what()));
  }
}

} // namespace app::bootstrap::runtime
