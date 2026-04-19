#pragma once

#include <memory>
#include <string>

namespace server_core {

class server {
public:
  server();
  ~server();

  server(const server &) = delete;
  server(server &&) = delete;
  server &operator=(const server &) = delete;
  server &operator=(server &&) = delete;

  int run(const std::string &config_path = "config.ini");

private:
  class state;
  std::unique_ptr<state> state_;
};

} // namespace server_core

#include "detail/runtime/engine_impl.h"
