#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace control::ports {

struct wol_params {
  std::string mac = "AA:BB:CC:DD:EE:FF";
  std::string broadcast = "192.168.1.255";
  int port = 9;
};

struct ssh_params {
  std::string host = "192.168.1.10";
  int port = 22;
  std::string user = "root";
  std::string password = "password";
  int timeout_ms = 5000;
};

struct control_params {
  wol_params wol;
  ssh_params ssh;
};

struct wakeonlan_request {
  std::string mac;
  std::string broadcast_ip;
  int port = 9;
};

struct wakeonlan_result {
  bool ok = false;
  int bytes_sent = 0;
  std::string error;
};

struct ssh_request {
  std::string host;
  int port = 22;
  std::string user;
  std::string password;
  std::string command;
  int timeout_ms = 5000;
};

struct ssh_result {
  bool ok = false;
  int exit_status = -1;
  std::string output;
  std::string error;
};

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

class i_agent_service_port {
public:
  virtual ~i_agent_service_port() = default;

  virtual control_params load_params() = 0;
  virtual bool save_params(const control_params& params, std::string& error) = 0;
  virtual wakeonlan_result send_magic_packet(const wakeonlan_request& req) = 0;
  virtual ssh_result run_ssh_command(const ssh_request& req) = 0;
  virtual host_probe_snapshot get_host_probe_snapshot() = 0;
  virtual bool is_monitoring_enabled() = 0;
  virtual void set_monitoring_enabled(bool enabled) = 0;
};

} // namespace control::ports
