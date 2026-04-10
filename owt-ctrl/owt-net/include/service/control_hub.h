#pragma once

#include "control/control_protocol.h"

#include <cstdint>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace server {
class websocket_session;
}

namespace service {

struct agent_runtime_state {
  std::string agent_id;
  std::string site_id;
  std::string agent_version;
  std::vector<std::string> capabilities;
  bool online = false;
  int64_t registered_at_ms = 0;
  int64_t last_heartbeat_at_ms = 0;
  int64_t last_seen_at_ms = 0;
  std::string stats_json;
};

void register_control_session(
    const std::string& agent_id,
    const std::shared_ptr<server::websocket_session>& session);
void unregister_control_session(const std::string& agent_id, const server::websocket_session* session);
void update_agent_register_state(const control::register_payload& payload, int64_t received_at_ms);
void update_agent_heartbeat_state(
    const std::string& agent_id,
    const control::heartbeat_payload* payload,
    int64_t received_at_ms);
void bootstrap_agent_runtime_states();
void list_agent_runtime_states(std::vector<agent_runtime_state>& out, bool include_offline);
bool get_agent_runtime_state(const std::string& agent_id, agent_runtime_state& out);

bool push_command_to_agent(
    const std::string& agent_id,
    const control::command& command,
    std::string& error);

size_t online_agent_count();

} // namespace service
