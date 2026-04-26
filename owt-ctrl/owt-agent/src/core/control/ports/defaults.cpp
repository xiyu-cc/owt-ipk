#include "control/ports/defaults.h"

#include "service/host_probe_agent.h"
#include "service/params_store.h"
#include "service/ssh_executor.h"
#include "service/wakeonlan_sender.h"

#include <memory>
#include <utility>

namespace control::ports {

namespace {

control_params to_control_params(const service::control_params& params) {
  control_params out;
  out.wol.mac = params.wol.mac;
  out.wol.broadcast = params.wol.broadcast;
  out.wol.port = params.wol.port;
  out.ssh.host = params.ssh.host;
  out.ssh.port = params.ssh.port;
  out.ssh.user = params.ssh.user;
  out.ssh.password = params.ssh.password;
  out.ssh.timeout_ms = params.ssh.timeout_ms;
  return out;
}

service::control_params to_service_params(const control_params& params) {
  service::control_params out;
  out.wol.mac = params.wol.mac;
  out.wol.broadcast = params.wol.broadcast;
  out.wol.port = params.wol.port;
  out.ssh.host = params.ssh.host;
  out.ssh.port = params.ssh.port;
  out.ssh.user = params.ssh.user;
  out.ssh.password = params.ssh.password;
  out.ssh.timeout_ms = params.ssh.timeout_ms;
  return out;
}

wakeonlan_result to_wakeonlan_result(const service::wakeonlan_result& result) {
  wakeonlan_result out;
  out.ok = result.ok;
  out.bytes_sent = result.bytes_sent;
  out.error = result.error;
  return out;
}

service::wakeonlan_request to_service_wakeonlan_request(const wakeonlan_request& req) {
  service::wakeonlan_request out;
  out.mac = req.mac;
  out.broadcast_ip = req.broadcast_ip;
  out.port = req.port;
  return out;
}

ssh_result to_ssh_result(const service::ssh_result& result) {
  ssh_result out;
  out.ok = result.ok;
  out.exit_status = result.exit_status;
  out.output = result.output;
  out.error = result.error;
  return out;
}

service::ssh_request to_service_ssh_request(const ssh_request& req) {
  service::ssh_request out;
  out.host = req.host;
  out.port = req.port;
  out.user = req.user;
  out.password = req.password;
  out.command = req.command;
  out.timeout_ms = req.timeout_ms;
  return out;
}

host_probe_snapshot to_host_probe_snapshot(const service::host_probe_snapshot& snap) {
  host_probe_snapshot out;
  out.status = snap.status;
  out.message = snap.message;
  out.host = snap.host;
  out.port = snap.port;
  out.user = snap.user;
  out.has_cpu_usage_percent = snap.has_cpu_usage_percent;
  out.cpu_usage_percent = snap.cpu_usage_percent;
  out.has_mem_total_kb = snap.has_mem_total_kb;
  out.mem_total_kb = snap.mem_total_kb;
  out.has_mem_available_kb = snap.has_mem_available_kb;
  out.mem_available_kb = snap.mem_available_kb;
  out.has_mem_used_percent = snap.has_mem_used_percent;
  out.mem_used_percent = snap.mem_used_percent;
  out.has_net_rx_bytes = snap.has_net_rx_bytes;
  out.net_rx_bytes = snap.net_rx_bytes;
  out.has_net_tx_bytes = snap.has_net_tx_bytes;
  out.net_tx_bytes = snap.net_tx_bytes;
  out.has_net_rx_bytes_per_sec = snap.has_net_rx_bytes_per_sec;
  out.net_rx_bytes_per_sec = snap.net_rx_bytes_per_sec;
  out.has_net_tx_bytes_per_sec = snap.has_net_tx_bytes_per_sec;
  out.net_tx_bytes_per_sec = snap.net_tx_bytes_per_sec;
  out.has_sample_interval_ms = snap.has_sample_interval_ms;
  out.sample_interval_ms = snap.sample_interval_ms;
  out.updated_at_ms = snap.updated_at_ms;
  return out;
}

class service_agent_service_port final : public i_agent_service_port {
public:
  control_params load_params() override {
    return to_control_params(service::load_control_params());
  }

  bool save_params(const control_params& params, std::string& error) override {
    return service::save_control_params(to_service_params(params), error);
  }

  wakeonlan_result send_magic_packet(const wakeonlan_request& req) override {
    return to_wakeonlan_result(service::send_magic_packet(to_service_wakeonlan_request(req)));
  }

  ssh_result run_ssh_command(const ssh_request& req) override {
    return to_ssh_result(service::run_ssh_command(to_service_ssh_request(req)));
  }

  host_probe_snapshot get_host_probe_snapshot() override {
    return to_host_probe_snapshot(service::get_host_probe_snapshot());
  }

  bool is_monitoring_enabled() override {
    return service::is_host_probe_monitoring_enabled();
  }

  void set_monitoring_enabled(bool enabled) override {
    service::set_host_probe_monitoring_enabled(enabled);
  }
};

} // namespace

std::shared_ptr<i_agent_service_port> make_default_agent_service_port() {
  return std::make_shared<service_agent_service_port>();
}

} // namespace control::ports
