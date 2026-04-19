#pragma once

#include "hub_api.h"
#include "message.h"

#include <string_view>

namespace ws_deal {

class handler {
public:
  virtual ~handler() = default;

  virtual void on_join(ws_hub_api &hub, std::string_view session_id) {
    (void)hub;
    (void)session_id;
  }

  virtual void on_join_route(
      ws_hub_api &hub,
      std::string_view session_id,
      std::string_view route) {
    (void)route;
    on_join(hub, session_id);
  }

  virtual void on_leave(ws_hub_api &hub, std::string_view session_id) {
    (void)hub;
    (void)session_id;
  }

  virtual void on_leave_route(
      ws_hub_api &hub,
      std::string_view session_id,
      std::string_view route) {
    (void)route;
    on_leave(hub, session_id);
  }

  virtual void on_message(ws_hub_api &hub, inbound_message message) {
    (void)hub;
    (void)message;
  }
};

} // namespace ws_deal
