#pragma once

#include <algorithm>
#include <cctype>
#include <fstream>

namespace server_core {
namespace detail {

inline std::string trim(std::string s) {
  const auto not_space = [](unsigned char c) { return !std::isspace(c); };
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
  s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
  return s;
}

inline std::string to_lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return s;
}

inline bool parse_int(const std::string &s, int &out) {
  try {
    size_t idx = 0;
    const int value = std::stoi(s, &idx);
    if (idx != s.size()) {
      return false;
    }
    out = value;
    return true;
  } catch (...) {
    return false;
  }
}

} // namespace detail

inline Config loadConfig(const std::string &path) {
  Config cfg;

  std::ifstream in(path);
  if (!in) {
    return cfg;
  }

  std::string section;
  std::string line;
  while (std::getline(in, line)) {
    line = detail::trim(std::move(line));
    if (line.empty() || line.front() == '#' || line.front() == ';') {
      continue;
    }

    if (line.front() == '[' && line.back() == ']') {
      section = detail::to_lower(detail::trim(line.substr(1, line.size() - 2)));
      continue;
    }

    const auto eq = line.find('=');
    if (eq == std::string::npos) {
      continue;
    }

    const std::string key = detail::to_lower(detail::trim(line.substr(0, eq)));
    const std::string value = detail::trim(line.substr(eq + 1));

    if (section != "server") {
      continue;
    }

    if (key == "host") {
      cfg.server.host = value;
      continue;
    }
    if (key == "port") {
      detail::parse_int(value, cfg.server.port);
      continue;
    }
    if (key == "threads") {
      detail::parse_int(value, cfg.server.threads);
    }
  }

  return cfg;
}

} // namespace server_core
