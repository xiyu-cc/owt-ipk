#include "api/host_probe_api.h"

#include "api/api_common.h"
#include "service/host_probe_agent.h"

#include <nlohmann/json.hpp>

namespace api {

namespace {

nlohmann::json maybe_int(bool has_value, uint64_t value) {
  if (!has_value) {
    return nullptr;
  }
  return value;
}

nlohmann::json maybe_int(bool has_value, int value) {
  if (!has_value) {
    return nullptr;
  }
  return value;
}

nlohmann::json maybe_double(bool has_value, double value) {
  if (!has_value) {
    return nullptr;
  }
  return value;
}

} // namespace

http_deal::http::message_generator host_probe_api::operator()(request_t& req) {
  const auto snap = service::get_host_probe_snapshot();

  nlohmann::json data = {
      {"status", snap.status},
      {"monitoring_enabled", service::is_host_probe_monitoring_enabled()},
      {"message", snap.message},
      {"host", snap.host},
      {"port", snap.port},
      {"user", snap.user},
      {"cpu_usage_percent", maybe_double(snap.has_cpu_usage_percent, snap.cpu_usage_percent)},
      {"mem_total_kb", maybe_int(snap.has_mem_total_kb, snap.mem_total_kb)},
      {"mem_available_kb", maybe_int(snap.has_mem_available_kb, snap.mem_available_kb)},
      {"mem_used_percent", maybe_double(snap.has_mem_used_percent, snap.mem_used_percent)},
      {"net_rx_bytes", maybe_int(snap.has_net_rx_bytes, snap.net_rx_bytes)},
      {"net_tx_bytes", maybe_int(snap.has_net_tx_bytes, snap.net_tx_bytes)},
      {"net_rx_bytes_per_sec",
       maybe_double(snap.has_net_rx_bytes_per_sec, snap.net_rx_bytes_per_sec)},
      {"net_tx_bytes_per_sec",
       maybe_double(snap.has_net_tx_bytes_per_sec, snap.net_tx_bytes_per_sec)},
      {"sample_interval_ms", maybe_int(snap.has_sample_interval_ms, snap.sample_interval_ms)},
      {"updated_at_ms", snap.updated_at_ms},
  };

  return reply_ok(req, data);
}

} // namespace api
