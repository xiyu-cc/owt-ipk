#pragma once

#include <memory>
#include <string>

namespace server {
class websocket_session;
}

namespace service {

void register_frontend_status_session(const std::shared_ptr<server::websocket_session>& session);
void unregister_frontend_status_session(const server::websocket_session* session);
void broadcast_frontend_status_snapshot(const std::string& reason, const std::string& agent_id);

} // namespace service

