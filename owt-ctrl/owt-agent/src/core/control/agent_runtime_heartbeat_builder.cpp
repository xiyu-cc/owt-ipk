#include "control/agent_runtime_heartbeat_builder.h"

#include "control/ports/defaults.h"
#include "control/ports/json_mappers.h"

#include <utility>

namespace control {

agent_runtime_heartbeat_builder::agent_runtime_heartbeat_builder(
    std::shared_ptr<ports::i_agent_service_port> service_port)
    : service_port_(std::move(service_port)) {
  if (!service_port_) {
    service_port_ = ports::make_default_agent_service_port();
  }
}

nlohmann::json agent_runtime_heartbeat_builder::build_stats() const {
  if (!service_port_) {
    return nlohmann::json(
        {{"monitoring_enabled", false}, {"status", "offline"}, {"message", "service port unavailable"}});
  }

  const bool monitoring_enabled = service_port_->is_monitoring_enabled();
  if (!monitoring_enabled) {
    return nlohmann::json(
        {{"monitoring_enabled", false}, {"status", "paused"}, {"message", "status collection disabled"}});
  }
  auto stats = ports::host_probe_snapshot_to_json(service_port_->get_host_probe_snapshot(), true);
  stats["monitoring_enabled"] = true;
  return stats;
}

} // namespace control
