#pragma once

#include "libcore/board_config.hpp"

namespace fancontrol::core {

struct RampAccumulator {
    double stronger_credit = 0.0;
    double weaker_credit = 0.0;
};

int apply_ramp(int current_pwm, int target_pwm, const BoardConfig &cfg, RampAccumulator &accumulator);

} // namespace fancontrol::core
