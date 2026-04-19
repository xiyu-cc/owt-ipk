#include "internal.h"

namespace app::bootstrap::runtime_internal {

WsAgentChannel::WsAgentChannel(const ctrl::ports::IClock& clock) : clock_(clock) {}

void WsAgentChannel::set_hub(ws_deal::ws_hub_api* hub) {
  std::lock_guard<std::mutex> lk(mutex_);
  hub_ = hub;
}

void WsAgentChannel::bind_session(
    std::string_view agent_mac,
    std::string_view agent_id,
    std::string_view session_id) {
  if (agent_mac.empty() || session_id.empty()) {
    return;
  }

  std::lock_guard<std::mutex> lk(mutex_);
  const auto mac = std::string(agent_mac);
  const auto session = std::string(session_id);
  auto old = agent_sessions_.find(mac);
  if (old != agent_sessions_.end()) {
    session_agents_.erase(old->second);
  }
  agent_sessions_[mac] = session;
  agent_display_ids_[mac] = agent_id.empty() ? mac : std::string(agent_id);
  session_agents_[session] = mac;
}

void WsAgentChannel::unbind_session(std::string_view session_id) {
  if (session_id.empty()) {
    return;
  }

  std::lock_guard<std::mutex> lk(mutex_);
  const auto sit = session_agents_.find(std::string(session_id));
  if (sit == session_agents_.end()) {
    return;
  }
  agent_sessions_.erase(sit->second);
  session_agents_.erase(sit);
}

std::string WsAgentChannel::find_agent_for_session(std::string_view session_id) const {
  if (session_id.empty()) {
    return {};
  }
  std::lock_guard<std::mutex> lk(mutex_);
  const auto it = session_agents_.find(std::string(session_id));
  if (it == session_agents_.end()) {
    return {};
  }
  return it->second;
}

bool WsAgentChannel::is_online(std::string_view agent_mac) const {
  std::lock_guard<std::mutex> lk(mutex_);
  return agent_sessions_.find(std::string(agent_mac)) != agent_sessions_.end();
}

bool WsAgentChannel::send_command(
    std::string_view agent_mac,
    const ctrl::domain::CommandSpec& cmd,
    std::string& error) {
  std::string session_id;
  std::string agent_id;
  ws_deal::ws_hub_api* hub = nullptr;
  {
    std::lock_guard<std::mutex> lk(mutex_);
    const auto it = agent_sessions_.find(std::string(agent_mac));
    if (it == agent_sessions_.end()) {
      error = "agent not connected";
      return false;
    }
    session_id = it->second;
    const auto display_it = agent_display_ids_.find(std::string(agent_mac));
    agent_id = (display_it == agent_display_ids_.end()) ? std::string(agent_mac) : display_it->second;
    hub = hub_;
  }

  if (hub == nullptr) {
    error = "ws hub is unavailable";
    return false;
  }

  const auto now_ms = clock_.now_ms();
  const auto seq = message_seq_.fetch_add(1, std::memory_order_relaxed);
  const std::string trace_id = cmd.trace_id.empty() ?
      ("trc-" + std::to_string(now_ms) + "-" + std::to_string(seq)) : cmd.trace_id;

  ws::AgentEnvelope envelope;
  envelope.type = std::string(owt::protocol::v4::agent::kTypeServerCommandDispatch);
  envelope.meta.protocol = std::string(kProtocolVersion);
  envelope.meta.trace_id = trace_id;
  envelope.meta.ts_ms = now_ms;
  envelope.meta.agent_id = agent_id;
  envelope.data = {
      {"command",
       {
           {"command_id", cmd.command_id},
           {"idempotency_key", cmd.command_id.empty() ? trace_id : cmd.command_id},
           {"command_type", ctrl::domain::to_string(cmd.kind)},
           {"issued_at_ms", now_ms},
           {"expires_at_ms", cmd.expires_at_ms},
           {"timeout_ms", cmd.timeout_ms},
           {"max_retry", cmd.max_retry},
           {"payload", cmd.payload},
       }},
  };

  hub->publish_to_session(session_id, true, ws::encode_agent_envelope(envelope));
  error.clear();
  return true;
}

} // namespace app::bootstrap::runtime_internal
