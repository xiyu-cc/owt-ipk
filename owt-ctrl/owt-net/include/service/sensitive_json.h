#pragma once

#include <nlohmann/json.hpp>

#include <string>

namespace service {

nlohmann::json redact_sensitive_json(const nlohmann::json& value);
std::string redact_sensitive_json_text(const std::string& text);

} // namespace service

