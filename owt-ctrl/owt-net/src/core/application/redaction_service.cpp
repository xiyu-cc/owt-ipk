#include "ctrl/application/redaction_service.h"

#include <algorithm>
#include <cctype>
#include <regex>
#include <string>
#include <vector>

namespace ctrl::application {

namespace {

std::string to_lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return text;
}

bool is_sensitive_key(std::string_view key) {
  static const std::vector<std::string> kSensitiveTokens = {
      "password",
      "passwd",
      "pwd",
      "token",
      "secret",
      "private_key",
      "private-key",
  };

  const auto lowered = to_lower(std::string(key));
  for (const auto& token : kSensitiveTokens) {
    if (lowered.find(token) != std::string::npos) {
      return true;
    }
  }
  return false;
}

nlohmann::json redact_node(const nlohmann::json& value) {
  if (value.is_object()) {
    nlohmann::json out = nlohmann::json::object();
    for (auto it = value.begin(); it != value.end(); ++it) {
      if (is_sensitive_key(it.key())) {
        out[it.key()] = "***";
      } else {
        out[it.key()] = redact_node(it.value());
      }
    }
    return out;
  }

  if (value.is_array()) {
    nlohmann::json out = nlohmann::json::array();
    for (const auto& item : value) {
      out.push_back(redact_node(item));
    }
    return out;
  }

  return value;
}

std::string redact_plain_text(std::string text) {
  static const std::regex kJsonPair(
      R"regex(("(?:password|passwd|pwd|token|secret|private[_-]?key)"\s*:\s*")([^"]*)("))regex",
      std::regex::icase);
  text = std::regex_replace(text, kJsonPair, "$1***$3");

  static const std::regex kKvPair(
      R"(((?:password|passwd|pwd|token|secret|private[_-]?key)\s*=\s*)([^&\s]+))",
      std::regex::icase);
  text = std::regex_replace(text, kKvPair, "$1***");
  return text;
}

} // namespace

nlohmann::json RedactionService::redact_json(const nlohmann::json& value) const {
  return redact_node(value);
}

std::string RedactionService::redact_text(std::string_view text) const {
  if (text.empty()) {
    return {};
  }
  const auto parsed = nlohmann::json::parse(text, nullptr, false);
  if (!parsed.is_discarded()) {
    return redact_json(parsed).dump();
  }
  return redact_plain_text(std::string(text));
}

} // namespace ctrl::application
