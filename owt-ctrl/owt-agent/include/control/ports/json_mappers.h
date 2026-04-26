#pragma once

#include "control/ports/interfaces.h"

#include <nlohmann/json.hpp>

#include <string>

namespace control::ports {

nlohmann::json control_params_to_json(const control_params& params);
nlohmann::json host_probe_snapshot_to_json(
    const host_probe_snapshot& snap,
    bool monitoring_enabled);
bool apply_control_params_patch(
    const nlohmann::json& patch,
    control_params& params,
    std::string& error);

} // namespace control::ports
