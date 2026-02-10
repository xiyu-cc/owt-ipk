#include "libcore/pwm_controller.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "libcore/demand_policy.hpp"

namespace fancontrol::core {

int apply_ramp(int current_pwm, int target_pwm, const BoardConfig &cfg, RampAccumulator &accumulator) {
    const int bounded_current = clamp_pwm(cfg, current_pwm);
    const int bounded_target = clamp_pwm(cfg, target_pwm);

    if (bounded_target == bounded_current) {
        accumulator.stronger_credit = 0.0;
        accumulator.weaker_credit = 0.0;
        return bounded_current;
    }

    const int span = std::abs(cfg.pwm_max - cfg.pwm_min);
    if (span <= 0) {
        accumulator.stronger_credit = 0.0;
        accumulator.weaker_credit = 0.0;
        return bounded_current;
    }

    const bool stronger = is_stronger_cooling_pwm(bounded_target, bounded_current, cfg);
    const int ramp_sec = std::max(1, stronger ? cfg.ramp_up : cfg.ramp_down);
    const int interval_sec = std::max(1, cfg.interval_sec);
    const double per_tick = (static_cast<double>(span) * static_cast<double>(interval_sec)) /
                            static_cast<double>(ramp_sec);

    double &credit = stronger ? accumulator.stronger_credit : accumulator.weaker_credit;
    double &other_credit = stronger ? accumulator.weaker_credit : accumulator.stronger_credit;
    credit += per_tick;
    other_credit = 0.0;

    int step = static_cast<int>(std::floor(credit));
    if (step <= 0) {
        return bounded_current;
    }
    credit -= static_cast<double>(step);

    if (step > std::numeric_limits<int>::max() / 2) {
        step = std::numeric_limits<int>::max() / 2;
    }

    if (bounded_target > bounded_current) {
        return clamp_pwm(cfg, std::min(bounded_target, bounded_current + step));
    }
    return clamp_pwm(cfg, std::max(bounded_target, bounded_current - step));
}

} // namespace fancontrol::core
