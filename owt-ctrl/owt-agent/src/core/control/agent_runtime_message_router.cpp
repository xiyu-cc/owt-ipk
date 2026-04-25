#include "control/agent_runtime_message_router.h"

#include <utility>

namespace control {

agent_runtime_message_router::agent_runtime_message_router(std::string local_protocol_version)
    : local_protocol_version_(std::move(local_protocol_version)) {}

void agent_runtime_message_router::route(const envelope& message, const handlers& handlers) const {
  if (message.version != local_protocol_version_) {
    if (handlers.on_unsupported_protocol) {
      handlers.on_unsupported_protocol(message.version);
    }
    return;
  }

  if (message.type == message_type::server_error) {
    const auto* payload = std::get_if<error_payload>(&message.payload);
    if (payload == nullptr) {
      if (handlers.on_invalid_message) {
        handlers.on_invalid_message("server.error payload missing");
      }
      return;
    }
    if (handlers.on_server_error) {
      handlers.on_server_error(*payload);
    }
    return;
  }

  if (message.type != message_type::server_command_dispatch) {
    return;
  }

  const auto* cmd = std::get_if<command>(&message.payload);
  if (cmd == nullptr) {
    if (handlers.on_invalid_message) {
      handlers.on_invalid_message("server.command.dispatch payload missing command body");
    }
    return;
  }

  if (handlers.on_command_dispatch) {
    handlers.on_command_dispatch(message.id, *cmd);
  }
}

} // namespace control
