#pragma once

#include <string>

namespace owt_ctrl {

struct ServerConfig {
  std::string host = "127.0.0.1";
  int port = 9081;
  int threads = 4;
  bool enable_rate_limit = true;
  int rate_limit_rps = 20;
  int rate_limit_burst = 40;
};

struct Config {
  ServerConfig server;
};

Config loadConfig(const std::string& path);

} // namespace owt_ctrl
