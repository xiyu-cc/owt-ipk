#pragma once

#include "app/bootstrap/runtime/session_runtime.h"
#include "app/ws/command_bus_protocol.h"
#include "app/ws/scheduler/event_scheduler.h"
#include "ctrl/application/agent_registry_service.h"
#include "ctrl/application/command_orchestrator.h"
#include "ctrl/application/params_service.h"
#include "ctrl/application/rate_limiter_service.h"
#include "ctrl/application/redaction_service.h"
#include "ctrl/ports/interfaces.h"

#include <functional>
#include <string>
#include <unordered_map>

namespace app::bootstrap::runtime {

class UiActionGateway {
public:
  UiActionGateway(
      UiSubscriptionStore& subscriptions,
      UiSessionRegistry& ui_sessions,
      app::ws::scheduler::EventScheduler& event_scheduler,
      BusStatusPublisher& status_publisher,
      ctrl::application::AgentRegistryService& registry_service,
      ctrl::application::ParamsService& params_service,
      ctrl::application::RateLimiterService& rate_limiter_service,
      ctrl::application::CommandOrchestrator& command_orchestrator,
      ctrl::application::RedactionService& redaction_service,
      ctrl::ports::IMetrics& metrics,
      const ctrl::ports::IClock& clock);

  void handle(
      const std::string& session_id,
      const std::string& actor_id,
      const ws::BusEnvelope& req);

private:
  using Strategy = std::function<void(
      const std::string&,
      const std::string&,
      const ws::BusEnvelope&)>;

  void send_ui_result(
      std::string_view session_id,
      std::string_view name,
      const nlohmann::json& id,
      const nlohmann::json& payload);
  void send_ui_error(
      std::string_view session_id,
      std::string_view name,
      const nlohmann::json& id,
      std::string_view code,
      std::string message,
      const nlohmann::json& detail = nlohmann::json::object());

  bool enforce_ui_rate_limit(
      const std::string& session_id,
      const std::string& actor_id,
      const ws::BusEnvelope& req);

  void handle_session_subscribe(
      const std::string& session_id,
      const std::string& actor_id,
      const ws::BusEnvelope& req);
  void handle_agent_list(
      const std::string& session_id,
      const std::string& actor_id,
      const ws::BusEnvelope& req);
  void handle_params_get(
      const std::string& session_id,
      const std::string& actor_id,
      const ws::BusEnvelope& req);
  void handle_params_update(
      const std::string& session_id,
      const std::string& actor_id,
      const ws::BusEnvelope& req);
  void handle_command_submit(
      const std::string& session_id,
      const std::string& actor_id,
      const ws::BusEnvelope& req);

  UiSubscriptionStore& subscriptions_;
  UiSessionRegistry& ui_sessions_;
  app::ws::scheduler::EventScheduler& event_scheduler_;
  BusStatusPublisher& status_publisher_;
  ctrl::application::AgentRegistryService& registry_service_;
  ctrl::application::ParamsService& params_service_;
  ctrl::application::RateLimiterService& rate_limiter_service_;
  ctrl::application::CommandOrchestrator& command_orchestrator_;
  ctrl::application::RedactionService& redaction_service_;
  ctrl::ports::IMetrics& metrics_;
  const ctrl::ports::IClock& clock_;
  std::unordered_map<std::string, Strategy> handlers_;
};

} // namespace app::bootstrap::runtime
