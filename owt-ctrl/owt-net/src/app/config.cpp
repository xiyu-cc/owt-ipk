#include "app/config.h"

#include <algorithm>
#include <cctype>
#include <fstream>

namespace app {

namespace {

std::string trim(std::string s) {
  const auto not_space = [](unsigned char c) { return !std::isspace(c); };
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
  s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
  return s;
}

std::string to_lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return s;
}

bool parse_int(const std::string& s, int& out) {
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

bool parse_bool(const std::string& s, bool& out) {
  const auto value = to_lower(trim(s));
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

Config load_config(const std::string& path) {
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
      section = to_lower(trim(line.substr(1, line.size() - 2)));
      continue;
    }

    const auto eq = line.find('=');
    if (eq == std::string::npos) {
      continue;
    }

    const std::string key = to_lower(trim(line.substr(0, eq)));
    const std::string value = trim(line.substr(eq + 1));
    if (section == "server") {
      if (key == "host") {
        cfg.server.host = value;
      } else if (key == "port") {
        parse_int(value, cfg.server.port);
      } else if (key == "threads") {
        parse_int(value, cfg.server.threads);
      } else if (key == "enable_rate_limit") {
        parse_bool(value, cfg.server.enable_rate_limit);
      } else if (key == "rate_limit_rps") {
        parse_int(value, cfg.server.rate_limit_rps);
      } else if (key == "rate_limit_burst") {
        parse_int(value, cfg.server.rate_limit_burst);
      } else if (key == "retry_tick_ms") {
        parse_int(value, cfg.server.retry_tick_ms);
      } else if (key == "retry_batch") {
        parse_int(value, cfg.server.retry_batch);
      }
      continue;
    }

    if (section == "storage") {
      if (key == "db_path") {
        cfg.storage.db_path = value;
      } else if (key == "retention_days") {
        int parsed = cfg.storage.retention_days;
        if (parse_int(value, parsed)) {
          cfg.storage.retention_days = std::max(1, std::min(parsed, 3650));
        }
      } else if (key == "cleanup_interval_sec") {
        int parsed = cfg.storage.cleanup_interval_sec;
        if (parse_int(value, parsed)) {
          cfg.storage.cleanup_interval_sec = std::max(60, std::min(parsed, 86400));
        }
      }
    }
  }

  return cfg;
}

} // namespace app
