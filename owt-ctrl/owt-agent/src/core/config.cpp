#include "config.h"

#include <algorithm>
#include <cctype>
#include <fstream>

namespace owt_agent {

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

    if (section == "agent") {
      if (key == "agent_id") {
        cfg.agent.agent_id = value;
      } else if (key == "protocol_version") {
        cfg.agent.protocol_version = value;
      } else if (key == "wss_endpoint") {
        cfg.agent.wss_endpoint = value;
      }
      continue;
    }

  }

  return cfg;
}

} // namespace owt_agent
