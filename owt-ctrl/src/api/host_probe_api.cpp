#include "api/host_probe_api.h"

#include "api/api_common.h"
#include "service/params_store.h"
#include "service/ssh_executor.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace api {

namespace {

constexpr const char* kProbeCommand =
    "sh -c \"echo __OWT_CPU__ $(sed -n '1p' /proc/stat); "
    "echo __OWT_MEM_TOTAL__ $(awk '/^MemTotal:/ {print $2}' /proc/meminfo); "
    "echo __OWT_MEM_AVAIL__ $(awk '/^MemAvailable:/ {print $2}' /proc/meminfo); "
    "echo __OWT_NET_BEGIN__; cat /proc/net/dev; echo __OWT_NET_END__\"";

struct probe_cache_entry {
  bool valid = false;
  uint64_t cpu_total = 0;
  uint64_t cpu_idle = 0;
  uint64_t rx_bytes = 0;
  uint64_t tx_bytes = 0;
  std::chrono::steady_clock::time_point sampled_at{};
};

std::mutex g_probe_mutex;
std::unordered_map<std::string, probe_cache_entry> g_probe_cache;

std::string trim(std::string s) {
  const auto not_space = [](unsigned char c) { return !std::isspace(c); };
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
  s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
  return s;
}

std::string squash_linebreaks(std::string s) {
  for (char& c : s) {
    if (c == '\r' || c == '\n' || c == '\t') {
      c = ' ';
    }
  }
  s = trim(std::move(s));
  if (s.size() > 180) {
    s.resize(180);
    s += "...";
  }
  return s;
}

bool parse_u64(const std::string& s, uint64_t& out) {
  if (s.empty()) {
    return false;
  }
  uint64_t value = 0;
  for (char c : s) {
    if (c < '0' || c > '9') {
      return false;
    }
    value = value * 10 + static_cast<uint64_t>(c - '0');
  }
  out = value;
  return true;
}

std::vector<std::string> split_ws(const std::string& line) {
  std::istringstream ss(line);
  std::vector<std::string> out;
  std::string token;
  while (ss >> token) {
    out.push_back(token);
  }
  return out;
}

bool parse_cpu_line(const std::string& line, uint64_t& total, uint64_t& idle) {
  const auto tokens = split_ws(line);
  size_t cpu_idx = tokens.size();
  for (size_t i = 0; i < tokens.size(); ++i) {
    if (tokens[i] == "cpu") {
      cpu_idx = i;
      break;
    }
  }
  if (cpu_idx == tokens.size() || cpu_idx + 5 >= tokens.size()) {
    return false;
  }

  std::vector<uint64_t> values;
  values.reserve(tokens.size() - cpu_idx - 1);
  for (size_t i = cpu_idx + 1; i < tokens.size(); ++i) {
    uint64_t v = 0;
    if (!parse_u64(tokens[i], v)) {
      break;
    }
    values.push_back(v);
  }
  if (values.size() < 5) {
    return false;
  }

  total = 0;
  for (uint64_t v : values) {
    total += v;
  }
  idle = values[3] + values[4];
  return true;
}

bool parse_mem_line(const std::string& line, const char* prefix, uint64_t& value) {
  const std::string p(prefix);
  if (line.rfind(p, 0) != 0) {
    return false;
  }
  const std::string number = trim(line.substr(p.size()));
  return parse_u64(number, value);
}

bool parse_net_dev_line(const std::string& line, uint64_t& rx, uint64_t& tx) {
  const auto colon = line.find(':');
  if (colon == std::string::npos) {
    return false;
  }
  const std::string iface = trim(line.substr(0, colon));
  if (iface.empty() || iface == "lo") {
    return false;
  }

  std::istringstream ss(line.substr(colon + 1));
  uint64_t rx_bytes = 0;
  uint64_t tx_bytes = 0;
  uint64_t throwaway = 0;

  if (!(ss >> rx_bytes)) {
    return false;
  }
  for (int i = 0; i < 7; ++i) {
    if (!(ss >> throwaway)) {
      return false;
    }
  }
  if (!(ss >> tx_bytes)) {
    return false;
  }

  rx += rx_bytes;
  tx += tx_bytes;
  return true;
}

nlohmann::json make_offline_payload(const service::ssh_params& ssh, const std::string& reason) {
  return {
      {"status", "offline"},
      {"message", reason},
      {"host", ssh.host},
      {"port", ssh.port},
      {"user", ssh.user},
      {"cpu_usage_percent", nullptr},
      {"mem_total_kb", nullptr},
      {"mem_available_kb", nullptr},
      {"mem_used_percent", nullptr},
      {"net_rx_bytes", nullptr},
      {"net_tx_bytes", nullptr},
      {"net_rx_bytes_per_sec", nullptr},
      {"net_tx_bytes_per_sec", nullptr},
      {"sample_interval_ms", nullptr},
  };
}

nlohmann::json make_online_payload(
    const service::ssh_params& ssh,
    uint64_t cpu_total,
    uint64_t cpu_idle,
    bool cpu_ok,
    uint64_t mem_total_kb,
    bool mem_total_ok,
    uint64_t mem_avail_kb,
    bool mem_avail_ok,
    uint64_t rx_bytes,
    uint64_t tx_bytes) {
  nlohmann::json payload = {
      {"status", "online"},
      {"message", "target reachable"},
      {"host", ssh.host},
      {"port", ssh.port},
      {"user", ssh.user},
      {"cpu_usage_percent", nullptr},
      {"mem_total_kb", mem_total_ok ? nlohmann::json(mem_total_kb) : nlohmann::json(nullptr)},
      {"mem_available_kb", mem_avail_ok ? nlohmann::json(mem_avail_kb) : nlohmann::json(nullptr)},
      {"mem_used_percent", nullptr},
      {"net_rx_bytes", rx_bytes},
      {"net_tx_bytes", tx_bytes},
      {"net_rx_bytes_per_sec", nullptr},
      {"net_tx_bytes_per_sec", nullptr},
      {"sample_interval_ms", nullptr},
  };

  if (mem_total_ok && mem_avail_ok && mem_total_kb > 0 && mem_avail_kb <= mem_total_kb) {
    const double used = static_cast<double>(mem_total_kb - mem_avail_kb);
    payload["mem_used_percent"] = used * 100.0 / static_cast<double>(mem_total_kb);
  }

  const auto now = std::chrono::steady_clock::now();
  const std::string cache_key =
      ssh.host + ":" + std::to_string(ssh.port) + ":" + ssh.user;

  std::lock_guard<std::mutex> lk(g_probe_mutex);
  probe_cache_entry& entry = g_probe_cache[cache_key];
  if (entry.valid) {
    const auto delta_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - entry.sampled_at).count();
    if (delta_ms > 0) {
      payload["sample_interval_ms"] = delta_ms;

      if (rx_bytes >= entry.rx_bytes && tx_bytes >= entry.tx_bytes) {
        const double seconds = static_cast<double>(delta_ms) / 1000.0;
        payload["net_rx_bytes_per_sec"] =
            static_cast<double>(rx_bytes - entry.rx_bytes) / seconds;
        payload["net_tx_bytes_per_sec"] =
            static_cast<double>(tx_bytes - entry.tx_bytes) / seconds;
      }

      if (cpu_ok && cpu_total >= entry.cpu_total && cpu_idle >= entry.cpu_idle) {
        const uint64_t total_delta = cpu_total - entry.cpu_total;
        const uint64_t idle_delta = cpu_idle - entry.cpu_idle;
        if (total_delta > 0 && idle_delta <= total_delta) {
          payload["cpu_usage_percent"] =
              static_cast<double>(total_delta - idle_delta) * 100.0 /
              static_cast<double>(total_delta);
        }
      }
    }
  }

  entry.valid = true;
  entry.cpu_total = cpu_total;
  entry.cpu_idle = cpu_idle;
  entry.rx_bytes = rx_bytes;
  entry.tx_bytes = tx_bytes;
  entry.sampled_at = now;

  return payload;
}

} // namespace

http_deal::http::message_generator host_probe_api::operator()(request_t& req) {
  const auto params = service::load_control_params();
  const auto& ssh = params.ssh;

  if (ssh.host.empty() || ssh.user.empty() || ssh.password.empty()) {
    return reply_ok(req, make_offline_payload(ssh, "ssh params incomplete"));
  }

  service::ssh_request ssh_req;
  ssh_req.host = ssh.host;
  ssh_req.port = ssh.port;
  ssh_req.user = ssh.user;
  ssh_req.password = ssh.password;
  ssh_req.timeout_ms = ssh.timeout_ms;
  ssh_req.command = kProbeCommand;

  const auto res = service::run_ssh_command(ssh_req);
  if (!res.ok) {
    const std::string reason = squash_linebreaks(
        !res.error.empty() ? res.error : (res.output.empty() ? "probe command failed" : res.output));
    return reply_ok(req, make_offline_payload(ssh, reason));
  }

  uint64_t cpu_total = 0;
  uint64_t cpu_idle = 0;
  bool cpu_ok = false;

  uint64_t mem_total_kb = 0;
  uint64_t mem_avail_kb = 0;
  bool mem_total_ok = false;
  bool mem_avail_ok = false;

  uint64_t rx_bytes = 0;
  uint64_t tx_bytes = 0;
  bool net_section = false;

  std::istringstream in(res.output);
  std::string line;
  while (std::getline(in, line)) {
    line = trim(std::move(line));
    if (line.empty()) {
      continue;
    }
    if (line == "__OWT_NET_BEGIN__") {
      net_section = true;
      continue;
    }
    if (line == "__OWT_NET_END__") {
      net_section = false;
      continue;
    }
    if (line.rfind("__OWT_CPU__", 0) == 0) {
      cpu_ok = parse_cpu_line(line, cpu_total, cpu_idle);
      continue;
    }
    if (parse_mem_line(line, "__OWT_MEM_TOTAL__", mem_total_kb)) {
      mem_total_ok = true;
      continue;
    }
    if (parse_mem_line(line, "__OWT_MEM_AVAIL__", mem_avail_kb)) {
      mem_avail_ok = true;
      continue;
    }
    if (net_section) {
      parse_net_dev_line(line, rx_bytes, tx_bytes);
    }
  }

  return reply_ok(
      req,
      make_online_payload(
          ssh,
          cpu_total,
          cpu_idle,
          cpu_ok,
          mem_total_kb,
          mem_total_ok,
          mem_avail_kb,
          mem_avail_ok,
          rx_bytes,
          tx_bytes));
}

} // namespace api
