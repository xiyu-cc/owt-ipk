#pragma once

#include "ctrl/domain/types.h"
#include "ctrl/application/agent_message_service.h"
#include "ctrl/application/agent_registry_service.h"

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace ctrl::adapters {

enum class WsMessageKind {
  Register,
  Heartbeat,
  CommandAck,
  CommandResult,
  Unknown,
};

struct WsInboundMessage {
  std::string session_token;
  WsMessageKind kind = WsMessageKind::Unknown;
  std::string trace_id;
  std::string agent_mac;
  std::string agent_id;
  domain::CommandState command_state = domain::CommandState::Created;
  std::string command_id;
  int exit_code = 0;
  nlohmann::json payload = nlohmann::json::object();
};

struct WsOutboundMessage {
  std::string session_token;
  std::string type;
  std::string trace_id;
  std::string agent_id;
  nlohmann::json payload = nlohmann::json::object();
};

class ControlWsUseCases {
public:
  ControlWsUseCases(
      application::AgentRegistryService& registry,
      application::AgentMessageService& messages);

  void on_open(std::string_view session_token);
  void on_text(const WsInboundMessage& in, std::vector<WsOutboundMessage>& out);
  void on_close(std::string_view session_token);

private:
  application::AgentRegistryService& registry_;
  application::AgentMessageService& messages_;
  mutable std::mutex mutex_;
  std::unordered_map<std::string, std::string> session_agents_;
};

} // namespace ctrl::adapters
