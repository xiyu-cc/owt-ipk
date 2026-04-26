#pragma once

#include "control/agent_command_executor_registry.h"
#include "control/ports/interfaces.h"

#include <memory>

namespace control {

std::shared_ptr<agent_command_executor_registry> make_default_command_executor_registry(
    std::shared_ptr<ports::i_agent_service_port> service_port = {});

void install_default_command_executors(
    agent_command_executor_registry& registry,
    std::shared_ptr<ports::i_agent_service_port> service_port = {});

} // namespace control
