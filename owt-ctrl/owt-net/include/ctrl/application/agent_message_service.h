#pragma once

#include "ctrl/ports/interfaces.h"
#include "ctrl/application/agent_registry_service.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string_view>

namespace ctrl::application {

class AgentMessageService {
public:
  AgentMessageService(
      ports::ICommandRepository& commands,
      AgentRegistryService& registry,
      ports::IStatusPublisher& publisher,
      ports::IMetrics& metrics,
      const ports::IClock& clock);

  void on_agent_registered(
      std::string_view agent_mac,
      std::string_view display_id,
      const nlohmann::json& meta);
  void on_agent_heartbeat(
      std::string_view agent_mac,
      const nlohmann::json& heartbeat_stats,
      int64_t heartbeat_at_ms);
  void on_command_ack(
      std::string_view agent_mac,
      std::string_view command_id,
      domain::CommandState ack_state,
      std::string_view trace_id,
      std::string_view message);
  void on_command_result(
      std::string_view agent_mac,
      std::string_view command_id,
      domain::CommandState final_state,
      int exit_code,
      const nlohmann::json& result,
      std::string_view trace_id);

private:
  domain::CommandEvent append_event(
      std::string_view command_id,
      std::string_view type,
      domain::CommandState state,
      const nlohmann::json& detail);

  ports::ICommandRepository& commands_;
  AgentRegistryService& registry_;
  ports::IStatusPublisher& publisher_;
  ports::IMetrics& metrics_;
  const ports::IClock& clock_;
};

} // namespace ctrl::application
