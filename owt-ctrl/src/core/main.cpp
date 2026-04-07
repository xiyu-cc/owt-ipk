#include "api/api_holder.h"
#include "config.h"
#include "log.h"
#include "server/controller.h"

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

  server::controller::init(cfg.server.host, cfg.server.port, cfg.server.threads);

  log::info("server exit");
  log::shutdown();
  return EXIT_SUCCESS;
}
