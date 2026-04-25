#pragma once

#include "app/bootstrap/runtime/session_runtime.h"
#include "app/ws/command_bus_protocol.h"
#include "ctrl/adapters/control_ws_use_cases.h"
#include "ctrl/application/redaction_service.h"
#include "ctrl/ports/interfaces.h"

#include <drogon/WebSocketConnection.h>

#include <functional>
#include <string>
#include <unordered_map>

namespace app::bootstrap::runtime {

class AgentActionGateway {
public:
  using TraceIdFn = std::function<std::string()>;

  AgentActionGateway(
      AgentSessionRegistry& agent_sessions,
      ctrl::adapters::ControlWsUseCases& control_ws_use_cases,
      ctrl::application::RedactionService& redaction_service,
      const ctrl::ports::IClock& clock,
      TraceIdFn trace_id_fn);

  void handle(
      const drogon::WebSocketConnectionPtr& conn,
      const std::string& session_id,
      const ws::BusEnvelope& req);

private:
  using Strategy = std::function<void(
      const drogon::WebSocketConnectionPtr&,
      const std::string&,
      const ws::BusEnvelope&)>;

  void send_agent_error(
      const drogon::WebSocketConnectionPtr& conn,
      const nlohmann::json& req_id,
      std::string_view code,
      std::string message);

  void handle_register(
      const drogon::WebSocketConnectionPtr& conn,
      const std::string& session_id,
      const ws::BusEnvelope& req);
  void handle_heartbeat(
      const drogon::WebSocketConnectionPtr& conn,
      const std::string& session_id,
      const ws::BusEnvelope& req);
  void handle_command_ack(
      const drogon::WebSocketConnectionPtr& conn,
      const std::string& session_id,
      const ws::BusEnvelope& req);
  void handle_command_result(
      const drogon::WebSocketConnectionPtr& conn,
      const std::string& session_id,
      const ws::BusEnvelope& req);

  AgentSessionRegistry& agent_sessions_;
  ctrl::adapters::ControlWsUseCases& control_ws_use_cases_;
  ctrl::application::RedactionService& redaction_service_;
  const ctrl::ports::IClock& clock_;
  TraceIdFn trace_id_fn_;
  std::unordered_map<std::string, Strategy> handlers_;
};

} // namespace app::bootstrap::runtime
