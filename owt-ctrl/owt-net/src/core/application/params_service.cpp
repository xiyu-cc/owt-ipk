#include "ctrl/application/params_service.h"

#include <cstdint>
#include <initializer_list>
#include <stdexcept>

namespace ctrl::application {

namespace {

void reject_unknown_fields(
    const nlohmann::json& source,
    std::string_view object_name,
    std::initializer_list<std::string_view> allowed) {
  if (!source.is_object()) {
    throw std::invalid_argument(std::string(object_name) + " must be object");
  }

  for (auto it = source.begin(); it != source.end(); ++it) {
    bool matched = false;
    for (const auto candidate : allowed) {
      if (it.key() == candidate) {
        matched = true;
        break;
      }
    }
    if (!matched) {
      throw std::invalid_argument(
          "unknown field in " + std::string(object_name) + ": " + it.key());
    }
  }
}

} // namespace

ParamsService::ParamsService(ports::IParamsRepository& repo, const ports::IClock& clock)
    : repo_(repo), clock_(clock) {}

nlohmann::json ParamsService::default_params_payload() {
  return {
      {"wol",
       {
           {"mac", "AA:BB:CC:DD:EE:FF"},
           {"broadcast", "192.168.1.255"},
           {"port", 9},
       }},
      {"ssh",
       {
           {"host", "192.168.1.10"},
           {"port", 22},
           {"user", "root"},
           {"password", "password"},
           {"timeout_ms", 5000},
       }},
  };
}

nlohmann::json ParamsService::load_or_init(std::string_view agent_mac) {
  if (agent_mac.empty()) {
    throw std::invalid_argument("agent_mac is required");
  }

  nlohmann::json params;
  std::string error;
  if (repo_.load(agent_mac, params, error)) {
    if (!params.is_object()) {
      params = default_params_payload();
      save(agent_mac, params);
    }
    return params;
  }

  if (!error.empty() && error != "agent params not found") {
    throw std::runtime_error("load params failed: " + error);
  }

  params = default_params_payload();
  save(agent_mac, params);
  return params;
}

std::optional<nlohmann::json> ParamsService::load_existing(std::string_view agent_mac) {
  if (agent_mac.empty()) {
    throw std::invalid_argument("agent_mac is required");
  }

  nlohmann::json params;
  std::string error;
  if (repo_.load(agent_mac, params, error)) {
    if (!params.is_object()) {
      throw std::runtime_error("load params failed: persisted params must be object");
    }
    return params;
  }

  if (error.empty() || error == "agent params not found") {
    return std::nullopt;
  }
  throw std::runtime_error("load params failed: " + error);
}

nlohmann::json ParamsService::merge_and_validate(
    std::string_view agent_mac,
    const nlohmann::json& patch) {
  if (!patch.is_object()) {
    throw std::invalid_argument("params payload must be object");
  }
  reject_unknown_fields(patch, "params payload", {"wol", "ssh"});

  auto current = load_or_init(agent_mac);
  if (!current.is_object()) {
    current = default_params_payload();
  }
  const auto defaults = default_params_payload();
  if (!current.contains("wol") || !current["wol"].is_object()) {
    current["wol"] = defaults["wol"];
  }
  if (!current.contains("ssh") || !current["ssh"].is_object()) {
    current["ssh"] = defaults["ssh"];
  }

  std::string error;
  if (patch.contains("wol")) {
    if (!patch["wol"].is_object()) {
      throw std::invalid_argument("field wol must be object");
    }
    reject_unknown_fields(patch["wol"], "wol", {"mac", "broadcast", "port"});
    auto& wol = current["wol"];
    const auto& wol_patch = patch["wol"];
    if (!update_string_field(wol, wol_patch, "mac", error) ||
        !update_string_field(wol, wol_patch, "broadcast", error) ||
        !update_int_field(wol, wol_patch, "port", 1, 65535, error)) {
      throw std::invalid_argument(error);
    }
  }

  if (patch.contains("ssh")) {
    if (!patch["ssh"].is_object()) {
      throw std::invalid_argument("field ssh must be object");
    }
    reject_unknown_fields(patch["ssh"], "ssh", {"host", "port", "user", "password", "timeout_ms"});
    auto& ssh = current["ssh"];
    const auto& ssh_patch = patch["ssh"];
    if (!update_string_field(ssh, ssh_patch, "host", error) ||
        !update_int_field(ssh, ssh_patch, "port", 1, 65535, error) ||
        !update_string_field(ssh, ssh_patch, "user", error) ||
        !update_string_field(ssh, ssh_patch, "password", error) ||
        !update_int_field(ssh, ssh_patch, "timeout_ms", 100, INT32_MAX, error)) {
      throw std::invalid_argument(error);
    }
  }

  return current;
}

void ParamsService::save(std::string_view agent_mac, const nlohmann::json& params) {
  if (agent_mac.empty()) {
    throw std::invalid_argument("agent_mac is required");
  }
  if (!params.is_object()) {
    throw std::invalid_argument("params must be object");
  }
  std::string error;
  if (!repo_.save(agent_mac, params, clock_.now_ms(), error)) {
    throw std::runtime_error("save params failed: " + error);
  }
}

bool ParamsService::update_string_field(
    nlohmann::json& target,
    const nlohmann::json& patch,
    const char* key,
    std::string& error) {
  if (!patch.contains(key)) {
    return true;
  }
  if (!patch[key].is_string()) {
    error = std::string("field ") + key + " must be string";
    return false;
  }
  target[key] = patch[key].get<std::string>();
  return true;
}

bool ParamsService::update_int_field(
    nlohmann::json& target,
    const nlohmann::json& patch,
    const char* key,
    int min_value,
    int max_value,
    std::string& error) {
  if (!patch.contains(key)) {
    return true;
  }
  if (!patch[key].is_number_integer()) {
    error = std::string("field ") + key + " must be integer";
    return false;
  }
  const auto parsed = patch[key].get<int64_t>();
  if (parsed < static_cast<int64_t>(min_value) || parsed > static_cast<int64_t>(max_value)) {
    error = std::string("field ") + key + " out of range";
    return false;
  }
  target[key] = static_cast<int>(parsed);
  return true;
}

} // namespace ctrl::application
