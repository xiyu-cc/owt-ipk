#include "config.h"

#include <algorithm>
#include <cctype>
#include <fstream>

namespace owt_ctrl {

namespace {

std::string trim(std::string s) {
  const auto not_space = [](unsigned char c) { return !std::isspace(c); };
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
  s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
  return s;
}

std::string toLower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return s;
}

bool parseInt(const std::string& s, int& out) {
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

bool parseBool(const std::string& s, bool& out) {
  const auto value = toLower(trim(s));
  if (value == "1" || value == "true" || value == "yes" || value == "on") {
    out = true;
    return true;
  }
  if (value == "0" || value == "false" || value == "no" || value == "off") {
    out = false;
    return true;
  }
  return false;
}

} // namespace

Config loadConfig(const std::string& path) {
  Config cfg;

  std::ifstream in(path);
  if (!in) {
    return cfg;
  }

  std::string section;
  std::string line;
  while (std::getline(in, line)) {
    line = trim(line);
    if (line.empty() || line.front() == '#' || line.front() == ';') {
      continue;
    }

    if (line.front() == '[' && line.back() == ']') {
      section = toLower(trim(line.substr(1, line.size() - 2)));
      continue;
    }

    const auto eq = line.find('=');
    if (eq == std::string::npos) {
      continue;
    }

    const std::string key = toLower(trim(line.substr(0, eq)));
    const std::string value = trim(line.substr(eq + 1));

    if (section == "server") {
      if (key == "host") {
        cfg.server.host = value;
      } else if (key == "port") {
        parseInt(value, cfg.server.port);
      } else if (key == "threads") {
        parseInt(value, cfg.server.threads);
      } else if (key == "enable_grpc") {
        parseBool(value, cfg.server.enable_grpc);
      } else if (key == "grpc_endpoint") {
        cfg.server.grpc_endpoint = value;
      }
    }
  }

  return cfg;
}

} // namespace owt_ctrl
