#include <cerrno>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <csignal>
#include <cstring>
#include <cstdlib>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include "libcore/board_config.hpp"
#include "libcore/demand_policy.hpp"
#include "libcore/fancontrol_core.hpp"
#include "libcore/json.hpp"
#include "libcore/pwm_controller.hpp"
#include "libcore/safety_guard.hpp"
#include "libcore/temp_source.hpp"

namespace {

volatile std::sig_atomic_t g_stop = 0;
volatile std::sig_atomic_t g_restore_status = 0;

constexpr std::string_view kRuntimeStatusPath = "/var/run/fancontrol.status.json";
constexpr std::string_view kDefaultConfigPath = "/etc/fancontrol.conf";
constexpr std::string_view kDefaultThermalModePath = "/sys/class/thermal/thermal_zone0/mode";
constexpr std::string_view kDefaultPwmPath = "/sys/class/hwmon/hwmon2/pwm1";
constexpr std::string_view kDefaultPwmEnablePath = "/sys/class/hwmon/hwmon2/pwm1_enable";

void on_signal(int sig) {
    switch (sig) {
    case SIGHUP:
    case SIGINT:
        g_restore_status = 1;
        break;
    case SIGQUIT:
    case SIGTERM:
    default:
        g_restore_status = 0;
        break;
    }
    g_stop = 1;
}

bool file_exists(const std::string &path) {
    struct stat st {};
    return ::stat(path.c_str(), &st) == 0;
}

std::optional<int> try_read_int(const std::string &path) {
    std::ifstream in(path);
    if (!in) {
        return std::nullopt;
    }
    int value = 0;
    in >> value;
    if (in.fail()) {
        return std::nullopt;
    }
    return value;
}

std::optional<std::string> try_read_text(const std::string &path) {
    std::ifstream in(path);
    if (!in) {
        return std::nullopt;
    }

    std::string value;
    if (!std::getline(in, value)) {
        return std::nullopt;
    }

    std::size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) {
        ++begin;
    }
    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    value = value.substr(begin, end - begin);
    if (value.empty()) {
        return std::nullopt;
    }
    return value;
}

bool try_write_int(const std::string &path, int value) {
    std::ofstream out(path);
    if (!out) {
        return false;
    }
    out << value << '\n';
    return out.good();
}

bool try_write_text(const std::string &path, std::string_view value) {
    std::ofstream out(path);
    if (!out) {
        return false;
    }
    out << value << '\n';
    return out.good();
}

std::string trim_ascii(const std::string &s) {
    std::size_t begin = 0;
    while (begin < s.size() && std::isspace(static_cast<unsigned char>(s[begin]))) {
        ++begin;
    }
    std::size_t end = s.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
        --end;
    }
    return s.substr(begin, end - begin);
}

std::string to_lower_ascii(std::string s) {
    for (char &c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

std::string sanitize_field(std::string s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '\r' || c == '\n' || c == ';') {
            continue;
        }
        out.push_back(c);
    }
    return trim_ascii(out);
}

std::vector<std::string> split(const std::string &text, char delim) {
    std::vector<std::string> out;
    std::string part;
    std::istringstream in(text);
    while (std::getline(in, part, delim)) {
        out.push_back(part);
    }
    if (!text.empty() && text.back() == delim) {
        out.emplace_back();
    }
    return out;
}

std::string json_value_to_text(const nlohmann::json &obj, const char *key) {
    if (!obj.is_object()) {
        return {};
    }
    const auto it = obj.find(key);
    if (it == obj.end() || it->is_null()) {
        return {};
    }
    if (it->is_string()) {
        return it->get<std::string>();
    }
    if (it->is_boolean()) {
        return it->get<bool>() ? "1" : "0";
    }
    if (it->is_number_integer()) {
        return std::to_string(it->get<long long>());
    }
    if (it->is_number_unsigned()) {
        return std::to_string(it->get<unsigned long long>());
    }
    if (it->is_number_float()) {
        std::ostringstream oss;
        oss << it->get<double>();
        return oss.str();
    }
    return {};
}

int parse_int_relaxed(const std::string &raw, int def) {
    const std::string text = trim_ascii(raw);
    if (text.empty()) {
        return def;
    }
    try {
        std::size_t idx = 0;
        const long long v = std::stoll(text, &idx, 10);
        if (idx != text.size()) {
            return def;
        }
        if (v < static_cast<long long>(std::numeric_limits<int>::min()) ||
            v > static_cast<long long>(std::numeric_limits<int>::max())) {
            return def;
        }
        return static_cast<int>(v);
    } catch (...) {
        return def;
    }
}

std::string normalize_control_mode(std::string raw) {
    raw = to_lower_ascii(sanitize_field(std::move(raw)));
    if (raw == "kernel" || raw == "user") {
        return raw;
    }
    throw std::runtime_error("CONTROL_MODE must be one of: kernel, user");
}

int parse_bool01(std::string raw, int def) {
    raw = to_lower_ascii(sanitize_field(std::move(raw)));
    if (raw.empty()) {
        return def;
    }
    if (raw == "1" || raw == "true" || raw == "yes" || raw == "on") {
        return 1;
    }
    if (raw == "0" || raw == "false" || raw == "no" || raw == "off") {
        return 0;
    }
    throw std::runtime_error("PWM_INVERTED must be boolean (0/1/true/false)");
}

fancontrol::core::BoardConfig default_board_config() {
    fancontrol::core::BoardConfig cfg;
    cfg.interval_sec = 1;
    cfg.control_mode = "kernel";
    cfg.pwm_path = std::string(kDefaultPwmPath);
    cfg.pwm_enable_path = std::string(kDefaultPwmEnablePath);
    cfg.thermal_mode_path = std::string(kDefaultThermalModePath);
    cfg.pwm_min = 0;
    cfg.pwm_max = 255;
    cfg.pwm_inverted = true;
    cfg.ramp_up = 25;
    cfg.ramp_down = 8;
    cfg.hysteresis_mC = 2000;
    cfg.policy = "max";
    cfg.failsafe_pwm = 64;

    cfg.sources.push_back({
        "soc",
        "sysfs",
        "/sys/class/thermal/thermal_zone0/temp",
        "",
        "",
        "",
        "",
        60000,
        82000,
        90000,
        6,
        1,
        100,
    });
    cfg.sources.push_back({
        "nvme",
        "sysfs",
        "/sys/class/nvme/nvme0/hwmon1/temp1_input",
        "",
        "",
        "",
        "",
        50000,
        70000,
        80000,
        6,
        1,
        120,
    });
    cfg.sources.push_back({
        "rm500",
        "ubus",
        "",
        "qmodem",
        "get_temperature",
        "temp_mC",
        "{\"config_section\":\"2_1\"}",
        58000,
        76000,
        85000,
        20,
        10,
        130,
    });
    return cfg;
}

std::optional<pid_t> try_read_pid(const std::string &path) {
    std::ifstream in(path);
    if (!in) {
        return std::nullopt;
    }
    long long pid = 0;
    in >> pid;
    if (in.fail()) {
        return std::nullopt;
    }
    if (pid <= 0 || pid > static_cast<long long>(std::numeric_limits<pid_t>::max())) {
        return std::nullopt;
    }
    return static_cast<pid_t>(pid);
}

class InstanceLock {
public:
    explicit InstanceLock(const std::string &pidfile) : lockfile_(pidfile + ".lock") {
        fd_ = ::open(lockfile_.c_str(), O_CREAT | O_RDWR, 0644);
        if (fd_ < 0) {
            throw std::runtime_error("cannot open lock file " + lockfile_ + ": " + std::strerror(errno));
        }

        if (::flock(fd_, LOCK_EX | LOCK_NB) != 0) {
            const int err = errno;
            std::string msg = "cannot acquire lock " + lockfile_ + ": " + std::strerror(err);
            const auto existing_pid = try_read_pid(pidfile);
            if (existing_pid) {
                msg += " (existing pidfile pid " + std::to_string(*existing_pid) + ")";
            }
            ::close(fd_);
            fd_ = -1;
            throw std::runtime_error(msg);
        }
    }

    ~InstanceLock() {
        if (fd_ >= 0) {
            (void)::flock(fd_, LOCK_UN);
            (void)::close(fd_);
        }
    }

    InstanceLock(const InstanceLock &) = delete;
    InstanceLock &operator=(const InstanceLock &) = delete;

private:
    int fd_ = -1;
    std::string lockfile_;
};

struct BoardPwmSnapshot {
    std::optional<int> orig_enable;
    bool has_enable = false;
};

bool setup_board_pwm(const fancontrol::core::BoardConfig &cfg, BoardPwmSnapshot &snap, bool debug) {
    snap.has_enable = file_exists(cfg.pwm_enable_path);
    if (snap.has_enable) {
        snap.orig_enable = try_read_int(cfg.pwm_enable_path);
    }

    if (snap.has_enable && !try_write_int(cfg.pwm_enable_path, 1)) {
        return false;
    }

    if (debug && snap.has_enable) {
        std::cerr << "Set " << cfg.pwm_enable_path << " to 1\n";
    }
    return true;
}

void restore_board_pwm(const fancontrol::core::BoardConfig &cfg, const BoardPwmSnapshot &snap, bool debug) {
    if (snap.has_enable) {
        const int enable_to_restore = snap.orig_enable.value_or(0);
        if (debug) {
            std::cerr << "Restoring " << cfg.pwm_enable_path << " to " << enable_to_restore << "\n";
        }
        if (!try_write_int(cfg.pwm_enable_path, enable_to_restore)) {
            (void)try_write_int(cfg.pwm_enable_path, enable_to_restore);
            (void)try_write_int(cfg.pwm_path, fancontrol::core::max_cooling_pwm(cfg));
        }
    }
}

class PidfileGuard {
public:
    explicit PidfileGuard(std::string pidfile) : pidfile_(std::move(pidfile)) {
        std::ofstream pid(pidfile_);
        if (!pid) {
            throw std::runtime_error("cannot create pidfile: " + pidfile_);
        }
        pid << ::getpid() << '\n';
        if (!pid.good()) {
            throw std::runtime_error("cannot write pidfile: " + pidfile_);
        }
    }

    ~PidfileGuard() {
        (void)::unlink(pidfile_.c_str());
    }

    PidfileGuard(const PidfileGuard &) = delete;
    PidfileGuard &operator=(const PidfileGuard &) = delete;

private:
    std::string pidfile_;
};

class BoardPwmGuard {
public:
    BoardPwmGuard(const fancontrol::core::BoardConfig &cfg, bool debug) : cfg_(cfg), debug_(debug) {
        if (cfg_.control_mode != "user") {
            return;
        }
        if (!setup_board_pwm(cfg_, snapshot_, debug_)) {
            throw std::runtime_error("failed to enable PWM in board mode");
        }
        armed_ = true;
    }

    ~BoardPwmGuard() {
        if (armed_) {
            restore_board_pwm(cfg_, snapshot_, debug_);
        }
    }

    BoardPwmGuard(const BoardPwmGuard &) = delete;
    BoardPwmGuard &operator=(const BoardPwmGuard &) = delete;

private:
    const fancontrol::core::BoardConfig &cfg_;
    BoardPwmSnapshot snapshot_;
    bool debug_ = false;
    bool armed_ = false;
};

class RuntimeStatusGuard {
public:
    explicit RuntimeStatusGuard(std::string path) : path_(std::move(path)) {}

    ~RuntimeStatusGuard() {
        (void)::unlink(path_.c_str());
    }

    bool write(const std::string &payload) const {
        return fancontrol::core::write_runtime_status_file(path_, payload);
    }

    RuntimeStatusGuard(const RuntimeStatusGuard &) = delete;
    RuntimeStatusGuard &operator=(const RuntimeStatusGuard &) = delete;

private:
    std::string path_;
};

class BoardOwnershipGuard {
public:
    BoardOwnershipGuard(const fancontrol::core::BoardConfig &cfg, bool debug)
        : instance_lock_(fancontrol::core::kFixedPidfilePath),
          pidfile_guard_(fancontrol::core::kFixedPidfilePath),
          pwm_guard_(cfg, debug),
          status_guard_(std::string(kRuntimeStatusPath)) {}

    bool write_runtime_status(const std::string &payload) const {
        return status_guard_.write(payload);
    }

    BoardOwnershipGuard(const BoardOwnershipGuard &) = delete;
    BoardOwnershipGuard &operator=(const BoardOwnershipGuard &) = delete;

private:
    InstanceLock instance_lock_;
    PidfileGuard pidfile_guard_;
    BoardPwmGuard pwm_guard_;
    RuntimeStatusGuard status_guard_;
};

bool create_board_sources(const fancontrol::core::BoardConfig &cfg,
                          fancontrol::core::SourceManager &mgr,
                          std::unordered_map<std::string, fancontrol::core::BoardSourceConfig> &by_id) {
    for (const auto &src : cfg.sources) {
        by_id[src.id] = src;
        if (src.type == "sysfs") {
            mgr.add(std::make_unique<fancontrol::core::SysfsTempSource>(
                src.id, src.path, std::chrono::seconds(src.poll_sec)));
        } else if (src.type == "ubus") {
            mgr.add(std::make_unique<fancontrol::core::UbusTempSource>(
                src.id, src.object, src.method, src.key, src.args_json, std::chrono::seconds(src.poll_sec)));
        } else {
            std::cerr << "Unsupported source type: " << src.type << "\n";
            return false;
        }
    }
    return true;
}

std::string desired_thermal_mode(const fancontrol::core::BoardConfig &cfg) {
    return (cfg.control_mode == "user") ? "disabled" : "enabled";
}

int run_board_mode(const fancontrol::core::BoardConfig &cfg, bool debug) {
    const bool fancontrol_should_own_pwm = (cfg.control_mode == "user");
    if (fancontrol_should_own_pwm) {
        if (::access(cfg.pwm_path.c_str(), W_OK) != 0) {
            throw std::runtime_error("PWM path is not writable: " + cfg.pwm_path);
        }
    } else if (::access(cfg.pwm_path.c_str(), R_OK) != 0) {
        throw std::runtime_error("PWM path is not readable: " + cfg.pwm_path);
    }
    if (fancontrol_should_own_pwm && file_exists(cfg.pwm_enable_path) && ::access(cfg.pwm_enable_path.c_str(), W_OK) != 0) {
        throw std::runtime_error("PWM enable path is not writable: " + cfg.pwm_enable_path);
    }
    if (::access(cfg.thermal_mode_path.c_str(), W_OK) != 0) {
        throw std::runtime_error("thermal mode path is not writable: " + cfg.thermal_mode_path);
    }

    BoardOwnershipGuard ownership_guard(cfg, debug);

    fancontrol::core::SourceManager mgr;
    std::unordered_map<std::string, fancontrol::core::BoardSourceConfig> by_id;
    if (!create_board_sources(cfg, mgr, by_id)) {
        throw std::runtime_error("failed to create temperature sources");
    }

    std::unordered_map<std::string, bool> active_state;
    int current_pwm = try_read_int(cfg.pwm_path).value_or(fancontrol::core::min_cooling_pwm(cfg));
    const std::string thermal_mode_target = desired_thermal_mode(cfg);
    std::optional<std::string> last_observed_mode;
    mgr.start(debug);
    std::cerr << "Starting board-mode fan control...\n";
    while (!g_stop) {
        std::string thermal_mode = try_read_text(cfg.thermal_mode_path).value_or("");
        if (thermal_mode != thermal_mode_target) {
            if (!try_write_text(cfg.thermal_mode_path, thermal_mode_target)) {
                throw std::runtime_error("failed to enforce thermal mode via " + cfg.thermal_mode_path);
            }
            thermal_mode = try_read_text(cfg.thermal_mode_path).value_or("");
        }
        if (!last_observed_mode || *last_observed_mode != thermal_mode) {
            if (debug) {
                std::cerr << "control owner: "
                          << (fancontrol_should_own_pwm ? "fancontrol" : "kernel")
                          << " (thermal mode=" << (thermal_mode.empty() ? "<unknown>" : thermal_mode) << ")\n";
            }
            last_observed_mode = thermal_mode;
        }

        std::vector<fancontrol::core::SourceTelemetry> telemetry;
        const fancontrol::core::TargetDecision decision = fancontrol::core::compute_target_decision(
            cfg, mgr, by_id, active_state, telemetry, debug);
        const int target = decision.target_pwm;

        if (const auto observed_pwm = try_read_int(cfg.pwm_path)) {
            current_pwm = *observed_pwm;
        }

        int applied_pwm = current_pwm;
        if (fancontrol_should_own_pwm) {
            int next_pwm = fancontrol::core::apply_ramp(current_pwm, target, cfg);

            if (next_pwm != current_pwm) {
                if (!try_write_int(cfg.pwm_path, next_pwm)) {
                    throw std::runtime_error("Error writing PWM value to " + cfg.pwm_path);
                }
                current_pwm = next_pwm;
                if (debug) {
                    std::cerr << "board pwm target=" << target << " applied=" << current_pwm << "\n";
                }
            }
            applied_pwm = current_pwm;
        }

        const std::string status_payload =
            fancontrol::core::build_runtime_status_json(cfg, decision, current_pwm, target, applied_pwm, telemetry);
        if (!ownership_guard.write_runtime_status(status_payload) && debug) {
            std::cerr << "warning: failed to write runtime status to " << kRuntimeStatusPath << "\n";
        }

        for (int i = 0; i < cfg.interval_sec && !g_stop; ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    return (g_restore_status == 0) ? 0 : 1;
}

std::string pick_config_path(const std::vector<std::string> &args, std::size_t offset) {
    if (args.size() > offset) {
        return args[offset];
    }
    return std::string(kDefaultConfigPath);
}

std::string build_board_config_json(const fancontrol::core::BoardConfig &cfg, const std::string &path, bool exists) {
    nlohmann::json root = {
        {"ok", 1},
        {"path", path},
        {"exists", exists ? 1 : 0},
        {"interval", cfg.interval_sec},
        {"control_mode", cfg.control_mode},
        {"pwm_path", cfg.pwm_path},
        {"pwm_enable_path", cfg.pwm_enable_path},
        {"thermal_mode_path", cfg.thermal_mode_path},
        {"pwm_min", cfg.pwm_min},
        {"pwm_max", cfg.pwm_max},
        {"pwm_inverted", cfg.pwm_inverted ? 1 : 0},
        {"ramp_up", cfg.ramp_up},
        {"ramp_down", cfg.ramp_down},
        {"hysteresis_mC", cfg.hysteresis_mC},
        {"policy", cfg.policy},
        {"failsafe_pwm", cfg.failsafe_pwm},
        {"sources", nlohmann::json::array()}
    };

    for (const auto &src : cfg.sources) {
        root["sources"].push_back({
            {"id", src.id},
            {"type", src.type},
            {"path", src.path},
            {"object", src.object},
            {"method", src.method},
            {"key", src.key},
            {"args", src.args_json},
            {"t_start", src.t_start_mC},
            {"t_full", src.t_full_mC},
            {"t_crit", src.t_crit_mC},
            {"ttl", src.ttl_sec},
            {"poll", src.poll_sec},
            {"weight", src.weight}
        });
    }

    return root.dump();
}

std::string build_effective_config_json(const std::string &path) {
    if (file_exists(path)) {
        return build_board_config_json(fancontrol::core::load_board_config(path), path, true);
    }
    return build_board_config_json(default_board_config(), path, false);
}

std::string render_board_config_from_rpc_json(const nlohmann::json &payload) {
    const int interval = parse_int_relaxed(json_value_to_text(payload, "interval"), 1);
    const std::string control_mode = normalize_control_mode(json_value_to_text(payload, "control_mode"));
    const std::string pwm_path = sanitize_field(json_value_to_text(payload, "pwm_path"));
    std::string pwm_enable_path = sanitize_field(json_value_to_text(payload, "pwm_enable_path"));
    std::string thermal_mode_path = sanitize_field(json_value_to_text(payload, "thermal_mode_path"));
    const int pwm_min = parse_int_relaxed(json_value_to_text(payload, "pwm_min"), 0);
    const int pwm_max = parse_int_relaxed(json_value_to_text(payload, "pwm_max"), 255);
    const int pwm_inverted = parse_bool01(json_value_to_text(payload, "pwm_inverted"), 1);
    const int ramp_up = parse_int_relaxed(json_value_to_text(payload, "ramp_up"), 25);
    const int ramp_down = parse_int_relaxed(json_value_to_text(payload, "ramp_down"), 8);
    const int hysteresis_mC = parse_int_relaxed(json_value_to_text(payload, "hysteresis_mC"), 2000);
    std::string policy = to_lower_ascii(sanitize_field(json_value_to_text(payload, "policy")));
    const int failsafe_pwm = parse_int_relaxed(json_value_to_text(payload, "failsafe_pwm"), 64);
    const std::string entries = json_value_to_text(payload, "entries");

    if (interval < 1) {
        throw std::runtime_error("INTERVAL must be >= 1");
    }
    if (pwm_path.empty()) {
        throw std::runtime_error("missing PWM path");
    }
    if (pwm_min < 0 || pwm_min > 255) {
        throw std::runtime_error("PWM_MIN must be in range [0, 255]");
    }
    if (pwm_max < 0 || pwm_max > 255) {
        throw std::runtime_error("PWM_MAX must be in range [0, 255]");
    }
    if (pwm_min > pwm_max) {
        throw std::runtime_error("PWM_MIN must be <= PWM_MAX");
    }
    if (failsafe_pwm < 0 || failsafe_pwm > 255) {
        throw std::runtime_error("FAILSAFE_PWM must be in range [0, 255]");
    }
    if (ramp_up < 1) {
        throw std::runtime_error("RAMP_UP must be >= 1");
    }
    if (ramp_down < 1) {
        throw std::runtime_error("RAMP_DOWN must be >= 1");
    }
    if (hysteresis_mC < 0) {
        throw std::runtime_error("HYSTERESIS_MC must be >= 0");
    }
    if (policy.empty()) {
        policy = "max";
    }
    if (policy != "max") {
        throw std::runtime_error("unsupported POLICY: " + policy + " (only 'max' is supported)");
    }

    if (pwm_enable_path.empty()) {
        pwm_enable_path = pwm_path + "_enable";
    }
    if (thermal_mode_path.empty()) {
        thermal_mode_path = std::string(kDefaultThermalModePath);
    }

    std::vector<std::string> source_lines;
    for (const auto &line_raw : split(entries, '\n')) {
        if (line_raw.empty()) {
            continue;
        }
        const auto cols = split(line_raw, ';');
        if (cols.size() != 14) {
            throw std::runtime_error("invalid source entry format");
        }

        const std::string enabled = sanitize_field(cols[0]);
        if (enabled != "1") {
            continue;
        }

        const std::string sid = sanitize_field(cols[1]);
        const std::string type = to_lower_ascii(sanitize_field(cols[2]));
        const std::string source_path = sanitize_field(cols[3]);
        const std::string object = sanitize_field(cols[4]);
        const std::string method = sanitize_field(cols[5]);
        const std::string key = sanitize_field(cols[6]);
        std::string args = sanitize_field(cols[7]);
        const int t_start = parse_int_relaxed(cols[8], 60000);
        const int t_full = parse_int_relaxed(cols[9], 80000);
        const int t_crit = parse_int_relaxed(cols[10], 90000);
        const std::string ttl_raw = trim_ascii(cols[11]);
        const std::string poll_raw = trim_ascii(cols[12]);
        const int poll = poll_raw.empty() ? interval : parse_int_relaxed(poll_raw, interval);
        const int ttl = ttl_raw.empty() ? std::max(poll * 2, interval * 2) : parse_int_relaxed(ttl_raw, 10);
        const int weight = parse_int_relaxed(cols[13], 100);

        if (sid.empty()) {
            throw std::runtime_error("empty SOURCE id is not allowed");
        }
        if (args.empty()) {
            args = "{}";
        }

        if (type == "sysfs") {
            source_lines.push_back("SOURCE_" + sid + "=type=sysfs,path=" + source_path + ",t_start=" +
                                   std::to_string(t_start) + ",t_full=" + std::to_string(t_full) + ",t_crit=" +
                                   std::to_string(t_crit) + ",ttl=" + std::to_string(ttl) +
                                   ",poll=" + std::to_string(poll) + ",weight=" + std::to_string(weight));
        } else if (type == "ubus") {
            source_lines.push_back("SOURCE_" + sid + "=type=ubus,object=" + object + ",method=" + method +
                                   ",key=" + key + ",args=" + args + ",t_start=" + std::to_string(t_start) +
                                   ",t_full=" + std::to_string(t_full) + ",t_crit=" + std::to_string(t_crit) +
                                   ",ttl=" + std::to_string(ttl) + ",poll=" + std::to_string(poll) +
                                   ",weight=" + std::to_string(weight));
        } else {
            throw std::runtime_error("unsupported source type for SOURCE_" + sid + ": " + type);
        }
    }

    if (source_lines.empty()) {
        throw std::runtime_error("no active sources selected");
    }

    std::ostringstream out;
    out << "# Configuration file generated by fancontrol\n";
    out << "INTERVAL=" << interval << '\n';
    out << "CONTROL_MODE=" << control_mode << '\n';
    out << "PWM_PATH=" << pwm_path << '\n';
    out << "PWM_ENABLE_PATH=" << pwm_enable_path << '\n';
    out << "THERMAL_MODE_PATH=" << thermal_mode_path << '\n';
    out << "PWM_MIN=" << pwm_min << '\n';
    out << "PWM_MAX=" << pwm_max << '\n';
    out << "PWM_INVERTED=" << pwm_inverted << '\n';
    out << "RAMP_UP=" << ramp_up << '\n';
    out << "RAMP_DOWN=" << ramp_down << '\n';
    out << "HYSTERESIS_MC=" << hysteresis_mC << '\n';
    out << "POLICY=" << policy << '\n';
    out << "FAILSAFE_PWM=" << failsafe_pwm << '\n';
    for (const auto &line : source_lines) {
        out << line << '\n';
    }

    return out.str();
}

void write_validated_config_from_rpc_json(const std::string &config_path, const std::string &payload_text) {
    nlohmann::json payload;
    try {
        payload = nlohmann::json::parse(payload_text);
    } catch (const std::exception &e) {
        throw std::runtime_error(std::string("invalid rpc payload json: ") + e.what());
    }
    if (!payload.is_object()) {
        throw std::runtime_error("rpc payload must be a JSON object");
    }

    const std::string rendered = render_board_config_from_rpc_json(payload);
    std::string temp_template = config_path + ".tmp.XXXXXX";
    std::vector<char> temp_buf(temp_template.begin(), temp_template.end());
    temp_buf.push_back('\0');
    const int temp_fd = ::mkstemp(temp_buf.data());
    if (temp_fd < 0) {
        throw std::runtime_error("cannot create temporary file");
    }
    (void)::close(temp_fd);
    const std::string temp_path = temp_buf.data();
    {
        std::ofstream out(temp_path);
        if (!out) {
            throw std::runtime_error("cannot create temporary file: " + temp_path);
        }
        out << rendered;
        if (!out.good()) {
            throw std::runtime_error("failed to render board configuration");
        }
    }

    try {
        (void)fancontrol::core::load_board_config(temp_path);
    } catch (...) {
        (void)::unlink(temp_path.c_str());
        throw;
    }

    if (::chmod(temp_path.c_str(), 0644) != 0) {
        (void)::unlink(temp_path.c_str());
        throw std::runtime_error("failed to set board configuration permissions");
    }
    if (::rename(temp_path.c_str(), config_path.c_str()) != 0) {
        (void)::unlink(temp_path.c_str());
        throw std::runtime_error("failed to write board configuration");
    }
}

} // namespace

namespace fancontrol::core {

int run(const std::vector<std::string> &args) {
    const bool debug = []() {
        const auto d = std::getenv("DEBUG");
        return d && *d && std::string(d) != "0";
    }();

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
    std::signal(SIGHUP, on_signal);
    std::signal(SIGQUIT, on_signal);

    try {
        if (args.size() > 1 && args[1] == "--validate-config") {
            const std::string config = pick_config_path(args, 2);
            const BoardConfig bcfg = load_board_config(config);
            (void)bcfg;
            std::cerr << "fancontrol: config validation passed for " << config << "\n";
            return 0;
        }
        if (args.size() > 1 && args[1] == "--dump-config-json") {
            const std::string config = pick_config_path(args, 2);
            const BoardConfig bcfg = load_board_config(config);
            std::cout << build_board_config_json(bcfg, config, true) << '\n';
            return 0;
        }
        if (args.size() > 1 && args[1] == "--dump-effective-config-json") {
            const std::string config = pick_config_path(args, 2);
            std::cout << build_effective_config_json(config) << '\n';
            return 0;
        }
        if (args.size() > 1 && args[1] == "--dump-default-config-json") {
            const std::string config = pick_config_path(args, 2);
            std::cout << build_board_config_json(default_board_config(), config, false) << '\n';
            return 0;
        }
        if (args.size() > 1 && args[1] == "--apply-config-json") {
            const std::string config = pick_config_path(args, 2);
            std::string payload;
            std::getline(std::cin, payload, '\0');
            write_validated_config_from_rpc_json(config, payload);
            return 0;
        }

        const std::string config = pick_config_path(args, 1);
        std::cerr << "Loading board configuration from " << config << " ...\n";

        g_stop = 0;
        g_restore_status = 0;

        const BoardConfig bcfg = load_board_config(config);
        return run_board_mode(bcfg, debug);
    } catch (const std::exception &e) {
        std::cerr << "fancontrol: " << e.what() << '\n';
        return 1;
    }
}

} // namespace fancontrol::core
