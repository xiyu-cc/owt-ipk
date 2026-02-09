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

int apply_startup_boost(const BoardConfig &cfg, int target_pwm, int current_pwm) {
    if (cfg.pwm_startup_pwm < 0) {
        return target_pwm;
    }

    const int startup_pwm = clamp_pwm(cfg, cfg.pwm_startup_pwm);
    const int idle_pwm = min_cooling_pwm(cfg);

    const bool requesting_active_cooling = is_stronger_cooling_pwm(target_pwm, idle_pwm, cfg);
    const bool startup_stronger_than_target = is_stronger_cooling_pwm(startup_pwm, target_pwm, cfg);
    const bool current_weaker_than_startup = is_stronger_cooling_pwm(startup_pwm, current_pwm, cfg);

    if (requesting_active_cooling && startup_stronger_than_target && current_weaker_than_startup) {
        return startup_pwm;
    }
    return target_pwm;
}

} // namespace fancontrol::core
