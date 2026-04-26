#include "config.h"
#include "common/string_utils.h"
#include "control/agent_runtime.h"
#include "log.h"
#include "service/host_probe_agent.h"

#include <ifaddrs.h>
#include <net/if.h>
#include <netpacket/packet.h>

#include <atomic>
#include <algorithm>
#include <array>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <string>
#include <thread>

namespace {

std::atomic<bool> g_stop_requested{false};

void on_signal(int) {
  g_stop_requested.store(true, std::memory_order_relaxed);
}

bool parse_mac(const std::string& text, std::array<uint8_t, 6>& out) {
  std::array<uint8_t, 6> parsed{};
  std::istringstream ss(text);
  std::string part;
  int idx = 0;
  while (std::getline(ss, part, ':')) {
    if (idx >= 6 || part.size() != 2) {
      return false;
    }
    char* end = nullptr;
    const auto value = std::strtoul(part.c_str(), &end, 16);
    if (end == nullptr || *end != '\0' || value > 0xFFUL) {
      return false;
    }
    parsed[static_cast<std::size_t>(idx++)] = static_cast<uint8_t>(value);
  }
  if (idx != 6) {
    return false;
  }
  out = parsed;
  return true;
}

std::string format_mac(const uint8_t* bytes) {
  std::ostringstream oss;
  oss << std::uppercase << std::hex << std::setfill('0');
  for (int i = 0; i < 6; ++i) {
    if (i > 0) {
      oss << ':';
    }
    oss << std::setw(2) << static_cast<int>(bytes[i]);
  }
  return oss.str();
}

std::string normalize_mac(std::string value) {
  value = owt_agent::common::trim(std::move(value));
  if (value.empty()) {
    return "";
  }
  std::array<uint8_t, 6> parsed{};
  if (!parse_mac(value, parsed)) {
    return "";
  }
  return format_mac(parsed.data());
}

std::string detect_agent_mac() {
  ifaddrs* ifaddr = nullptr;
  if (getifaddrs(&ifaddr) != 0 || ifaddr == nullptr) {
    return "";
  }

  std::string resolved;
  for (ifaddrs* it = ifaddr; it != nullptr; it = it->ifa_next) {
    if (it->ifa_addr == nullptr || it->ifa_name == nullptr) {
      continue;
    }
    if ((it->ifa_flags & IFF_LOOPBACK) != 0) {
      continue;
    }
    if (it->ifa_addr->sa_family != AF_PACKET) {
      continue;
    }

    const auto* sll = reinterpret_cast<const sockaddr_ll*>(it->ifa_addr);
    if (sll->sll_halen != 6) {
      continue;
    }
    const auto* mac = reinterpret_cast<const uint8_t*>(sll->sll_addr);
    bool all_zero = true;
    for (int i = 0; i < 6; ++i) {
      if (mac[i] != 0) {
        all_zero = false;
        break;
      }
    }
    if (all_zero) {
      continue;
    }
    resolved = format_mac(mac);
    break;
  }

  freeifaddrs(ifaddr);
  return resolved;
}

std::string resolve_agent_mac(const std::string& configured) {
  const auto trimmed = owt_agent::common::trim(configured);
  if (!trimmed.empty() && owt_agent::common::to_lower(trimmed) != "auto") {
    const auto normalized = normalize_mac(trimmed);
    if (!normalized.empty()) {
      return normalized;
    }
  }
  return detect_agent_mac();
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

  const auto cfg = owt_agent::load_config(configPath);

  std::signal(SIGINT, on_signal);
  std::signal(SIGTERM, on_signal);

  control::agent_runtime runtime;
  control::agent_runtime_options options;
  options.agent_id = cfg.agent.agent_id;
  options.agent_mac = resolve_agent_mac(cfg.agent.agent_mac);
  options.protocol_version = cfg.agent.protocol_version;
  options.wss_endpoint = cfg.agent.wss_endpoint;
  options.ws_event_workers = cfg.agent.ws_event_workers;
  options.ws_event_queue_capacity = cfg.agent.ws_event_queue_capacity;
  if (options.agent_mac.empty()) {
    log::error("agent runtime start failed: cannot resolve agent_mac");
    log::shutdown();
    return EXIT_FAILURE;
  }
  log::info("agent identity: agent_id={}, agent_mac={}", options.agent_id, options.agent_mac);

  const int heartbeat_interval_ms = std::clamp(cfg.agent.heartbeat_interval_ms, 1000, 120000);
  const int status_collect_interval_ms =
      std::clamp(cfg.agent.status_collect_interval_ms, 200, 60000);
  log::info(
      "runtime intervals: heartbeat_interval_ms={}, status_collect_interval_ms={}, ws_event_workers={}, ws_event_queue_capacity={}",
      heartbeat_interval_ms,
      status_collect_interval_ms,
      options.ws_event_workers,
      options.ws_event_queue_capacity);

  service::start_host_probe_agent(status_collect_interval_ms);
  if (!runtime.start(options)) {
    service::stop_host_probe_agent();
    log::error("agent runtime start failed");
    log::shutdown();
    return EXIT_FAILURE;
  }

  const auto heartbeat_interval = std::chrono::milliseconds(heartbeat_interval_ms);
  auto next_heartbeat_at = std::chrono::steady_clock::now();
  while (!g_stop_requested.load(std::memory_order_relaxed)) {
    const auto now = std::chrono::steady_clock::now();
    if (now >= next_heartbeat_at) {
      runtime.send_heartbeat();
      next_heartbeat_at = now + heartbeat_interval;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }

  runtime.stop();
  service::stop_host_probe_agent();

  log::info("owt-agent exit");
  log::shutdown();
  return EXIT_SUCCESS;
}
