#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "libcore/board_config.hpp"
#include "libcore/temp_source.hpp"

namespace fancontrol::core {

struct SourceTelemetry {
    std::string id;
    bool has_polled = false;
    bool ok = false;
    bool stale = false;
    bool using_last_good = false;
    bool active = false;
    bool critical = false;
    int temp_mC = 0;
    int age_sec = 0;
    int ttl_sec = 0;
    int demand_pwm = 0;
    std::string error;
};

struct TargetDecision {
    int target_pwm = 0;
    bool any_valid = false;
    bool any_timeout = false;
    bool critical = false;
};

TargetDecision compute_target_decision(const BoardConfig &cfg,
                                       const SourceManager &mgr,
                                       const std::unordered_map<std::string, BoardSourceConfig> &by_id,
                                       std::unordered_map<std::string, bool> &active_state,
                                       std::vector<SourceTelemetry> &telemetry);

std::string build_runtime_status_json(const BoardConfig &cfg,
                                      const TargetDecision &decision,
                                      int current_pwm,
                                      int target_pwm,
                                      int applied_pwm,
                                      const std::vector<SourceTelemetry> &telemetry);

bool write_runtime_status_file(const std::string &path, const std::string &payload);

} // namespace fancontrol::core
