#include "ctrl/application/agent_registry_service.h"

#include <algorithm>

namespace ctrl::application {

AgentRegistryService::AgentRegistryService(ports::IAgentRepository& repo, const ports::IClock& clock)
    : repo_(repo), clock_(clock) {}

void AgentRegistryService::set_error(std::string* out, std::string&& value) {
  if (out != nullptr) {
    *out = std::move(value);
  }
}

bool AgentRegistryService::on_register(
    const domain::AgentState& register_state,
    std::string* persist_error) {
  if (register_state.agent.mac.empty()) {
    set_error(persist_error, "agent_mac is required");
    return false;
  }

  ensure_bootstrapped();
  std::lock_guard<std::mutex> lk(mutex_);
  auto state = register_state;
  const auto now = clock_.now_ms();
  state.agent.display_id =
      state.agent.display_id.empty() ? register_state.agent.mac : state.agent.display_id;
  state.online = true;
  if (state.registered_at_ms <= 0) {
    state.registered_at_ms = now;
  }
  if (state.last_seen_at_ms <= 0) {
    state.last_seen_at_ms = now;
  }
  if (state.last_heartbeat_at_ms <= 0) {
    state.last_heartbeat_at_ms = now;
  }
  states_[state.agent.mac] = state;
  std::string error;
  const auto persisted = persist_unlocked(state, error);
  set_error(persist_error, std::move(error));
  return persisted;
}

bool AgentRegistryService::on_heartbeat(
    std::string_view agent_mac,
    const nlohmann::json& heartbeat_stats,
    int64_t heartbeat_at_ms,
    std::string* persist_error) {
  if (agent_mac.empty()) {
    set_error(persist_error, "agent_mac is required");
    return false;
  }

  ensure_bootstrapped();
  std::lock_guard<std::mutex> lk(mutex_);
  const auto now = clock_.now_ms();
  auto& state = states_[std::string(agent_mac)];
  state.agent.mac = std::string(agent_mac);
  if (state.agent.display_id.empty()) {
    state.agent.display_id = state.agent.mac;
  }
  state.online = true;
  state.last_seen_at_ms = now;
  state.last_heartbeat_at_ms = (heartbeat_at_ms > 0) ? heartbeat_at_ms : now;
  state.stats = heartbeat_stats.is_object() ? heartbeat_stats : nlohmann::json::object();
  if (state.registered_at_ms <= 0) {
    state.registered_at_ms = now;
  }
  std::string error;
  const auto persisted = persist_unlocked(state, error);
  set_error(persist_error, std::move(error));
  return persisted;
}

bool AgentRegistryService::on_disconnect(
    std::string_view agent_mac,
    std::string_view session_token,
    std::string* persist_error) {
  if (agent_mac.empty()) {
    set_error(persist_error, "agent_mac is required");
    return false;
  }

  ensure_bootstrapped();
  std::lock_guard<std::mutex> lk(mutex_);
  const auto it = states_.find(std::string(agent_mac));
  if (it == states_.end()) {
    set_error(persist_error, "agent not found");
    return false;
  }

  const auto sit = sessions_.find(std::string(agent_mac));
  if (sit != sessions_.end() && !session_token.empty() && sit->second != session_token) {
    set_error(persist_error, "session token mismatch");
    return false;
  }

  sessions_.erase(std::string(agent_mac));
  it->second.online = false;
  it->second.last_seen_at_ms = clock_.now_ms();
  std::string error;
  const auto persisted = persist_unlocked(it->second, error);
  set_error(persist_error, std::move(error));
  return persisted;
}

bool AgentRegistryService::bind_session(
    std::string_view agent_mac,
    std::string_view session_token,
    std::string* persist_error) {
  if (agent_mac.empty() || session_token.empty()) {
    set_error(persist_error, "agent_mac/session_token is required");
    return false;
  }

  ensure_bootstrapped();
  std::lock_guard<std::mutex> lk(mutex_);
  sessions_[std::string(agent_mac)] = std::string(session_token);
  set_error(persist_error, "");
  return true;
}

bool AgentRegistryService::get_agent(std::string_view agent_mac, domain::AgentState& out) const {
  if (agent_mac.empty()) {
    return false;
  }

  ensure_bootstrapped();
  std::lock_guard<std::mutex> lk(mutex_);
  const auto it = states_.find(std::string(agent_mac));
  if (it == states_.end()) {
    return false;
  }
  out = it->second;
  return true;
}

std::vector<domain::AgentState> AgentRegistryService::list_agents(bool include_offline) const {
  ensure_bootstrapped();
  std::lock_guard<std::mutex> lk(mutex_);
  std::vector<domain::AgentState> out;
  out.reserve(states_.size());
  for (const auto& [agent_mac, state] : states_) {
    (void)agent_mac;
    if (!include_offline && !state.online) {
      continue;
    }
    out.push_back(state);
  }
  std::sort(out.begin(), out.end(), [](const domain::AgentState& lhs, const domain::AgentState& rhs) {
    if (lhs.agent.display_id == rhs.agent.display_id) {
      return lhs.agent.mac < rhs.agent.mac;
    }
    return lhs.agent.display_id < rhs.agent.display_id;
  });
  return out;
}

size_t AgentRegistryService::online_count() const {
  ensure_bootstrapped();
  std::lock_guard<std::mutex> lk(mutex_);
  size_t count = 0;
  for (const auto& [agent_mac, state] : states_) {
    (void)agent_mac;
    if (state.online) {
      ++count;
    }
  }
  return count;
}

uint64_t AgentRegistryService::persist_failure_count() const {
  std::lock_guard<std::mutex> lk(mutex_);
  return persist_failure_count_;
}

void AgentRegistryService::ensure_bootstrapped() const {
  std::lock_guard<std::mutex> lk(mutex_);
  if (bootstrapped_) {
    return;
  }

  std::vector<domain::AgentState> rows;
  std::string error;
  if (repo_.list(rows, error)) {
    states_.clear();
    for (const auto& row : rows) {
      if (!row.agent.mac.empty()) {
        states_[row.agent.mac] = row;
      }
    }
  }
  bootstrapped_ = true;
}

bool AgentRegistryService::persist_unlocked(const domain::AgentState& state, std::string& error) {
  if (repo_.upsert(state, error)) {
    return true;
  }
  ++persist_failure_count_;
  if (error.empty()) {
    error = "persist failed";
  }
  return false;
}

} // namespace ctrl::application
