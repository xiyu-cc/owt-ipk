#pragma once

#include "control/control_protocol.h"

#include <functional>
#include <string>

namespace control {

class agent_runtime_message_router {
public:
  struct handlers {
    std::function<void(const std::string& remote_version)> on_unsupported_protocol;
    std::function<void(const error_payload&)> on_server_error;
    std::function<void(const std::string& reason)> on_invalid_message;
    std::function<void(const envelope& message, const command& cmd)> on_command_dispatch;
  };

  explicit agent_runtime_message_router(std::string local_protocol_version);

  void route(const envelope& message, const handlers& handlers) const;

private:
  std::string local_protocol_version_;
};

} // namespace control
