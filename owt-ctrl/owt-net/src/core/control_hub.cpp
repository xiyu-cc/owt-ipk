#include "service/control_hub.h"

#include "control/control_json_codec.h"
#include "log.h"
#include "server/websocket_session.h"
#include "service/command_store.h"

#include <nlohmann/json.hpp>

#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace service {

namespace {

std::mutex g_sessions_mutex;
std::unordered_map<std::string, std::weak_ptr<server::websocket_session>> g_wss_agent_sessions;
std::unordered_map<std::string, std::weak_ptr<i_control_session>> g_grpc_agent_sessions;

} // namespace

void register_control_session(
    const std::string& agent_id,
    const std::shared_ptr<server::websocket_session>& session) {
  if (agent_id.empty() || !session) {
    return;
  }

  std::lock_guard<std::mutex> lk(g_sessions_mutex);
  g_wss_agent_sessions[agent_id] = session;
  log::info("control hub register wss session: agent_id={}", agent_id);
}

void unregister_control_session(const std::string& agent_id, const server::websocket_session* session) {
  if (agent_id.empty() || session == nullptr) {
    return;
  }

  std::lock_guard<std::mutex> lk(g_sessions_mutex);
  auto it = g_wss_agent_sessions.find(agent_id);
  if (it == g_wss_agent_sessions.end()) {
    return;
  }

  const auto existing = it->second.lock();
  if (!existing || existing.get() == session) {
    g_wss_agent_sessions.erase(it);
    log::info("control hub unregister wss session: agent_id={}", agent_id);
  }
}

void register_grpc_control_session(
    const std::string& agent_id,
    const std::shared_ptr<i_control_session>& session) {
  if (agent_id.empty() || !session) {
    return;
  }

  std::lock_guard<std::mutex> lk(g_sessions_mutex);
  g_grpc_agent_sessions[agent_id] = session;
  log::info("control hub register grpc session: agent_id={}", agent_id);
}

void unregister_grpc_control_session(const std::string& agent_id, const i_control_session* session) {
  if (agent_id.empty() || session == nullptr) {
    return;
  }

  std::lock_guard<std::mutex> lk(g_sessions_mutex);
  auto it = g_grpc_agent_sessions.find(agent_id);
  if (it == g_grpc_agent_sessions.end()) {
    return;
  }

  const auto existing = it->second.lock();
  if (!existing || existing.get() == session) {
    g_grpc_agent_sessions.erase(it);
    log::info("control hub unregister grpc session: agent_id={}", agent_id);
  }
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

  std::shared_ptr<server::websocket_session> wss_session;
  std::shared_ptr<i_control_session> grpc_session;
  auto selected_channel = control::channel_type::wss;
  {
    std::lock_guard<std::mutex> lk(g_sessions_mutex);
    auto wss_it = g_wss_agent_sessions.find(agent_id);
    if (wss_it != g_wss_agent_sessions.end()) {
      wss_session = wss_it->second.lock();
      if (!wss_session) {
        g_wss_agent_sessions.erase(wss_it);
      }
    }
    if (!wss_session) {
      auto grpc_it = g_grpc_agent_sessions.find(agent_id);
      if (grpc_it != g_grpc_agent_sessions.end()) {
        grpc_session = grpc_it->second.lock();
        if (!grpc_session) {
          g_grpc_agent_sessions.erase(grpc_it);
        } else {
          selected_channel = control::channel_type::grpc;
        }
      }
    }
  }
  if (!wss_session && !grpc_session) {
    error = "agent not connected";
    return false;
  }

  control::envelope message;
  message.message_id = control::make_message_id();
  message.type = control::message_type::command_push;
  message.protocol_version = "v1.0-draft";
  message.channel = selected_channel;
  message.sent_at_ms = control::unix_time_ms_now();
  message.trace_id = message.message_id;
  message.agent_id = agent_id;
  message.payload = command;

  bool send_ok = true;
  if (wss_session) {
    wss_session->send_control_message(message);
  } else {
    send_ok = grpc_session->send_control_message(message);
  }
  if (!send_ok) {
    error = "send command failed";
    return false;
  }

  const auto channel_text = control::to_string(selected_channel);

  const auto now = control::unix_time_ms_now();
  command_record record;
  record.command_id = command.command_id;
  record.idempotency_key = command.idempotency_key;
  record.command_type = control::to_string(command.type);
  record.status = "DISPATCHED";
  record.channel_type = channel_text;
  record.payload_json = command.payload_json;
  record.result_json = "";
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
          channel_text,
          detail.dump(),
          now,
          db_error)) {
    log::warn("persist command dispatch event failed: {}", db_error);
  }

  return true;
}

size_t online_agent_count() {
  std::lock_guard<std::mutex> lk(g_sessions_mutex);
  std::unordered_set<std::string> online_agents;
  for (auto it = g_wss_agent_sessions.begin(); it != g_wss_agent_sessions.end();) {
    if (it->second.expired()) {
      it = g_wss_agent_sessions.erase(it);
      continue;
    }
    online_agents.insert(it->first);
    ++it;
  }
  for (auto it = g_grpc_agent_sessions.begin(); it != g_grpc_agent_sessions.end();) {
    if (it->second.expired()) {
      it = g_grpc_agent_sessions.erase(it);
      continue;
    }
    online_agents.insert(it->first);
    ++it;
  }
  return online_agents.size();
}

} // namespace service
