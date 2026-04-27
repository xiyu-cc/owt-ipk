#pragma once

#include "control/agent_command_executor_registry.h"
#include "control/ports/interfaces.h"

#include <memory>

namespace control::command_handlers {

command_executor make_wol_wake_handler(const std::shared_ptr<ports::i_agent_service_port>& service_port);
command_executor make_host_reboot_handler(const std::shared_ptr<ports::i_agent_service_port>& service_port);
command_executor make_host_poweroff_handler(const std::shared_ptr<ports::i_agent_service_port>& service_port);
command_executor make_monitoring_set_handler(const std::shared_ptr<ports::i_agent_service_port>& service_port);
command_executor make_params_set_handler(const std::shared_ptr<ports::i_agent_service_port>& service_port);

} // namespace control::command_handlers
