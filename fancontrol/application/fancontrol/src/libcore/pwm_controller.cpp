#include "libcore/pwm_controller.hpp"

#include <algorithm>

#include "libcore/demand_policy.hpp"

namespace fancontrol::core {

int apply_ramp(int current_pwm, int target_pwm, const BoardConfig &cfg) {
    if (target_pwm == current_pwm) {
        return current_pwm;
    }

    if (is_stronger_cooling_pwm(target_pwm, current_pwm, cfg)) {
        if (cfg.pwm_inverted) {
            return std::max(target_pwm, current_pwm - cfg.ramp_up);
        }
        return std::min(target_pwm, current_pwm + cfg.ramp_up);
    }

    if (cfg.pwm_inverted) {
        return std::min(target_pwm, current_pwm + cfg.ramp_down);
    }
    return std::max(target_pwm, current_pwm - cfg.ramp_down);
}

} // namespace fancontrol::core
