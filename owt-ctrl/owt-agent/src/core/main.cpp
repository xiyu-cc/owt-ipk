#include "config.h"
#include "control/agent_runtime.h"
#include "log.h"
#include "service/command_store.h"
#include "service/host_probe_agent.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <string>
#include <thread>

namespace {

std::atomic<bool> g_stop_requested{false};

void on_signal(int) {
  g_stop_requested.store(true, std::memory_order_relaxed);
}

} // namespace

int main(int argc, char* argv[]) {
  log::init();
  log::info("owt-agent start");

  std::string configPath = "config.ini";
  if (argc > 1 && argv[1] != nullptr && argv[1][0] != '\0') {
    configPath = argv[1];
  }
  log::info("config path: {}", configPath);

  const auto cfg = owt_agent::loadConfig(configPath);

  std::string db_error;
  if (!service::init_command_store(db_error)) {
    log::warn("init command store failed: {}", db_error);
  }

  std::signal(SIGINT, on_signal);
  std::signal(SIGTERM, on_signal);

  control::agent_runtime runtime;
  control::agent_runtime_options options;
  options.agent_id = cfg.agent.agent_id;
  options.protocol_version = cfg.agent.protocol_version;
  options.management_token = cfg.agent.management_token;
  options.enable_wss = cfg.agent.enable_wss;
  options.wss_endpoint = cfg.agent.wss_endpoint;
  options.enable_grpc = cfg.agent.enable_grpc;
  options.grpc_endpoint = cfg.agent.grpc_endpoint;
  options.prefer_wss = (cfg.agent.primary_channel != "grpc");

  service::start_host_probe_agent();
  if (!runtime.start(options)) {
    service::stop_host_probe_agent();
    service::shutdown_command_store();
    log::error("agent runtime start failed");
    log::shutdown();
    return EXIT_FAILURE;
  }

  auto next_heartbeat_at = std::chrono::steady_clock::now();
  while (!g_stop_requested.load(std::memory_order_relaxed)) {
    const auto now = std::chrono::steady_clock::now();
    if (now >= next_heartbeat_at) {
      runtime.send_heartbeat();
      next_heartbeat_at = now + std::chrono::seconds(10);
    }
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  runtime.stop();
  service::stop_host_probe_agent();
  service::shutdown_command_store();

  log::info("owt-agent exit");
  log::shutdown();
  return EXIT_SUCCESS;
}
