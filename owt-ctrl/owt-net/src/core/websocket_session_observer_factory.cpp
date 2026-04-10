#include "server/websocket_session_observer_factory.h"

#include <mutex>
#include <utility>

namespace server {

namespace {

std::mutex g_factory_mutex;
websocket_session_observer_factory g_factory;

} // namespace

void set_websocket_session_observer_factory(websocket_session_observer_factory factory) {
  std::lock_guard<std::mutex> lk(g_factory_mutex);
  g_factory = std::move(factory);
}

std::shared_ptr<websocket_session_observer> create_websocket_session_observer(
    const std::string& path) {
  websocket_session_observer_factory factory;
  {
    std::lock_guard<std::mutex> lk(g_factory_mutex);
    factory = g_factory;
  }
  if (!factory) {
    return nullptr;
  }
  return factory(path);
}

} // namespace server
