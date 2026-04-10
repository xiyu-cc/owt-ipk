#include "service/sensitive_json.h"

#include <algorithm>
#include <cctype>
#include <regex>
#include <string>
#include <vector>

namespace service {

namespace {

std::string to_lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return text;
}

bool is_sensitive_key(const std::string& key) {
  static const std::vector<std::string> kSensitiveTokens = {
      "password",
      "passwd",
      "pwd",
      "token",
      "secret",
      "private_key",
      "private-key",
  };

  const auto lowered = to_lower(key);
  for (const auto& token : kSensitiveTokens) {
    if (lowered.find(token) != std::string::npos) {
      return true;
    }
  }
  return false;
}

nlohmann::json redact_json_node(const nlohmann::json& value) {
  if (value.is_object()) {
    nlohmann::json out = nlohmann::json::object();
    for (auto it = value.begin(); it != value.end(); ++it) {
      if (is_sensitive_key(it.key())) {
        out[it.key()] = "***";
      } else {
        out[it.key()] = redact_json_node(it.value());
      }
    }
    return out;
  }

  if (value.is_array()) {
    nlohmann::json out = nlohmann::json::array();
    for (const auto& item : value) {
      out.push_back(redact_json_node(item));
    }
    return out;
  }

  return value;
}

std::string redact_plain_text(const std::string& text) {
  std::string redacted = text;

  static const std::regex kJsonPair(
      R"regex(("(?:password|passwd|pwd|token|secret|private[_-]?key)"\s*:\s*")([^"]*)("))regex",
      std::regex::icase);
  redacted = std::regex_replace(redacted, kJsonPair, "$1***$3");

  static const std::regex kKvPair(
      R"(((?:password|passwd|pwd|token|secret|private[_-]?key)\s*=\s*)([^&\s]+))",
      std::regex::icase);
  redacted = std::regex_replace(redacted, kKvPair, "$1***");

  return redacted;
}

} // namespace

nlohmann::json redact_sensitive_json(const nlohmann::json& value) {
  return redact_json_node(value);
}

std::string redact_sensitive_json_text(const std::string& text) {
  if (text.empty()) {
    return text;
  }

  const auto parsed = nlohmann::json::parse(text, nullptr, false);
  if (!parsed.is_discarded()) {
    return redact_sensitive_json(parsed).dump();
  }
  return redact_plain_text(text);
}

} // namespace service
