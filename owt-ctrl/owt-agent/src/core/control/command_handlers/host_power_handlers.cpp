#include "control/command_handlers/handlers.h"

#include "control/command_handlers/handler_utils.h"

#include <string_view>

namespace control::command_handlers {

namespace {

command_executor make_ssh_action_handler(
    const std::shared_ptr<ports::i_agent_service_port>& service_port,
    std::string_view action) {
  return [service_port, action](const command&, const nlohmann::json& payload) {
    command_execution_result out;
    if (!service_port) {
      out.status = command_status::failed;
      out.exit_code = -1;
      out.result = nlohmann::json{{"error", "service port unavailable"}};
      return out;
    }

    const auto params = service_port->load_params();
    ports::ssh_request req;
    req.host = payload_value_or<std::string>(payload, "host", params.ssh.host);
    req.port = payload_value_or<int>(payload, "port", params.ssh.port);
    req.user = payload_value_or<std::string>(payload, "user", params.ssh.user);
    req.password = payload_value_or<std::string>(payload, "password", params.ssh.password);
    req.timeout_ms = payload_value_or<int>(payload, "timeout_ms", params.ssh.timeout_ms);
    req.command = std::string(action);

    const auto res = service_port->run_ssh_command(req);
    out.status = res.ok ? command_status::succeeded : command_status::failed;
    out.exit_code = res.exit_status;
    out.result = {
        {"ok", res.ok},
        {"error", res.error},
        {"output", res.output},
        {"exit_status", res.exit_status},
        {"host", req.host},
        {"port", req.port},
        {"user", req.user},
        {"command", req.command},
    };
    return out;
  };
}

} // namespace

command_executor make_host_reboot_handler(const std::shared_ptr<ports::i_agent_service_port>& service_port) {
  return make_ssh_action_handler(service_port, "reboot");
}

command_executor make_host_poweroff_handler(const std::shared_ptr<ports::i_agent_service_port>& service_port) {
  return make_ssh_action_handler(service_port, "poweroff");
}

} // namespace control::command_handlers
