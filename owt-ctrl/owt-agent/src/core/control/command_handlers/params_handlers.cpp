#include "control/command_handlers/handlers.h"

#include "control/ports/json_mappers.h"

namespace control::command_handlers {

command_executor make_params_set_handler(const std::shared_ptr<ports::i_agent_service_port>& service_port) {
  return [service_port](const command&, const nlohmann::json& payload) {
    command_execution_result out;
    if (!service_port) {
      out.status = command_status::failed;
      out.exit_code = -1;
      out.result = nlohmann::json{{"error", "service port unavailable"}};
      return out;
    }

    auto params = service_port->load_params();
    std::string field_error;
    if (!ports::apply_control_params_patch(payload, params, field_error)) {
      out.status = command_status::failed;
      out.exit_code = -1;
      out.result = nlohmann::json{{"error", field_error}};
      return out;
    }

    std::string save_error;
    if (!service_port->save_params(params, save_error)) {
      out.status = command_status::failed;
      out.exit_code = -1;
      out.result = nlohmann::json{{"error", save_error}};
      return out;
    }

    out.status = command_status::succeeded;
    out.exit_code = 0;
    out.result = ports::control_params_to_json(params);
    return out;
  };
}

} // namespace control::command_handlers
