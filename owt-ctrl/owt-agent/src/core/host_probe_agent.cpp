#include "service/host_probe_agent.h"

#include "service/params_store.h"
#include "service/ssh_executor.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cctype>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace service {

namespace {

constexpr int kDefaultProbeIntervalMs = 1000;
constexpr int kMinProbeIntervalMs = 200;
constexpr int kMaxProbeIntervalMs = 60000;
constexpr const char* kProbeCommand =
    "sh -c \"echo __OWT_CPU__ $(sed -n '1p' /proc/stat); "
    "echo __OWT_MEM_TOTAL__ $(awk '/^MemTotal:/ {print $2}' /proc/meminfo); "
    "echo __OWT_MEM_AVAIL__ $(awk '/^MemAvailable:/ {print $2}' /proc/meminfo); "
    "echo __OWT_NET_BEGIN__; cat /proc/net/dev; echo __OWT_NET_END__\"";

std::mutex g_probe_mutex;
std::condition_variable g_probe_cv;
bool g_probe_running = false;
bool g_probe_enabled = true;
std::chrono::milliseconds g_probe_interval{kDefaultProbeIntervalMs};
std::thread g_probe_thread;
host_probe_snapshot g_snapshot;

struct probe_counters {
  bool valid = false;
  std::string key;
  uint64_t cpu_total = 0;
  uint64_t cpu_idle = 0;
  uint64_t rx_bytes = 0;
  uint64_t tx_bytes = 0;
  std::chrono::steady_clock::time_point sampled_at{};
};

std::string trim(std::string s) {
  const auto not_space = [](unsigned char c) { return !std::isspace(c); };
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
  s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
  return s;
}

int64_t now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

std::string sanitize_reason(std::string s) {
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

bool parse_cpu_line(const std::string& line, uint64_t& total, uint64_t& idle) {
  std::istringstream ss(line);
  std::string marker;
  std::string cpu_label;
  if (!(ss >> marker >> cpu_label) || marker != "__OWT_CPU__" || cpu_label != "cpu") {
    return false;
  }

  std::vector<uint64_t> values;
  std::string token;
  while (ss >> token) {
    uint64_t v = 0;
    if (!parse_u64(token, v)) {
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

bool parse_prefixed_u64(const std::string& line, const char* prefix, uint64_t& value) {
  const std::string p(prefix);
  if (line.rfind(p, 0) != 0) {
    return false;
  }
  return parse_u64(trim(line.substr(p.size())), value);
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

host_probe_snapshot make_offline_snapshot(const ssh_params& ssh, const std::string& reason) {
  host_probe_snapshot snap;
  snap.status = "offline";
  snap.message = reason;
  snap.host = ssh.host;
  snap.port = ssh.port;
  snap.user = ssh.user;
  snap.updated_at_ms = now_ms();
  return snap;
}

host_probe_snapshot make_paused_snapshot(const ssh_params& ssh) {
  host_probe_snapshot snap;
  snap.status = "paused";
  snap.message = "monitoring disabled";
  snap.host = ssh.host;
  snap.port = ssh.port;
  snap.user = ssh.user;
  snap.updated_at_ms = now_ms();
  return snap;
}

host_probe_snapshot parse_probe_output(
    const ssh_params& ssh,
    const std::string& output,
    probe_counters& counters) {
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

  std::istringstream in(output);
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
    if (!cpu_ok) {
      cpu_ok = parse_cpu_line(line, cpu_total, cpu_idle);
      if (cpu_ok) {
        continue;
      }
    }
    if (parse_prefixed_u64(line, "__OWT_MEM_TOTAL__", mem_total_kb)) {
      mem_total_ok = true;
      continue;
    }
    if (parse_prefixed_u64(line, "__OWT_MEM_AVAIL__", mem_avail_kb)) {
      mem_avail_ok = true;
      continue;
    }
    if (net_section) {
      parse_net_dev_line(line, rx_bytes, tx_bytes);
    }
  }

  host_probe_snapshot snap;
  snap.status = "online";
  snap.message = "target reachable";
  snap.host = ssh.host;
  snap.port = ssh.port;
  snap.user = ssh.user;
  snap.updated_at_ms = now_ms();

  if (cpu_ok) {
    const auto now = std::chrono::steady_clock::now();
    const std::string key = ssh.host + ":" + std::to_string(ssh.port) + ":" + ssh.user;

    if (counters.valid && counters.key == key && cpu_total >= counters.cpu_total &&
        cpu_idle >= counters.cpu_idle) {
      const auto delta_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                now - counters.sampled_at)
                                .count();
      if (delta_ms > 0) {
        snap.has_sample_interval_ms = true;
        snap.sample_interval_ms = static_cast<int>(delta_ms);

        const uint64_t total_delta = cpu_total - counters.cpu_total;
        const uint64_t idle_delta = cpu_idle - counters.cpu_idle;
        if (total_delta > 0 && idle_delta <= total_delta) {
          snap.has_cpu_usage_percent = true;
          snap.cpu_usage_percent = static_cast<double>(total_delta - idle_delta) * 100.0 /
                                   static_cast<double>(total_delta);
        }

        if (rx_bytes >= counters.rx_bytes && tx_bytes >= counters.tx_bytes) {
          const double seconds = static_cast<double>(delta_ms) / 1000.0;
          snap.has_net_rx_bytes_per_sec = true;
          snap.net_rx_bytes_per_sec =
              static_cast<double>(rx_bytes - counters.rx_bytes) / seconds;
          snap.has_net_tx_bytes_per_sec = true;
          snap.net_tx_bytes_per_sec =
              static_cast<double>(tx_bytes - counters.tx_bytes) / seconds;
        }
      }
    }

    counters.valid = true;
    counters.key = key;
    counters.cpu_total = cpu_total;
    counters.cpu_idle = cpu_idle;
    counters.rx_bytes = rx_bytes;
    counters.tx_bytes = tx_bytes;
    counters.sampled_at = now;
  } else {
    counters.valid = false;
  }

  snap.has_net_rx_bytes = true;
  snap.net_rx_bytes = rx_bytes;
  snap.has_net_tx_bytes = true;
  snap.net_tx_bytes = tx_bytes;

  if (mem_total_ok) {
    snap.has_mem_total_kb = true;
    snap.mem_total_kb = mem_total_kb;
  }
  if (mem_avail_ok) {
    snap.has_mem_available_kb = true;
    snap.mem_available_kb = mem_avail_kb;
  }
  if (mem_total_ok && mem_avail_ok && mem_total_kb > 0 && mem_avail_kb <= mem_total_kb) {
    snap.has_mem_used_percent = true;
    snap.mem_used_percent = static_cast<double>(mem_total_kb - mem_avail_kb) * 100.0 /
                            static_cast<double>(mem_total_kb);
  }

  return snap;
}

void probe_loop() {
  probe_counters counters;

  while (true) {
    bool enabled = false;
    std::chrono::milliseconds probe_interval{kDefaultProbeIntervalMs};
    {
      std::lock_guard<std::mutex> lk(g_probe_mutex);
      if (!g_probe_running) {
        break;
      }
      enabled = g_probe_enabled;
      probe_interval = g_probe_interval;
    }

    const auto params = load_control_params();
    const auto& ssh = params.ssh;

    host_probe_snapshot next;
    if (!enabled) {
      counters.valid = false;
      next = make_paused_snapshot(ssh);
    } else if (ssh.host.empty() || ssh.user.empty() || ssh.password.empty()) {
      counters.valid = false;
      next = make_offline_snapshot(ssh, "ssh params incomplete");
    } else {
      ssh_request req;
      req.host = ssh.host;
      req.port = ssh.port;
      req.user = ssh.user;
      req.password = ssh.password;
      req.timeout_ms = ssh.timeout_ms;
      req.command = kProbeCommand;

      const auto res = run_ssh_command(req);
      if (!res.ok) {
        counters.valid = false;
        const std::string reason = sanitize_reason(
            !res.error.empty() ? res.error
                               : (res.output.empty() ? "probe command failed" : res.output));
        next = make_offline_snapshot(ssh, reason);
      } else {
        next = parse_probe_output(ssh, res.output, counters);
      }
    }

    {
      std::lock_guard<std::mutex> lk(g_probe_mutex);
      g_snapshot = std::move(next);
    }

    std::unique_lock<std::mutex> lk(g_probe_mutex);
    if (g_probe_cv.wait_for(lk, probe_interval, []() { return !g_probe_running; })) {
      break;
    }
  }
}

} // namespace

void start_host_probe_agent(int status_collect_interval_ms) {
  std::lock_guard<std::mutex> lk(g_probe_mutex);
  if (g_probe_running) {
    return;
  }
  const int bounded_status_collect_interval_ms =
      std::max(kMinProbeIntervalMs, std::min(status_collect_interval_ms, kMaxProbeIntervalMs));
  g_probe_interval = std::chrono::milliseconds(bounded_status_collect_interval_ms);
  g_probe_running = true;
  g_probe_enabled = true;
  g_snapshot.status = "unknown";
  g_snapshot.message = "probing";
  g_snapshot.updated_at_ms = now_ms();
  g_probe_thread = std::thread(probe_loop);
}

void stop_host_probe_agent() {
  {
    std::lock_guard<std::mutex> lk(g_probe_mutex);
    if (!g_probe_running) {
      return;
    }
    g_probe_running = false;
  }
  g_probe_cv.notify_all();
  if (g_probe_thread.joinable()) {
    g_probe_thread.join();
  }
}

host_probe_snapshot get_host_probe_snapshot() {
  std::lock_guard<std::mutex> lk(g_probe_mutex);
  return g_snapshot;
}

bool is_host_probe_monitoring_enabled() {
  std::lock_guard<std::mutex> lk(g_probe_mutex);
  return g_probe_enabled;
}

void set_host_probe_monitoring_enabled(bool enabled) {
  {
    std::lock_guard<std::mutex> lk(g_probe_mutex);
    g_probe_enabled = enabled;
  }
  g_probe_cv.notify_all();
}

} // namespace service
