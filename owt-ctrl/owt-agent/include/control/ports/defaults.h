#pragma once

#include "control/ports/interfaces.h"

#include <memory>

namespace control::ports {

std::shared_ptr<i_agent_service_port> make_default_agent_service_port();

} // namespace control::ports
