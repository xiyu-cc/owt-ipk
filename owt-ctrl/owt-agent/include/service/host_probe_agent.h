#pragma once

#include <cstdint>
#include <string>

namespace service {

struct host_probe_snapshot {
  std::string status = "unknown";
  std::string message = "agent not started";
  std::string host;
  int port = 22;
  std::string user;

  bool has_cpu_usage_percent = false;
  double cpu_usage_percent = 0.0;

  bool has_mem_total_kb = false;
  uint64_t mem_total_kb = 0;
  bool has_mem_available_kb = false;
  uint64_t mem_available_kb = 0;
  bool has_mem_used_percent = false;
  double mem_used_percent = 0.0;

  bool has_net_rx_bytes = false;
  uint64_t net_rx_bytes = 0;
  bool has_net_tx_bytes = false;
  uint64_t net_tx_bytes = 0;
  bool has_net_rx_bytes_per_sec = false;
  double net_rx_bytes_per_sec = 0.0;
  bool has_net_tx_bytes_per_sec = false;
  double net_tx_bytes_per_sec = 0.0;

  bool has_sample_interval_ms = false;
  int sample_interval_ms = 0;
  int64_t updated_at_ms = 0;
};

void start_host_probe_agent(int status_collect_interval_ms);
void stop_host_probe_agent();
host_probe_snapshot get_host_probe_snapshot();
bool is_host_probe_monitoring_enabled();
void set_host_probe_monitoring_enabled(bool enabled);

} // namespace service
