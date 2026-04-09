#pragma once

#include "control/control_protocol.h"

#include <cstddef>
#include <memory>
#include <string>

namespace server {
class websocket_session;
}

namespace service {

class i_control_session {
public:
  virtual ~i_control_session() = default;
  virtual bool send_control_message(const control::envelope& message) = 0;
};

void register_control_session(
    const std::string& agent_id,
    const std::shared_ptr<server::websocket_session>& session);
void unregister_control_session(const std::string& agent_id, const server::websocket_session* session);
void register_grpc_control_session(
    const std::string& agent_id,
    const std::shared_ptr<i_control_session>& session);
void unregister_grpc_control_session(const std::string& agent_id, const i_control_session* session);

bool push_command_to_agent(
    const std::string& agent_id,
    const control::command& command,
    std::string& error);

size_t online_agent_count();

} // namespace service
