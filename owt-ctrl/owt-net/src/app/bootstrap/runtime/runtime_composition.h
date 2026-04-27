#pragma once

#include "app/bootstrap/runtime/agent_action_gateway.h"
#include "app/bootstrap/runtime/lifecycle.h"
#include "app/bootstrap/runtime/session_runtime.h"
#include "app/bootstrap/runtime/ui_action_gateway.h"
#include "app/bootstrap/runtime/ws_envelope_validator.h"
#include "app/config.h"
#include "app/ws/scheduler/event_scheduler.h"
#include "ctrl/adapters/control_ws_use_cases.h"
#include "ctrl/application/agent_message_service.h"
#include "ctrl/application/agent_registry_service.h"
#include "ctrl/application/command_orchestrator.h"
#include "ctrl/application/params_service.h"
#include "ctrl/application/rate_limiter_service.h"
#include "ctrl/application/redaction_service.h"
#include "ctrl/application/retry_service.h"
#include "ctrl/infrastructure/sqlite_store.h"
#include "ctrl/ports/defaults.h"

#include <drogon/WebSocketConnection.h>

#include <atomic>
#include <string>

namespace app::bootstrap::runtime {

class RuntimeComposition {
public:
  explicit RuntimeComposition(const Config& cfg);

  std::string trace_id();

  void on_ui_open(
      const drogon::HttpRequestPtr& req,
      const drogon::WebSocketConnectionPtr& conn);
  void on_ui_close(const drogon::WebSocketConnectionPtr& conn);
  void on_ui_message(
      const drogon::WebSocketConnectionPtr& conn,
      std::string&& message,
      const drogon::WebSocketMessageType& type);

  void on_agent_open(
      const drogon::HttpRequestPtr& req,
      const drogon::WebSocketConnectionPtr& conn);
  void on_agent_close(const drogon::WebSocketConnectionPtr& conn);
  void on_agent_message(
      const drogon::WebSocketConnectionPtr& conn,
      std::string&& message,
      const drogon::WebSocketMessageType& type);

public:
  Config config;
  ctrl::ports::SystemClock clock;
  ctrl::ports::DefaultIdGenerator id_generator;
  RuntimeMetrics metrics;
  ctrl::infrastructure::SqliteStore store;
  UiSubscriptionStore subscriptions;
  UiSessionRegistry ui_sessions;
  app::ws::scheduler::EventScheduler event_scheduler;
  AgentSessionRegistry agent_sessions;
  DrogonAgentChannel agent_channel;
  ctrl::application::AgentRegistryService registry_service;
  BusStatusPublisher status_publisher;
  ctrl::application::ParamsService params_service;
  ctrl::application::RateLimiterService rate_limiter_service;
  ctrl::application::RedactionService redaction_service;
  ctrl::application::CommandOrchestrator command_orchestrator;
  ctrl::application::AgentMessageService agent_message_service;
  ctrl::adapters::ControlWsUseCases control_ws_use_cases;
  ctrl::application::RetryService retry_service;
  RuntimeLifecycle lifecycle;
  UiActionGateway ui_gateway;
  AgentActionGateway agent_gateway;

private:
  std::atomic<uint64_t> trace_id_seq_{0};
};

} // namespace app::bootstrap::runtime
