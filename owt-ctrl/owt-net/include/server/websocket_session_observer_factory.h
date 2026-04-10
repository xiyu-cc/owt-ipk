#pragma once

#include "server/websocket_session_observer.h"

#include <functional>
#include <memory>
#include <string>

namespace server {

using websocket_session_observer_factory =
    std::function<std::shared_ptr<websocket_session_observer>(const std::string& path)>;

void set_websocket_session_observer_factory(websocket_session_observer_factory factory);
std::shared_ptr<websocket_session_observer> create_websocket_session_observer(const std::string& path);

} // namespace server
