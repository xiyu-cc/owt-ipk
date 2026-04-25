#pragma once

#include "app/ws/command_bus_protocol.h"
#include "ctrl/ports/interfaces.h"

#include <drogon/WebSocketConnection.h>

#include <string_view>

namespace app::bootstrap::runtime {

enum class WsPeer {
  Ui,
  Agent,
};

bool parse_and_validate_action_envelope(
    WsPeer peer,
    std::string_view text,
    const ctrl::ports::IClock& clock,
    const drogon::WebSocketConnectionPtr& conn,
    ws::BusEnvelope& out);

} // namespace app::bootstrap::runtime
