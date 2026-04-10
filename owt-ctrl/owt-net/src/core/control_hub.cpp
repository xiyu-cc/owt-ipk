#include "service/control_hub.h"

#include "control/control_json_codec.h"
#include "log.h"
#include "server/websocket_session.h"
#include "service/command_store.h"
#include "service/observability.h"
#include "service/sensitive_json.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace service {

namespace {

std::mutex g_sessions_mutex;
std::unordered_map<std::string, std::weak_ptr<server::websocket_session>> g_ws_agent_sessions;
std::unordered_map<std::string, agent_runtime_state> g_agent_states;

std::vector<std::string> parse_capabilities(const std::string& capabilities_json) {
  if (capabilities_json.empty()) {
    return {};
  }
  const auto parsed = nlohmann::json::parse(capabilities_json, nullptr, false);
  if (!parsed.is_array()) {
    return {};
  }
  std::vector<std::string> out;
  out.reserve(parsed.size());
  for (const auto& item : parsed) {
    if (item.is_string()) {
      out.push_back(item.get<std::string>());
    }
  }
  return out;
}

std::string dump_capabilities(const std::vector<std::string>& capabilities) {
  nlohmann::json j = nlohmann::json::array();
  for (const auto& capability : capabilities) {
    j.push_back(capability);
  }
  return j.dump();
}

void persist_agent_state_locked(const agent_runtime_state& state, int64_t now_ms) {
  agent_record record;
  record.agent_id = state.agent_id;
  record.site_id = state.site_id;
  record.agent_version = state.agent_version;
  record.capabilities_json = dump_capabilities(state.capabilities);
  record.online = state.online;
  record.registered_at_ms = state.registered_at_ms;
  record.last_heartbeat_at_ms = state.last_heartbeat_at_ms;
  record.last_seen_at_ms = state.last_seen_at_ms;
  record.stats_json = state.stats_json;
  record.updated_at_ms = now_ms;

  std::string error;
  if (!upsert_agent(record, error)) {
    log::warn("persist agent state failed: agent_id={}, err={}", state.agent_id, error);
  }
}

void refresh_online_states_locked() {
  std::unordered_set<std::string> online_agents;
  for (auto it = g_ws_agent_sessions.begin(); it != g_ws_agent_sessions.end();) {
    if (it->second.expired()) {
      it = g_ws_agent_sessions.erase(it);
      continue;
    }
    online_agents.insert(it->first);
    ++it;
  }

  for (auto& [agent_id, state] : g_agent_states) {
    state.online = online_agents.find(agent_id) != online_agents.end();
  }
}

void mark_agent_online_locked(const std::string& agent_id, int64_t now_ms) {
  auto& state = g_agent_states[agent_id];
  state.agent_id = agent_id;
  state.online = true;
  state.last_seen_at_ms = now_ms;
  persist_agent_state_locked(state, now_ms);
}

} // namespace

void register_control_session(
    const std::string& agent_id,
    const std::shared_ptr<server::websocket_session>& session) {
  if (agent_id.empty() || !session) {
    return;
  }

  std::lock_guard<std::mutex> lk(g_sessions_mutex);
  g_ws_agent_sessions[agent_id] = session;
  mark_agent_online_locked(agent_id, control::unix_time_ms_now());
  log::info("control hub register websocket session: agent_id={}", agent_id);
}

void unregister_control_session(const std::string& agent_id, const server::websocket_session* session) {
  if (agent_id.empty() || session == nullptr) {
    return;
  }

  std::lock_guard<std::mutex> lk(g_sessions_mutex);
  auto it = g_ws_agent_sessions.find(agent_id);
  if (it == g_ws_agent_sessions.end()) {
    return;
  }

  const auto existing = it->second.lock();
  if (!existing || existing.get() == session) {
    g_ws_agent_sessions.erase(it);
    refresh_online_states_locked();
    const auto now = control::unix_time_ms_now();
    auto state_it = g_agent_states.find(agent_id);
    if (state_it != g_agent_states.end()) {
      state_it->second.last_seen_at_ms = now;
      persist_agent_state_locked(state_it->second, now);
    }
    log::info("control hub unregister websocket session: agent_id={}", agent_id);
  }
}

void update_agent_register_state(const control::register_payload& payload, int64_t received_at_ms) {
  if (payload.agent_id.empty()) {
    return;
  }

  std::lock_guard<std::mutex> lk(g_sessions_mutex);
  auto& state = g_agent_states[payload.agent_id];
  state.agent_id = payload.agent_id;
  state.site_id = payload.site_id;
  state.agent_version = payload.agent_version;
  state.capabilities = payload.capabilities;
  state.online = true;
  state.registered_at_ms = received_at_ms;
  state.last_heartbeat_at_ms = received_at_ms;
  state.last_seen_at_ms = received_at_ms;
  persist_agent_state_locked(state, received_at_ms);
}

void update_agent_heartbeat_state(
    const std::string& agent_id,
    const control::heartbeat_payload* payload,
    int64_t received_at_ms) {
  if (agent_id.empty()) {
    return;
  }

  std::lock_guard<std::mutex> lk(g_sessions_mutex);
  auto& state = g_agent_states[agent_id];
  state.agent_id = agent_id;
  state.online = true;
  state.last_seen_at_ms = received_at_ms;
  if (payload != nullptr) {
    state.last_heartbeat_at_ms =
        (payload->heartbeat_at_ms > 0) ? payload->heartbeat_at_ms : received_at_ms;
    state.stats_json = payload->stats_json;
  }
  persist_agent_state_locked(state, received_at_ms);
}

void bootstrap_agent_runtime_states() {
  const auto now = control::unix_time_ms_now();
  std::string error;
  if (!set_all_agents_offline(now, error)) {
    log::warn("mark agents offline on bootstrap failed: {}", error);
  }

  std::vector<agent_record> rows;
  error.clear();
  if (!list_agents(rows, error)) {
    log::warn("load agents from store failed: {}", error);
    return;
  }

  std::lock_guard<std::mutex> lk(g_sessions_mutex);
  g_agent_states.clear();
  for (const auto& row : rows) {
    agent_runtime_state state;
    state.agent_id = row.agent_id;
    state.site_id = row.site_id;
    state.agent_version = row.agent_version;
    state.capabilities = parse_capabilities(row.capabilities_json);
    state.online = row.online;
    state.registered_at_ms = row.registered_at_ms;
    state.last_heartbeat_at_ms = row.last_heartbeat_at_ms;
    state.last_seen_at_ms = row.last_seen_at_ms;
    state.stats_json = row.stats_json;
    g_agent_states[state.agent_id] = std::move(state);
  }
}

void list_agent_runtime_states(std::vector<agent_runtime_state>& out, bool include_offline) {
  std::lock_guard<std::mutex> lk(g_sessions_mutex);
  refresh_online_states_locked();

  out.clear();
  out.reserve(g_agent_states.size());
  for (const auto& [agent_id, state] : g_agent_states) {
    (void)agent_id;
    if (!include_offline && !state.online) {
      continue;
    }
    out.push_back(state);
  }
  std::sort(out.begin(), out.end(), [](const agent_runtime_state& lhs, const agent_runtime_state& rhs) {
    return lhs.agent_id < rhs.agent_id;
  });
}

bool get_agent_runtime_state(const std::string& agent_id, agent_runtime_state& out) {
  if (agent_id.empty()) {
    return false;
  }

  std::lock_guard<std::mutex> lk(g_sessions_mutex);
  refresh_online_states_locked();
  const auto it = g_agent_states.find(agent_id);
  if (it == g_agent_states.end()) {
    return false;
  }
  out = it->second;
  return true;
}

bool push_command_to_agent(
    const std::string& agent_id,
    const control::command& command,
    std::string& error) {
  if (agent_id.empty()) {
    error = "agent_id is empty";
    return false;
  }
  if (command.command_id.empty()) {
    error = "command_id is empty";
    return false;
  }
  const auto now = control::unix_time_ms_now();
  const auto redacted_payload_json = service::redact_sensitive_json_text(command.payload_json);

  std::shared_ptr<server::websocket_session> ws_session;
  {
    std::lock_guard<std::mutex> lk(g_sessions_mutex);
    auto it = g_ws_agent_sessions.find(agent_id);
    if (it != g_ws_agent_sessions.end()) {
      ws_session = it->second.lock();
      if (!ws_session) {
        g_ws_agent_sessions.erase(it);
      }
    }
  }
  if (!ws_session) {
    if (command.max_retry > 0) {
      command_record retry_record;
      retry_record.command_id = command.command_id;
      retry_record.agent_id = agent_id;
      retry_record.idempotency_key = command.idempotency_key;
      retry_record.command_type = control::to_string(command.type);
      retry_record.status = "RETRY_PENDING";
      retry_record.payload_json = redacted_payload_json;
      retry_record.result_json = "";
      retry_record.issued_at_ms = command.issued_at_ms;
      retry_record.expires_at_ms = command.expires_at_ms;
      retry_record.timeout_ms = command.timeout_ms;
      retry_record.max_retry = command.max_retry;
      retry_record.retry_count = 0;
      retry_record.next_retry_at_ms = now;
      retry_record.last_error = "agent not connected";
      retry_record.created_at_ms = now;
      retry_record.updated_at_ms = now;

      std::string db_error;
      if (!upsert_command(retry_record, db_error)) {
        error = "persist retry command failed: " + db_error;
        return false;
      }

      nlohmann::json detail = {
          {"event", "COMMAND_RETRY_SCHEDULED"},
          {"command_type", control::to_string(command.type)},
          {"retry_count", 0},
          {"max_retry", command.max_retry},
          {"next_retry_at_ms", now},
          {"error", "agent not connected"},
      };
      db_error.clear();
      if (!append_command_event(
              command.command_id,
              "COMMAND_RETRY_SCHEDULED",
              "RETRY_PENDING",
              detail.dump(),
              now,
              db_error)) {
        log::warn("persist command retry event failed: {}", db_error);
      }
      service::record_command_retry(command.command_id, 0, "agent not connected");
      error.clear();
      return true;
    }
    error = "agent not connected";
    return false;
  }

  control::envelope message;
  message.message_id = control::make_message_id();
  message.type = control::message_type::command_push;
  message.protocol_version = control::current_protocol_version();
  message.sent_at_ms = control::unix_time_ms_now();
  message.trace_id = message.message_id;
  message.agent_id = agent_id;
  message.payload = command;

  ws_session->send_text(control::encode_envelope_json(message));

  command_record record;
  record.command_id = command.command_id;
  record.agent_id = agent_id;
  record.idempotency_key = command.idempotency_key;
  record.command_type = control::to_string(command.type);
  record.status = "DISPATCHED";
  record.payload_json = redacted_payload_json;
  record.result_json = "";
  record.issued_at_ms = command.issued_at_ms;
  record.expires_at_ms = command.expires_at_ms;
  record.timeout_ms = command.timeout_ms;
  record.max_retry = command.max_retry;
  record.retry_count = 0;
  record.next_retry_at_ms = 0;
  record.last_error = "";
  record.created_at_ms = now;
  record.updated_at_ms = now;

  std::string db_error;
  if (!upsert_command(record, db_error)) {
    log::warn(
        "persist command dispatch failed: command_id={}, err={}", command.command_id, db_error);
  }

  nlohmann::json detail = {
      {"event", "COMMAND_PUSH"},
      {"command_type", control::to_string(command.type)},
      {"trace_id", message.trace_id},
  };
  db_error.clear();
  if (!append_command_event(
          command.command_id,
          "COMMAND_PUSH_SENT",
          "DISPATCHED",
          detail.dump(),
          now,
          db_error)) {
    log::warn("persist command dispatch event failed: {}", db_error);
  }

  return true;
}

size_t online_agent_count() {
  std::lock_guard<std::mutex> lk(g_sessions_mutex);
  refresh_online_states_locked();
  size_t count = 0;
  for (const auto& [agent_id, state] : g_agent_states) {
    (void)agent_id;
    if (state.online) {
      ++count;
    }
  }
  return count;
}

} // namespace service
