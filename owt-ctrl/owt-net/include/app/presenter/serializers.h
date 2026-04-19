#pragma once

#include "ctrl/domain/types.h"

#include <nlohmann/json.hpp>

namespace app::presenter {

nlohmann::json to_agent_json(const ctrl::domain::AgentState& state);
nlohmann::json to_command_json(const ctrl::domain::CommandSnapshot& row);
nlohmann::json to_command_event_json(const ctrl::domain::CommandEvent& event);
nlohmann::json to_audit_json(const ctrl::domain::AuditEntry& row);

nlohmann::json to_command_event_notification(
    const ctrl::domain::CommandSnapshot& command,
    const ctrl::domain::CommandEvent& event);

} // namespace app::presenter
