#pragma once

#include "libcore/board_config.hpp"

namespace fancontrol::core {

int min_cooling_pwm(const BoardConfig &cfg);
int max_cooling_pwm(const BoardConfig &cfg);
bool is_stronger_cooling_pwm(int candidate, int baseline, const BoardConfig &cfg);
int stronger_cooling_pwm(int lhs, int rhs, const BoardConfig &cfg);
int clamp_pwm(const BoardConfig &cfg, int pwm);

int demand_from_source(const BoardConfig &cfg,
                       const BoardSourceConfig &src,
                       int temp_mC,
                       bool &active,
                       bool &critical);

} // namespace fancontrol::core
