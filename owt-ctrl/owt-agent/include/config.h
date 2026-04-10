#pragma once

#include <string>

namespace owt_agent {

struct AgentConfig {
  std::string agent_id = "agent-local";
  std::string protocol_version = "v1.0-draft";

  std::string wss_endpoint = "wss://owt.wzhex.com/ws/control";
};

struct Config {
  AgentConfig agent;
};

Config loadConfig(const std::string& path);

} // namespace owt_agent
