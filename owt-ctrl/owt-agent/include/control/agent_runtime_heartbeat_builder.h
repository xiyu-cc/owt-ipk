#pragma once

#include "service/host_probe_agent.h"

#include <functional>

#include <nlohmann/json.hpp>

namespace control {

struct agent_runtime_heartbeat_builder_deps {
  std::function<service::host_probe_snapshot()> get_host_probe_snapshot;
  std::function<bool()> is_monitoring_enabled;
};

class agent_runtime_heartbeat_builder {
public:
  explicit agent_runtime_heartbeat_builder(agent_runtime_heartbeat_builder_deps deps = {});

  nlohmann::json build_stats() const;

private:
  static nlohmann::json snapshot_to_json(
      const service::host_probe_snapshot& snap,
      bool monitoring_enabled);

  agent_runtime_heartbeat_builder_deps deps_;
};

} // namespace control
