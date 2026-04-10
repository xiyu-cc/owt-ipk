#include "api/api_holder.h"
#include "config.h"
#include "control/control_protocol.h"
#include "log.h"
#include "server/controller.h"
#include "server/websocket_session_observer_factory.h"
#include "service/command_retry_scheduler.h"
#include "service/command_store.h"
#include "service/control_hub.h"
#include "service/control_ws_session_observer.h"
#include "service/frontend_status_ws_session_observer.h"
#include "service/rate_limiter.h"

#include <cstdlib>
#include <string>

int main(int argc, char* argv[]) {
  log::init();
  log::info("owt-net start");

  std::string configPath = "config.ini";
  if (argc > 1 && argv[1] != nullptr && argv[1][0] != '\0') {
    configPath = argv[1];
  }
  log::info("config path: {}", configPath);

  const auto cfg = owt_ctrl::loadConfig(configPath);
  service::configure_http_rate_limit(
      cfg.server.enable_rate_limit, cfg.server.rate_limit_rps, cfg.server.rate_limit_burst);
  log::info(
      "http rate limit: enabled={}, rps={}, burst={}",
      cfg.server.enable_rate_limit ? "true" : "false",
      cfg.server.rate_limit_rps,
      cfg.server.rate_limit_burst);
  server::set_websocket_session_observer_factory([](const std::string& path) {
    if (path == "/ws/control") {
      return service::create_control_ws_session_observer();
    }
    if (path == "/ws/status") {
      return service::create_frontend_status_ws_session_observer();
    }
    return std::shared_ptr<server::websocket_session_observer>{};
  });
  bool retry_scheduler_started = false;
  std::string db_error;
  if (!service::init_command_store(db_error)) {
    log::warn("init command store failed: {}", db_error);
  } else {
    service::bootstrap_agent_runtime_states();
    int recovered_count = 0;
    db_error.clear();
    if (!service::recover_inflight_commands(control::unix_time_ms_now(), recovered_count, db_error)) {
      log::warn("recover inflight commands failed: {}", db_error);
    } else if (recovered_count > 0) {
      log::warn("recovered inflight commands as timed_out: count={}", recovered_count);
    }
    retry_scheduler_started = service::start_command_retry_scheduler();
    if (!retry_scheduler_started) {
      log::warn("command retry scheduler start failed");
    }
  }

  log::info("run mode: control-plane");
  api::api_holder holder;

  try {
    server::controller::init(cfg.server.host, cfg.server.port, cfg.server.threads);
  } catch (...) {
    if (retry_scheduler_started) {
      service::stop_command_retry_scheduler();
    }
    service::shutdown_command_store();
    throw;
  }

  if (retry_scheduler_started) {
    service::stop_command_retry_scheduler();
  }
  service::shutdown_command_store();

  log::info("owt-net exit");
  log::shutdown();
  return EXIT_SUCCESS;
}
