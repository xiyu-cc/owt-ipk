#pragma once

#include <string>

namespace owt_agent {

struct AgentConfig {
  std::string agent_id = "agent-local";
  std::string agent_mac = "auto";
  std::string protocol_version = "v3";
  std::string wss_endpoint = "wss://owt.wzhex.com/ws/v3/agent";
  int heartbeat_interval_ms = 10000;
  int status_collect_interval_ms = 1000;
};

struct Config {
  AgentConfig agent;
};

Config loadConfig(const std::string& path);

} // namespace owt_agent
