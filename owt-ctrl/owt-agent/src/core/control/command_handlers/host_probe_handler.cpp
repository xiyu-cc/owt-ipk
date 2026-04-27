#include "control/command_handlers/handlers.h"

#include "control/ports/json_mappers.h"

namespace control::command_handlers {

command_executor make_monitoring_set_handler(const std::shared_ptr<ports::i_agent_service_port>& service_port) {
  return [service_port](const command&, const nlohmann::json& payload) {
    command_execution_result out;
    if (!service_port) {
      out.status = command_status::failed;
      out.exit_code = -1;
      out.result = nlohmann::json{{"error", "service port unavailable"}};
      return out;
    }
    if (!payload.contains("enabled") || !payload["enabled"].is_boolean()) {
      out.status = command_status::failed;
      out.exit_code = -1;
      out.result = nlohmann::json{{"error", "field enabled must be boolean"}};
      return out;
    }

    service_port->set_monitoring_enabled(payload["enabled"].get<bool>());
    out.status = command_status::succeeded;
    out.exit_code = 0;
    out.result = nlohmann::json{{"enabled", service_port->is_monitoring_enabled()}};
    return out;
  };
}

} // namespace control::command_handlers
