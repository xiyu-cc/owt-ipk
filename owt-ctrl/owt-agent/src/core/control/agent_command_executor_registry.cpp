#include "control/agent_command_executor_registry.h"

#include <cstdint>
#include <exception>
#include <limits>
#include <string>
#include <utility>

namespace control {

namespace {

template <typename T>
T payload_value_or(const nlohmann::json& payload, const char* key, const T& fallback) {
  if (!payload.is_object()) {
    return fallback;
  }
  const auto it = payload.find(key);
  if (it == payload.end() || it->is_null()) {
    return fallback;
  }
  try {
    return it->get<T>();
  } catch (const std::exception&) {
    return fallback;
  }
}

bool update_int_field(
    const nlohmann::json& j,
    const char* key,
    int min_value,
    int max_value,
    int& target,
    std::string& error) {
  if (!j.contains(key)) {
    return true;
  }
  if (!j[key].is_number_integer()) {
    error = std::string("field ") + key + " must be integer";
    return false;
  }
  int64_t parsed = 0;
  try {
    parsed = j[key].get<int64_t>();
  } catch (const std::exception&) {
    error = std::string("field ") + key + " must be integer";
    return false;
  }
  if (parsed < static_cast<int64_t>(std::numeric_limits<int>::min()) ||
      parsed > static_cast<int64_t>(std::numeric_limits<int>::max())) {
    error = std::string("field ") + key + " out of range";
    return false;
  }
  const int value = static_cast<int>(parsed);
  if (value < min_value || value > max_value) {
    error = std::string("field ") + key + " out of range";
    return false;
  }
  target = value;
  return true;
}

bool update_string_field(
    const nlohmann::json& j,
    const char* key,
    std::string& target,
    std::string& error) {
  if (!j.contains(key)) {
    return true;
  }
  if (!j[key].is_string()) {
    error = std::string("field ") + key + " must be string";
    return false;
  }
  target = j[key].get<std::string>();
  return true;
}

nlohmann::json params_to_json(const service::control_params& params) {
  return {
      {"wol",
       {
           {"mac", params.wol.mac},
           {"broadcast", params.wol.broadcast},
           {"port", params.wol.port},
       }},
      {"ssh",
       {
           {"host", params.ssh.host},
           {"port", params.ssh.port},
           {"user", params.ssh.user},
           {"password", params.ssh.password},
           {"timeout_ms", params.ssh.timeout_ms},
       }},
  };
}

nlohmann::json probe_snapshot_to_json(
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

} // namespace

agent_command_executor_registry::agent_command_executor_registry(agent_command_executor_deps deps)
    : deps_(std::move(deps)) {
  if (!deps_.load_params) {
    deps_.load_params = []() { return service::load_control_params(); };
  }
  if (!deps_.save_params) {
    deps_.save_params = [](const service::control_params& params, std::string& error) {
      return service::save_control_params(params, error);
    };
  }
  if (!deps_.send_magic_packet) {
    deps_.send_magic_packet = [](const service::wakeonlan_request& req) {
      return service::send_magic_packet(req);
    };
  }
  if (!deps_.run_ssh_command) {
    deps_.run_ssh_command = [](const service::ssh_request& req) {
      return service::run_ssh_command(req);
    };
  }
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
  if (!deps_.set_monitoring_enabled) {
    deps_.set_monitoring_enabled = [](bool enabled) {
      service::set_host_probe_monitoring_enabled(enabled);
    };
  }

  install_defaults();
}

void agent_command_executor_registry::register_executor(command_type type, command_executor executor) {
  if (!executor) {
    executors_.erase(type);
    return;
  }
  executors_[type] = std::move(executor);
}

bool agent_command_executor_registry::execute(
    const command& cmd,
    const nlohmann::json& payload,
    command_execution_result& out) const {
  const auto it = executors_.find(cmd.type);
  if (it == executors_.end() || !it->second) {
    return false;
  }
  out = it->second(cmd, payload);
  return true;
}

void agent_command_executor_registry::install_defaults() {
  executors_.clear();

  register_executor(command_type::wol_wake, [this](const command&, const nlohmann::json& payload) {
    command_execution_result out;
    auto params = deps_.load_params();
    service::wakeonlan_request req;
    req.mac = payload_value_or<std::string>(payload, "mac", params.wol.mac);
    req.broadcast_ip = payload_value_or<std::string>(payload, "broadcast", params.wol.broadcast);
    req.port = payload_value_or<int>(payload, "port", params.wol.port);

    const auto res = deps_.send_magic_packet(req);
    out.status = res.ok ? command_status::succeeded : command_status::failed;
    out.exit_code = res.ok ? 0 : -1;
    out.result = {
        {"ok", res.ok},
        {"error", res.error},
        {"bytes_sent", res.bytes_sent},
        {"mac", req.mac},
        {"broadcast", req.broadcast_ip},
        {"port", req.port},
    };
    return out;
  });

  register_executor(command_type::host_reboot, [this](const command&, const nlohmann::json& payload) {
    command_execution_result out;
    auto params = deps_.load_params();
    service::ssh_request req;
    req.host = payload_value_or<std::string>(payload, "host", params.ssh.host);
    req.port = payload_value_or<int>(payload, "port", params.ssh.port);
    req.user = payload_value_or<std::string>(payload, "user", params.ssh.user);
    req.password = payload_value_or<std::string>(payload, "password", params.ssh.password);
    req.timeout_ms = payload_value_or<int>(payload, "timeout_ms", params.ssh.timeout_ms);
    req.command = "reboot";

    const auto res = deps_.run_ssh_command(req);
    out.status = res.ok ? command_status::succeeded : command_status::failed;
    out.exit_code = res.exit_status;
    out.result = {
        {"ok", res.ok},
        {"error", res.error},
        {"output", res.output},
        {"exit_status", res.exit_status},
        {"host", req.host},
        {"port", req.port},
        {"user", req.user},
        {"command", req.command},
    };
    return out;
  });

  register_executor(command_type::host_poweroff, [this](const command&, const nlohmann::json& payload) {
    command_execution_result out;
    auto params = deps_.load_params();
    service::ssh_request req;
    req.host = payload_value_or<std::string>(payload, "host", params.ssh.host);
    req.port = payload_value_or<int>(payload, "port", params.ssh.port);
    req.user = payload_value_or<std::string>(payload, "user", params.ssh.user);
    req.password = payload_value_or<std::string>(payload, "password", params.ssh.password);
    req.timeout_ms = payload_value_or<int>(payload, "timeout_ms", params.ssh.timeout_ms);
    req.command = "poweroff";

    const auto res = deps_.run_ssh_command(req);
    out.status = res.ok ? command_status::succeeded : command_status::failed;
    out.exit_code = res.exit_status;
    out.result = {
        {"ok", res.ok},
        {"error", res.error},
        {"output", res.output},
        {"exit_status", res.exit_status},
        {"host", req.host},
        {"port", req.port},
        {"user", req.user},
        {"command", req.command},
    };
    return out;
  });

  register_executor(command_type::host_probe_get, [this](const command&, const nlohmann::json&) {
    command_execution_result out;
    out.status = command_status::succeeded;
    out.exit_code = 0;
    out.result = probe_snapshot_to_json(deps_.get_host_probe_snapshot(), deps_.is_monitoring_enabled());
    return out;
  });

  register_executor(command_type::monitoring_set, [this](const command&, const nlohmann::json& payload) {
    command_execution_result out;
    if (!payload.contains("enabled") || !payload["enabled"].is_boolean()) {
      out.status = command_status::failed;
      out.exit_code = -1;
      out.result = {{"error", "field enabled must be boolean"}};
      return out;
    }

    deps_.set_monitoring_enabled(payload["enabled"].get<bool>());
    out.status = command_status::succeeded;
    out.exit_code = 0;
    out.result = {{"enabled", deps_.is_monitoring_enabled()}};
    return out;
  });

  register_executor(command_type::params_get, [this](const command&, const nlohmann::json&) {
    command_execution_result out;
    out.status = command_status::succeeded;
    out.exit_code = 0;
    out.result = params_to_json(deps_.load_params());
    return out;
  });

  register_executor(command_type::params_set, [this](const command&, const nlohmann::json& payload) {
    command_execution_result out;

    auto params = deps_.load_params();
    std::string field_error;
    bool ok = true;

    if (payload.contains("wol")) {
      if (!payload["wol"].is_object()) {
        ok = false;
        field_error = "field wol must be object";
      } else {
        const auto& wol = payload["wol"];
        ok = update_string_field(wol, "mac", params.wol.mac, field_error) &&
             update_string_field(wol, "broadcast", params.wol.broadcast, field_error) &&
             update_int_field(wol, "port", 1, 65535, params.wol.port, field_error);
      }
    }

    if (ok && payload.contains("ssh")) {
      if (!payload["ssh"].is_object()) {
        ok = false;
        field_error = "field ssh must be object";
      } else {
        const auto& ssh = payload["ssh"];
        ok = update_string_field(ssh, "host", params.ssh.host, field_error) &&
             update_int_field(ssh, "port", 1, 65535, params.ssh.port, field_error) &&
             update_string_field(ssh, "user", params.ssh.user, field_error) &&
             update_string_field(ssh, "password", params.ssh.password, field_error) &&
             update_int_field(
                 ssh,
                 "timeout_ms",
                 100,
                 std::numeric_limits<int>::max(),
                 params.ssh.timeout_ms,
                 field_error);
      }
    }

    if (!ok) {
      out.status = command_status::failed;
      out.exit_code = -1;
      out.result = {{"error", field_error}};
      return out;
    }

    std::string save_error;
    if (!deps_.save_params(params, save_error)) {
      out.status = command_status::failed;
      out.exit_code = -1;
      out.result = {{"error", save_error}};
      return out;
    }

    out.status = command_status::succeeded;
    out.exit_code = 0;
    out.result = params_to_json(params);
    return out;
  });
}

} // namespace control
