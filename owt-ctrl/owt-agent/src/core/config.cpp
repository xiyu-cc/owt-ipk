#include "config.h"
#include "common/string_utils.h"

#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace owt_agent {

namespace {

bool parse_int_strict(const std::string& text, int& out) {
  try {
    size_t idx = 0;
    const int parsed = std::stoi(text, &idx);
    if (idx != text.size()) {
      return false;
    }
    out = parsed;
    return true;
  } catch (const std::exception&) {
    return false;
  }
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
    line = common::trim(std::move(line));
    if (line.empty() || line.front() == '#' || line.front() == ';') {
      continue;
    }

    if (line.front() == '[' && line.back() == ']') {
      section = common::to_lower(common::trim(line.substr(1, line.size() - 2)));
      continue;
    }

    const auto eq = line.find('=');
    if (eq == std::string::npos) {
      continue;
    }

    const std::string key = common::to_lower(common::trim(line.substr(0, eq)));
    const std::string value = common::trim(line.substr(eq + 1));

    if (section == "agent") {
      if (key == "agent_id") {
        cfg.agent.agent_id = value;
      } else if (key == "agent_mac") {
        cfg.agent.agent_mac = value;
      } else if (key == "protocol_version") {
        cfg.agent.protocol_version = value;
      } else if (key == "wss_endpoint") {
        cfg.agent.wss_endpoint = value;
      } else if (key == "heartbeat_interval_ms") {
        int parsed = 0;
        if (parse_int_strict(value, parsed)) {
          cfg.agent.heartbeat_interval_ms = std::clamp(parsed, 1000, 120000);
        }
      } else if (key == "status_collect_interval_ms") {
        int parsed = 0;
        if (parse_int_strict(value, parsed)) {
          cfg.agent.status_collect_interval_ms = std::clamp(parsed, 200, 60000);
        }
      } else if (key == "ws_event_workers") {
        int parsed = 0;
        if (parse_int_strict(value, parsed)) {
          cfg.agent.ws_event_workers = std::clamp(parsed, 1, 256);
        }
      } else if (key == "ws_event_queue_capacity") {
        int parsed = 0;
        if (parse_int_strict(value, parsed)) {
          cfg.agent.ws_event_queue_capacity = std::clamp(parsed, 64, 1'000'000);
        }
      }
      continue;
    }

  }

  return cfg;
}

} // namespace owt_agent
