#include "control/default_command_executors.h"

#include "control/command_handlers/handlers.h"
#include "control/ports/defaults.h"

#include <memory>

namespace control {

namespace {

std::shared_ptr<ports::i_agent_service_port> resolve_service_port(
    std::shared_ptr<ports::i_agent_service_port> service_port) {
  if (service_port) {
    return service_port;
  }
  return ports::make_default_agent_service_port();
}

} // namespace

std::shared_ptr<agent_command_executor_registry> make_default_command_executor_registry(
    std::shared_ptr<ports::i_agent_service_port> service_port) {
  auto registry = std::make_shared<agent_command_executor_registry>();
  install_default_command_executors(*registry, std::move(service_port));
  return registry;
}

void install_default_command_executors(
    agent_command_executor_registry& registry,
    std::shared_ptr<ports::i_agent_service_port> service_port) {
  registry.clear();
  const auto resolved = resolve_service_port(std::move(service_port));
  registry.register_executor(command_type::wol_wake, command_handlers::make_wol_wake_handler(resolved));
  registry.register_executor(command_type::host_reboot, command_handlers::make_host_reboot_handler(resolved));
  registry.register_executor(command_type::host_poweroff, command_handlers::make_host_poweroff_handler(resolved));
  registry.register_executor(command_type::monitoring_set, command_handlers::make_monitoring_set_handler(resolved));
  registry.register_executor(command_type::params_set, command_handlers::make_params_set_handler(resolved));
}

} // namespace control
