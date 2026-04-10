#pragma once

#include <memory>
#include <string>

namespace server {

class websocket_session;

class websocket_session_observer {
public:
  virtual ~websocket_session_observer() = default;

  virtual void on_session_open(const std::shared_ptr<websocket_session>& session) {
    (void)session;
  }

  virtual void on_text_message(
      const std::shared_ptr<websocket_session>& session,
      const std::string& text) = 0;

  virtual void on_session_closed(const std::shared_ptr<websocket_session>& session) {
    (void)session;
  }
};

} // namespace server
