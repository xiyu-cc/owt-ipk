#pragma once

#include <string>

namespace owt_agent {

struct AgentConfig {
  std::string agent_id = "agent-local";
  std::string protocol_version = "v1.0-draft";
  std::string management_token;

  bool enable_wss = true;
  std::string wss_endpoint = "wss://127.0.0.1:9527/ws/control";

  bool enable_grpc = true;
  std::string grpc_endpoint = "127.0.0.1:50051";

  std::string primary_channel = "wss";
};

struct Config {
  AgentConfig agent;
};

Config loadConfig(const std::string& path);

} // namespace owt_agent
