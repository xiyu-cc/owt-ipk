#pragma once

#include "control/i_control_channel.h"

#include <memory>

namespace control {

std::unique_ptr<i_control_channel> make_default_control_channel();

} // namespace control
