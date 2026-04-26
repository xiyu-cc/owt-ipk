#include "control/ports/json_mappers.h"

#include <exception>
#include <limits>

namespace control::ports {

namespace {

template <typename T>
bool update_string_field(
    const nlohmann::json& source,
    const char* key,
    T& target,
    std::string& error) {
  if (!source.contains(key)) {
    return true;
  }
  if (!source[key].is_string()) {
    error = std::string("field ") + key + " must be string";
    return false;
  }
  target = source[key].get<std::string>();
  return true;
}

template <typename T>
bool update_int_field(
    const nlohmann::json& source,
    const char* key,
    int min_value,
    int max_value,
    T& target,
    std::string& error) {
  if (!source.contains(key)) {
    return true;
  }
  if (!source[key].is_number_integer()) {
    error = std::string("field ") + key + " must be integer";
    return false;
  }

  int64_t parsed = 0;
  try {
    parsed = source[key].get<int64_t>();
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

} // namespace

nlohmann::json control_params_to_json(const control_params& params) {
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

nlohmann::json host_probe_snapshot_to_json(
    const host_probe_snapshot& snap,
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

bool apply_control_params_patch(
    const nlohmann::json& patch,
    control_params& params,
    std::string& error) {
  error.clear();
  if (!patch.is_object()) {
    error = "payload must be object";
    return false;
  }

  if (patch.contains("wol")) {
    if (!patch["wol"].is_object()) {
      error = "field wol must be object";
      return false;
    }
    const auto& wol = patch["wol"];
    if (!update_string_field(wol, "mac", params.wol.mac, error) ||
        !update_string_field(wol, "broadcast", params.wol.broadcast, error) ||
        !update_int_field(wol, "port", 1, 65535, params.wol.port, error)) {
      return false;
    }
  }

  if (patch.contains("ssh")) {
    if (!patch["ssh"].is_object()) {
      error = "field ssh must be object";
      return false;
    }
    const auto& ssh = patch["ssh"];
    if (!update_string_field(ssh, "host", params.ssh.host, error) ||
        !update_int_field(ssh, "port", 1, 65535, params.ssh.port, error) ||
        !update_string_field(ssh, "user", params.ssh.user, error) ||
        !update_string_field(ssh, "password", params.ssh.password, error) ||
        !update_int_field(ssh, "timeout_ms", 100, std::numeric_limits<int>::max(), params.ssh.timeout_ms, error)) {
      return false;
    }
  }

  return true;
}

} // namespace control::ports
