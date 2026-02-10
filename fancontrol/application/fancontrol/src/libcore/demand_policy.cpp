#include "libcore/demand_policy.hpp"

#include <algorithm>
#include <cmath>

namespace fancontrol::core {

int min_cooling_pwm(const BoardConfig &cfg) {
    return cfg.pwm_min;
}

int max_cooling_pwm(const BoardConfig &cfg) {
    return cfg.pwm_max;
}

bool is_stronger_cooling_pwm(int candidate, int baseline, const BoardConfig &cfg) {
    const int bounded_candidate = clamp_pwm(cfg, candidate);
    const int bounded_baseline = clamp_pwm(cfg, baseline);
    const int span = cfg.pwm_max - cfg.pwm_min;
    if (span == 0) {
        return false;
    }
    const int dir = (span > 0) ? 1 : -1;
    const long long cand_level = static_cast<long long>(bounded_candidate - cfg.pwm_min) * dir;
    const long long base_level = static_cast<long long>(bounded_baseline - cfg.pwm_min) * dir;
    return cand_level > base_level;
}

int stronger_cooling_pwm(int lhs, int rhs, const BoardConfig &cfg) {
    return is_stronger_cooling_pwm(rhs, lhs, cfg) ? rhs : lhs;
}

int clamp_pwm(const BoardConfig &cfg, int pwm) {
    const int lo = std::min(cfg.pwm_min, cfg.pwm_max);
    const int hi = std::max(cfg.pwm_min, cfg.pwm_max);
    return std::clamp(pwm, lo, hi);
}

int demand_from_source(const BoardConfig &cfg,
                       const BoardSourceConfig &src,
                       int temp_mC,
                       bool &active,
                       bool &critical) {
    critical = false;

    const int idle_pwm = min_cooling_pwm(cfg);
    const int full_pwm = max_cooling_pwm(cfg);

    if (temp_mC >= src.t_crit_mC) {
        critical = true;
        active = true;
        return full_pwm;
    }

    const int on_threshold = src.t_start_mC + cfg.hysteresis_mC;
    const int off_threshold = src.t_start_mC - cfg.hysteresis_mC;

    if (!active) {
        if (temp_mC < on_threshold) {
            return idle_pwm;
        }
        active = true;
    } else {
        if (temp_mC <= off_threshold) {
            active = false;
            return idle_pwm;
        }
    }

    double ratio = 0.0;
    if (temp_mC <= src.t_start_mC) {
        ratio = 0.0;
    } else if (temp_mC >= src.t_full_mC) {
        ratio = 1.0;
    } else {
        ratio = static_cast<double>(temp_mC - src.t_start_mC) /
                static_cast<double>(src.t_full_mC - src.t_start_mC);
    }

    ratio *= static_cast<double>(src.weight) / 100.0;
    ratio = std::clamp(ratio, 0.0, 1.0);

    const int span = cfg.pwm_max - cfg.pwm_min;
    const int demand = cfg.pwm_min + static_cast<int>(std::lround(ratio * static_cast<double>(span)));
    return clamp_pwm(cfg, demand);
}

} // namespace fancontrol::core
