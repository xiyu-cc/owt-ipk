#include "control/agent_runtime_heartbeat_builder.h"

#include <utility>

namespace control {

agent_runtime_heartbeat_builder::agent_runtime_heartbeat_builder(
    agent_runtime_heartbeat_builder_deps deps)
    : deps_(std::move(deps)) {
  if (!deps_.get_host_probe_snapshot) {
    deps_.get_host_probe_snapshot = []() {
      return service::get_host_probe_snapshot();
    };
  }
  if (!deps_.is_monitoring_enabled) {
    deps_.is_monitoring_enabled = []() {
      return service::is_host_probe_monitoring_enabled();
    };
  }
}

nlohmann::json agent_runtime_heartbeat_builder::build_stats() const {
  const bool monitoring_enabled = deps_.is_monitoring_enabled();
  if (!monitoring_enabled) {
    return nlohmann::json(
        {{"monitoring_enabled", false}, {"status", "paused"}, {"message", "status collection disabled"}});
  }
  auto stats = snapshot_to_json(deps_.get_host_probe_snapshot(), true);
  stats["monitoring_enabled"] = true;
  return stats;
}

nlohmann::json agent_runtime_heartbeat_builder::snapshot_to_json(
    const service::host_probe_snapshot& snap,
    bool monitoring_enabled) {
  return {
      {"status", snap.status},
      {"monitoring_enabled", monitoring_enabled},
      {"message", snap.message},
      {"host", snap.host},
      {"port", snap.port},
      {"user", snap.user},
      {"cpu_usage_percent", snap.has_cpu_usage_percent ? nlohmann::json(snap.cpu_usage_percent) : nlohmann::json(nullptr)},
      {"mem_total_kb", snap.has_mem_total_kb ? nlohmann::json(snap.mem_total_kb) : nlohmann::json(nullptr)},
      {"mem_available_kb", snap.has_mem_available_kb ? nlohmann::json(snap.mem_available_kb) : nlohmann::json(nullptr)},
      {"mem_used_percent", snap.has_mem_used_percent ? nlohmann::json(snap.mem_used_percent) : nlohmann::json(nullptr)},
      {"net_rx_bytes", snap.has_net_rx_bytes ? nlohmann::json(snap.net_rx_bytes) : nlohmann::json(nullptr)},
      {"net_tx_bytes", snap.has_net_tx_bytes ? nlohmann::json(snap.net_tx_bytes) : nlohmann::json(nullptr)},
      {"net_rx_bytes_per_sec",
       snap.has_net_rx_bytes_per_sec ? nlohmann::json(snap.net_rx_bytes_per_sec) : nlohmann::json(nullptr)},
      {"net_tx_bytes_per_sec",
       snap.has_net_tx_bytes_per_sec ? nlohmann::json(snap.net_tx_bytes_per_sec) : nlohmann::json(nullptr)},
      {"sample_interval_ms",
       snap.has_sample_interval_ms ? nlohmann::json(snap.sample_interval_ms) : nlohmann::json(nullptr)},
      {"updated_at_ms", snap.updated_at_ms},
  };
}

} // namespace control
