#include "service/frontend_status_ws_session_observer.h"

#include "server/websocket_session.h"
#include "server/websocket_session_observer.h"
#include "service/frontend_status_stream.h"

#include <memory>
#include <string>

namespace service {

namespace {

class frontend_status_ws_session_observer final : public server::websocket_session_observer {
public:
  void on_session_open(const std::shared_ptr<server::websocket_session>& session) override {
    service::register_frontend_status_session(session);
  }

  void on_text_message(
      const std::shared_ptr<server::websocket_session>& session,
      const std::string& text) override {
    (void)session;
    (void)text;
  }

  void on_session_closed(const std::shared_ptr<server::websocket_session>& session) override {
    service::unregister_frontend_status_session(session.get());
  }
};

} // namespace

std::shared_ptr<server::websocket_session_observer> create_frontend_status_ws_session_observer() {
  return std::make_shared<frontend_status_ws_session_observer>();
}

} // namespace service

