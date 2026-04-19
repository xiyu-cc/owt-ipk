#include "internal.h"

namespace app::bootstrap::runtime_internal {

WsUiPublisher::WsUiPublisher(
    ctrl::application::AgentRegistryService& registry,
    const ctrl::ports::IClock& clock,
    UiSubscriptionStore& subscriptions)
    : registry_(registry), clock_(clock), subscriptions_(subscriptions) {}

void WsUiPublisher::set_hub(ws_deal::ws_hub_api* hub) {
  std::lock_guard<std::mutex> lk(mutex_);
  hub_ = hub;
}

void WsUiPublisher::push_snapshot_to_session(std::string_view session_id, std::string_view reason) {
  if (session_id.empty()) {
    return;
  }
  auto agents = registry_.list_agents(true);
  size_t online_count = 0;
  nlohmann::json rows = nlohmann::json::array();
  for (const auto& state : agents) {
    if (state.online) {
      ++online_count;
    }
    rows.push_back(presenter::to_agent_json(state));
  }
  const auto resource = nlohmann::json{
      {"agents", std::move(rows)},
      {"online_count", online_count},
      {"total_count", agents.size()},
  };
  send_notify(
      session_id,
      owt::protocol::v4::ui::kEventAgentSnapshot,
      resource,
      nlohmann::json{{"reason", std::string(reason)}});
}

void WsUiPublisher::push_agent_to_session(
    std::string_view session_id,
    std::string_view agent_mac,
    std::string_view reason) {
  if (session_id.empty() || agent_mac.empty()) {
    return;
  }

  ctrl::domain::AgentState state;
  nlohmann::json resource = {
      {"agent_mac", std::string(agent_mac)},
      {"agent_id", std::string(agent_mac)},
      {"online", false},
      {"stats", nlohmann::json::object()},
  };
  if (registry_.get_agent(agent_mac, state)) {
    resource = presenter::to_agent_json(state);
  }
  send_notify(
      session_id,
      owt::protocol::v4::ui::kEventAgentUpdate,
      resource,
      nlohmann::json{{"reason", std::string(reason)}});
}

void WsUiPublisher::publish_snapshot(std::string_view reason, std::string_view agent_mac) {
  const auto rows = subscriptions_.snapshot();
  for (const auto& [session_id, sub] : rows) {
    if (sub.scope == UiSubscriptionStore::Scope::All) {
      push_snapshot_to_session(session_id, reason);
      continue;
    }
    if (!agent_mac.empty() && sub.agent_mac == agent_mac) {
      push_agent_to_session(session_id, agent_mac, reason);
    }
  }
}

void WsUiPublisher::publish_agent(std::string_view reason, std::string_view agent_mac) {
  if (agent_mac.empty()) {
    return;
  }
  const auto rows = subscriptions_.snapshot();
  for (const auto& [session_id, sub] : rows) {
    if (sub.scope == UiSubscriptionStore::Scope::All || sub.agent_mac == agent_mac) {
      push_agent_to_session(session_id, agent_mac, reason);
    }
  }
}

void WsUiPublisher::publish_command_event(
    const ctrl::domain::CommandSnapshot& command,
    const ctrl::domain::CommandEvent& event) {
  const auto rows = subscriptions_.snapshot();
  const auto resource = presenter::to_command_event_notification(command, event);
  for (const auto& [session_id, sub] : rows) {
    if (sub.scope == UiSubscriptionStore::Scope::All || sub.agent_mac == command.agent.mac) {
      send_notify(
          session_id,
          owt::protocol::v4::ui::kEventCommandEvent,
          resource,
          nlohmann::json{{"reason", std::string(owt::protocol::v4::ui::kEventCommandEvent)}});
    }
  }
}

void WsUiPublisher::send_notify(
    std::string_view session_id,
    std::string_view method,
    const nlohmann::json& resource,
    const nlohmann::json& extra_meta) {
  ws_deal::ws_hub_api* hub = nullptr;
  {
    std::lock_guard<std::mutex> lk(mutex_);
    hub = hub_;
  }
  if (hub == nullptr || session_id.empty()) {
    return;
  }

  nlohmann::json meta = {
      {"ts_ms", clock_.now_ms()},
  };
  if (extra_meta.is_object()) {
    for (auto it = extra_meta.begin(); it != extra_meta.end(); ++it) {
      meta[it.key()] = it.value();
    }
  }
  const auto payload = ws::jsonrpc_notify(
      std::string(method),
      nlohmann::json{{"resource", resource}, {"meta", std::move(meta)}});
  hub->publish_to_session(std::string(session_id), true, payload);
}

} // namespace app::bootstrap::runtime_internal
