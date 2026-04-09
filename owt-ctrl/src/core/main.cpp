#include "api/api_holder.h"
#include "config.h"
#include "log.h"
#include "server/controller.h"
#include "server/grpc_control_server.h"
#include "service/command_store.h"
#include "service/host_probe_agent.h"

#include <cstdlib>
#include <string>

int main(int argc, char* argv[]) {
  log::init();
  log::info("owt-ctrl start");

  std::string configPath = "config.ini";
  if (argc > 1 && argv[1] != nullptr && argv[1][0] != '\0') {
    configPath = argv[1];
  }
  log::info("config path: {}", configPath);

  const auto cfg = owt_ctrl::loadConfig(configPath);
  std::string db_error;
  if (!service::init_command_store(db_error)) {
    log::warn("init command store failed: {}", db_error);
  }

  log::info("run mode: control-plane");
  api::api_holder holder;
  server::grpc_control_server grpc_server;
  if (cfg.server.enable_grpc) {
    if (!grpc_server.start(cfg.server.grpc_endpoint)) {
      log::warn("start grpc control server failed: {}", cfg.server.grpc_endpoint);
    }
  }

  service::start_host_probe_agent();
  try {
    server::controller::init(cfg.server.host, cfg.server.port, cfg.server.threads);
  } catch (...) {
    service::stop_host_probe_agent();
    grpc_server.stop();
    service::shutdown_command_store();
    throw;
  }

  service::stop_host_probe_agent();
  grpc_server.stop();
  service::shutdown_command_store();

  log::info("owt-ctrl exit");
  log::shutdown();
  return EXIT_SUCCESS;
}
