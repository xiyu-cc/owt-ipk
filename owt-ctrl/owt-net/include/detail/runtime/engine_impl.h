#pragma once

#include "server_core/http_routes.h"
#include "server_core/ws_routes.h"
#include "detail/runtime/config.h"
#include "detail/runtime/log.h"
#include "detail/transport/controller.h"
#include "detail/ws_deal/handler_dispatcher.h"

#include <cstdlib>
#include <memory>

namespace server_core {

class server::state {
public:
  state()
      : ws_dispatcher(std::make_shared<ws_deal::handler_dispatcher>()),
        http_routes(),
        ws_routes(*ws_dispatcher) {}

  std::shared_ptr<ws_deal::handler_dispatcher> ws_dispatcher;
  server_core::http::route_registry http_routes;
  server_core::ws::route_registry ws_routes;
};

inline server::server() {
  log::init();
  state_ = std::make_unique<state>();
  log::info("server start");
}

inline server::~server() {
  state_.reset();
  log::info("server exit");
  log::shutdown();
}

inline int server::run(const std::string &config_path) {
  if (!state_) {
    return EXIT_FAILURE;
  }
  const auto cfg = loadConfig(config_path);
  const bool ok = ::transport::controller::init(
      cfg.server.host, cfg.server.port, cfg.server.threads,
      state_->ws_dispatcher);
  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}

} // namespace server_core
