#pragma once

#include <memory>

namespace server {
class websocket_session_observer;
}

namespace service {

std::shared_ptr<server::websocket_session_observer> create_control_ws_session_observer();

} // namespace service
