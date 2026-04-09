#pragma once

#include <string>

namespace owt_ctrl {

struct ServerConfig {
  std::string host = "0.0.0.0";
  int port = 9527;
  int threads = 4;
  bool enable_grpc = true;
  std::string grpc_endpoint = "0.0.0.0:50051";
};

struct Config {
  ServerConfig server;
};

Config loadConfig(const std::string& path);

} // namespace owt_ctrl
