#pragma once

#include "ctrl/ports/interfaces.h"

#include <nlohmann/json.hpp>

#include <optional>
#include <string_view>

namespace ctrl::application {

class ParamsService {
public:
  ParamsService(ports::IParamsRepository& repo, const ports::IClock& clock);

  nlohmann::json load_or_init(std::string_view agent_mac);
  std::optional<nlohmann::json> load_existing(std::string_view agent_mac);
  nlohmann::json merge_and_validate(std::string_view agent_mac, const nlohmann::json& patch);
  void save(std::string_view agent_mac, const nlohmann::json& params);

  static nlohmann::json default_params_payload();

private:
  static bool update_string_field(
      nlohmann::json& target,
      const nlohmann::json& patch,
      const char* key,
      std::string& error);
  static bool update_int_field(
      nlohmann::json& target,
      const nlohmann::json& patch,
      const char* key,
      int min_value,
      int max_value,
      std::string& error);

  ports::IParamsRepository& repo_;
  const ports::IClock& clock_;
};

} // namespace ctrl::application
