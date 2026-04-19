#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>

namespace ctrl::application {

class RedactionService {
public:
  nlohmann::json redact_json(const nlohmann::json& value) const;
  std::string redact_text(std::string_view text) const;
};

} // namespace ctrl::application
