#pragma once

#include <string>

namespace app {

struct ServerConfig {
  std::string host = "127.0.0.1";
  int port = 9081;
  int threads = 4;
  bool enable_rate_limit = true;
  int rate_limit_rps = 20;
  int rate_limit_burst = 40;
  int retry_tick_ms = 250;
  int retry_batch = 100;
  int ws_event_workers = 0;
  int ws_event_queue_capacity = 4096;
  int ws_event_low_priority_drop_threshold_pct = 80;
  int ui_event_send_timeout_ms = 2000;
  int ui_session_queue_limit = 1024;
};

struct StorageConfig {
  std::string db_path = "/var/lib/owt-net/owt-net.db";
  int retention_days = 30;
  int cleanup_interval_sec = 3600;
};

struct Config {
  ServerConfig server;
  StorageConfig storage;
};

Config load_config(const std::string& path);

} // namespace app
