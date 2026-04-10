#include "service/frontend_status_stream.h"

#include "control/control_protocol.h"
#include "server/websocket_session.h"
#include "service/control_hub.h"

#include <nlohmann/json.hpp>

#include <mutex>
#include <vector>

namespace service {

namespace {

std::mutex g_frontend_status_sessions_mutex;
std::vector<std::weak_ptr<server::websocket_session>> g_frontend_status_sessions;

nlohmann::json parse_json_or_null(const std::string& text) {
  if (text.empty()) {
    return nullptr;
  }
  auto parsed = nlohmann::json::parse(text, nullptr, false);
  if (parsed.is_discarded()) {
    return text;
  }
  return parsed;
}

nlohmann::json build_status_snapshot(const std::string& reason, const std::string& agent_id) {
  std::vector<service::agent_runtime_state> states;
  service::list_agent_runtime_states(states, true);

  nlohmann::json agents = nlohmann::json::array();
  std::size_t online_count = 0;
  for (const auto& state : states) {
    if (state.online) {
      ++online_count;
    }
    agents.push_back({
        {"agent_id", state.agent_id},
        {"site_id", state.site_id},
        {"agent_version", state.agent_version},
        {"capabilities", state.capabilities},
        {"online", state.online},
        {"registered_at_ms", state.registered_at_ms},
        {"last_heartbeat_at_ms", state.last_heartbeat_at_ms},
        {"last_seen_at_ms", state.last_seen_at_ms},
        {"stats", parse_json_or_null(state.stats_json)},
    });
  }

  return {
      {"type", "STATUS_SNAPSHOT"},
      {"sent_at_ms", control::unix_time_ms_now()},
      {"reason", reason},
      {"agent_id", agent_id},
      {"data",
       {
           {"agents", std::move(agents)},
           {"online_count", online_count},
           {"total_count", states.size()},
       }},
  };
}

std::vector<std::shared_ptr<server::websocket_session>> collect_live_sessions() {
  std::vector<std::shared_ptr<server::websocket_session>> live_sessions;
  std::lock_guard<std::mutex> lk(g_frontend_status_sessions_mutex);
  for (auto it = g_frontend_status_sessions.begin(); it != g_frontend_status_sessions.end();) {
    const auto session = it->lock();
    if (!session) {
      it = g_frontend_status_sessions.erase(it);
      continue;
    }
    live_sessions.push_back(std::move(session));
    ++it;
  }
  return live_sessions;
}

} // namespace

void register_frontend_status_session(const std::shared_ptr<server::websocket_session>& session) {
  if (!session) {
    return;
  }
  {
    std::lock_guard<std::mutex> lk(g_frontend_status_sessions_mutex);
    g_frontend_status_sessions.push_back(session);
  }
  session->send_text(build_status_snapshot("session_open", "").dump());
}

void unregister_frontend_status_session(const server::websocket_session* session) {
  if (session == nullptr) {
    return;
  }
  std::lock_guard<std::mutex> lk(g_frontend_status_sessions_mutex);
  for (auto it = g_frontend_status_sessions.begin(); it != g_frontend_status_sessions.end();) {
    const auto current = it->lock();
    if (!current || current.get() == session) {
      it = g_frontend_status_sessions.erase(it);
      continue;
    }
    ++it;
  }
}

void broadcast_frontend_status_snapshot(const std::string& reason, const std::string& agent_id) {
  const auto payload = build_status_snapshot(reason, agent_id).dump();
  auto sessions = collect_live_sessions();
  for (const auto& session : sessions) {
    session->send_text(payload);
  }
}

} // namespace service
