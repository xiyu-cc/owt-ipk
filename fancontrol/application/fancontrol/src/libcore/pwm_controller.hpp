#pragma once

#include "libcore/board_config.hpp"

namespace fancontrol::core {

int apply_ramp(int current_pwm, int target_pwm, const BoardConfig &cfg);

} // namespace fancontrol::core
