#include "control/command_handlers/handlers.h"

#include "control/command_handlers/handler_utils.h"

namespace control::command_handlers {

command_executor make_wol_wake_handler(const std::shared_ptr<ports::i_agent_service_port>& service_port) {
  return [service_port](const command&, const nlohmann::json& payload) {
    command_execution_result out;
    if (!service_port) {
      out.status = command_status::failed;
      out.exit_code = -1;
      out.result = nlohmann::json{{"error", "service port unavailable"}};
      return out;
    }

    const auto params = service_port->load_params();
    ports::wakeonlan_request req;
    req.mac = payload_value_or<std::string>(payload, "mac", params.wol.mac);
    req.broadcast_ip = payload_value_or<std::string>(payload, "broadcast", params.wol.broadcast);
    req.port = payload_value_or<int>(payload, "port", params.wol.port);

    const auto res = service_port->send_magic_packet(req);
    out.status = res.ok ? command_status::succeeded : command_status::failed;
    out.exit_code = res.ok ? 0 : -1;
    out.result = {
        {"ok", res.ok},
        {"error", res.error},
        {"bytes_sent", res.bytes_sent},
        {"mac", req.mac},
        {"broadcast", req.broadcast_ip},
        {"port", req.port},
    };
    return out;
  };
}

} // namespace control::command_handlers
