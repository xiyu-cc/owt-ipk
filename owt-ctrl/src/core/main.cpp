#include "api/api_holder.h"
#include "config.h"
#include "log.h"
#include "server/controller.h"
#include "service/host_probe_agent.h"

#include <cstdlib>
#include <string>

int main(int argc, char* argv[]) {
  log::init();
  log::info("server start");

  api::api_holder holder;

  std::string configPath = "config.ini";
  if (argc > 1 && argv[1] != nullptr && argv[1][0] != '\0') {
    configPath = argv[1];
  }
  log::info("config path: {}", configPath);

  const auto cfg = owt_ctrl::loadConfig(configPath);

  service::start_host_probe_agent();
  try {
    server::controller::init(cfg.server.host, cfg.server.port, cfg.server.threads);
  } catch (...) {
    service::stop_host_probe_agent();
    throw;
  }
  service::stop_host_probe_agent();

  log::info("server exit");
  log::shutdown();
  return EXIT_SUCCESS;
}
