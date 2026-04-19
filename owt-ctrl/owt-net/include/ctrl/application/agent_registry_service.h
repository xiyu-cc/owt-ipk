#pragma once

#include "ctrl/ports/interfaces.h"

#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ctrl::application {

class AgentRegistryService {
public:
  AgentRegistryService(ports::IAgentRepository& repo, const ports::IClock& clock);

  void on_register(const domain::AgentState& register_state);
  void on_heartbeat(
      std::string_view agent_mac,
      const nlohmann::json& heartbeat_stats,
      int64_t heartbeat_at_ms);
  bool on_disconnect(std::string_view agent_mac, std::string_view session_token);
  void bind_session(std::string_view agent_mac, std::string_view session_token);
  bool get_agent(std::string_view agent_mac, domain::AgentState& out) const;
  std::vector<domain::AgentState> list_agents(bool include_offline) const;
  size_t online_count() const;

private:
  void ensure_bootstrapped() const;
  void persist_unlocked(const domain::AgentState& state);

  ports::IAgentRepository& repo_;
  const ports::IClock& clock_;

  mutable std::mutex mutex_;
  mutable bool bootstrapped_ = false;
  mutable std::unordered_map<std::string, domain::AgentState> states_;
  mutable std::unordered_map<std::string, std::string> sessions_;
};

} // namespace ctrl::application
