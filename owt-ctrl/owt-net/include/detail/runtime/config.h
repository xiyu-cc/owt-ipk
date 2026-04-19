#pragma once

#include <string>

namespace server_core {

struct ServerConfig {
  std::string host = "0.0.0.0";
  int port = 8080;
  int threads = 4;
};

struct Config {
  ServerConfig server;
};

Config loadConfig(const std::string &path);

} // namespace server_core

#include "detail/runtime/config_impl.h"
