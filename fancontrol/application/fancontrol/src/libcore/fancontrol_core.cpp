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
#include "libcore/config_spec.hpp"
#include "libcore/demand_policy.hpp"
#include "libcore/fancontrol_core.hpp"
#include "libcore/json.hpp"
#include "libcore/pwm_controller.hpp"
#include "libcore/safety_guard.hpp"
#include "libcore/temp_source.hpp"

namespace {

volatile std::sig_atomic_t g_stop = 0;
volatile std::sig_atomic_t g_restore_status = 0;

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

bool try_write_text(const std::string &path, std::string_view value) {
    const int fd = ::open(path.c_str(), O_WRONLY | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }

    const ssize_t written = ::write(fd, value.data(), value.size());
    bool ok = (written >= 0 && static_cast<std::size_t>(written) == value.size());
    if (::close(fd) != 0) {
        ok = false;
    }
    return ok;
}

bool try_write_int(const std::string &path, int value) {
    const std::string text = std::to_string(value);
    return try_write_text(path, text);
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

std::string normalize_control_mode(std::string raw) {
    raw = to_lower_ascii(sanitize_field(std::move(raw)));
    if (raw == "kernel" || raw == "user") {
        return raw;
    }
    throw std::runtime_error("CONTROL_MODE must be one of: kernel, user");
}

int parse_bool01(std::string raw, int def, const char *name) {
    raw = to_lower_ascii(sanitize_field(std::move(raw)));
    if (raw.empty()) {
        return def;
    }
    if (raw == "1" || raw == "true") {
        return 1;
    }
    if (raw == "0" || raw == "false") {
        return 0;
    }
    throw std::runtime_error(std::string(name) + " must be boolean (0/1/true/false)");
}

std::optional<int> parse_optional_int(std::string raw, const char *name) {
    raw = trim_ascii(raw);
    if (raw.empty()) {
        return std::nullopt;
    }
    try {
        std::size_t idx = 0;
        const long long v = std::stoll(raw, &idx, 10);
        if (idx != raw.size()) {
            throw std::runtime_error("");
        }
        if (v < static_cast<long long>(std::numeric_limits<int>::min()) ||
            v > static_cast<long long>(std::numeric_limits<int>::max())) {
            throw std::runtime_error("");
        }
        return static_cast<int>(v);
    } catch (...) {
        throw std::runtime_error(std::string("invalid integer for ") + name + ": " + raw);
    }
}

std::optional<int> parse_optional_bool01(std::string raw, const char *name) {
    raw = to_lower_ascii(sanitize_field(std::move(raw)));
    if (raw.empty()) {
        return std::nullopt;
    }
    return parse_bool01(raw, 0, name);
}

fancontrol::core::BoardSourceConfig make_rpc_source_defaults(int interval_sec) {
    const fancontrol::core::ConfigSpec &spec = fancontrol::core::board_config_spec();
    fancontrol::core::BoardSourceConfig src;
    src.t_start_mC = spec.source_t_start_mC.default_value;
    src.t_full_mC = spec.source_t_full_mC.default_value;
    src.t_crit_mC = spec.source_t_crit_mC.default_value;
    src.poll_sec = std::max(interval_sec, spec.source_poll_sec.min_value);
    src.ttl_sec = std::max(src.poll_sec * 2, interval_sec * 2);
    src.weight = spec.source_weight.default_value;
    src.args_json = "{}";
    return src;
}

std::vector<fancontrol::core::BoardSourceConfig> parse_sources_from_rpc_payload(const nlohmann::json &payload,
                                                                                 int interval_sec) {
    const auto it = payload.find("sources");
    if (it == payload.end()) {
        return {};
    }
    if (!it->is_array()) {
        throw std::runtime_error("sources must be a JSON array");
    }

    std::vector<fancontrol::core::BoardSourceConfig> out;
    out.reserve(it->size());

    for (const auto &entry : *it) {
        if (!entry.is_object()) {
            throw std::runtime_error("sources[] items must be JSON objects");
        }

        if (const auto enabled = parse_optional_bool01(json_value_to_text(entry, "enabled"), "enabled")) {
            if (*enabled == 0) {
                continue;
            }
        }

        fancontrol::core::BoardSourceConfig src = make_rpc_source_defaults(interval_sec);
        src.id = sanitize_field(json_value_to_text(entry, "id"));
        src.type = to_lower_ascii(sanitize_field(json_value_to_text(entry, "type")));
        src.path = sanitize_field(json_value_to_text(entry, "path"));
        src.object = sanitize_field(json_value_to_text(entry, "object"));
        src.method = sanitize_field(json_value_to_text(entry, "method"));
        src.key = sanitize_field(json_value_to_text(entry, "key"));
        src.args_json = sanitize_field(json_value_to_text(entry, "args"));
        if (src.args_json.empty()) {
            src.args_json = "{}";
        }

        if (const auto v = parse_optional_int(json_value_to_text(entry, "t_start"), "t_start")) {
            src.t_start_mC = *v;
        }
        if (const auto v = parse_optional_int(json_value_to_text(entry, "t_full"), "t_full")) {
            src.t_full_mC = *v;
        }
        if (const auto v = parse_optional_int(json_value_to_text(entry, "t_crit"), "t_crit")) {
            src.t_crit_mC = *v;
        }
        if (const auto v = parse_optional_int(json_value_to_text(entry, "poll"), "poll")) {
            src.poll_sec = *v;
        }

        const bool has_ttl = !trim_ascii(json_value_to_text(entry, "ttl")).empty();
        if (const auto v = parse_optional_int(json_value_to_text(entry, "ttl"), "ttl")) {
            src.ttl_sec = *v;
        } else if (!has_ttl) {
            src.ttl_sec = std::max(src.poll_sec * 2, interval_sec * 2);
        }

        if (const auto v = parse_optional_int(json_value_to_text(entry, "weight"), "weight")) {
            src.weight = *v;
        }

        out.push_back(std::move(src));
    }

    return out;
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

struct BoardControlModeSnapshot {
    std::optional<std::string> orig_mode;
    bool has_mode = false;
};

std::string desired_control_mode_value(const fancontrol::core::BoardConfig &cfg);

bool setup_board_pwm(const fancontrol::core::BoardConfig &cfg, BoardPwmSnapshot &snap) {
    snap.has_enable = file_exists(cfg.pwm_enable_path);
    if (snap.has_enable) {
        snap.orig_enable = try_read_int(cfg.pwm_enable_path);
    }

    if (snap.has_enable && !try_write_int(cfg.pwm_enable_path, 1)) {
        return false;
    }

    return true;
}

void restore_board_pwm(const fancontrol::core::BoardConfig &cfg, const BoardPwmSnapshot &snap) {
    if (snap.has_enable) {
        const int enable_to_restore = snap.orig_enable.value_or(0);
        if (!try_write_int(cfg.pwm_enable_path, enable_to_restore)) {
            (void)try_write_int(cfg.pwm_enable_path, enable_to_restore);
            (void)try_write_int(cfg.pwm_path, fancontrol::core::max_cooling_pwm(cfg));
        }
    }
}

bool setup_board_control_mode(const fancontrol::core::BoardConfig &cfg, BoardControlModeSnapshot &snap) {
    snap.has_mode = file_exists(cfg.control_mode_path);
    if (!snap.has_mode) {
        return false;
    }
    snap.orig_mode = try_read_text(cfg.control_mode_path);

    const std::string runtime_mode = desired_control_mode_value(cfg);
    if (!try_write_text(cfg.control_mode_path, runtime_mode)) {
        return false;
    }

    return true;
}

void restore_board_control_mode(const fancontrol::core::BoardConfig &cfg, const BoardControlModeSnapshot &snap) {
    if (!snap.has_mode) {
        return;
    }

    const std::string restore_mode = (snap.orig_mode && !snap.orig_mode->empty()) ? *snap.orig_mode : "enabled";
    if (!try_write_text(cfg.control_mode_path, restore_mode)) {
        (void)try_write_text(cfg.control_mode_path, restore_mode);
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
    explicit BoardPwmGuard(const fancontrol::core::BoardConfig &cfg) : cfg_(cfg) {
        if (!setup_board_control_mode(cfg_, control_snapshot_)) {
            throw std::runtime_error("failed to set control mode via " + cfg_.control_mode_path);
        }
        control_armed_ = true;

        if (cfg_.control_mode != "user") {
            return;
        }
        if (!setup_board_pwm(cfg_, snapshot_)) {
            throw std::runtime_error("failed to enable PWM in board mode");
        }
        armed_ = true;
    }

    ~BoardPwmGuard() {
        if (armed_) {
            restore_board_pwm(cfg_, snapshot_);
        }
        if (control_armed_) {
            restore_board_control_mode(cfg_, control_snapshot_);
        }
    }

    BoardPwmGuard(const BoardPwmGuard &) = delete;
    BoardPwmGuard &operator=(const BoardPwmGuard &) = delete;

private:
    const fancontrol::core::BoardConfig &cfg_;
    BoardPwmSnapshot snapshot_;
    BoardControlModeSnapshot control_snapshot_;
    bool armed_ = false;
    bool control_armed_ = false;
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
    explicit BoardOwnershipGuard(const fancontrol::core::BoardConfig &cfg)
        : instance_lock_(fancontrol::core::kFixedPidfilePath),
          pidfile_guard_(fancontrol::core::kFixedPidfilePath),
          pwm_guard_(cfg),
          status_guard_(std::string(fancontrol::core::kRuntimeStatusPath)) {}

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

std::string desired_control_mode_value(const fancontrol::core::BoardConfig &cfg) {
    return (cfg.control_mode == "user") ? "disabled" : "enabled";
}

int run_board_mode(const fancontrol::core::BoardConfig &cfg) {
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
    if (::access(cfg.control_mode_path.c_str(), W_OK) != 0) {
        throw std::runtime_error("control mode path is not writable: " + cfg.control_mode_path);
    }

    BoardOwnershipGuard ownership_guard(cfg);

    fancontrol::core::SourceManager mgr;
    std::unordered_map<std::string, fancontrol::core::BoardSourceConfig> by_id;
    if (!create_board_sources(cfg, mgr, by_id)) {
        throw std::runtime_error("failed to create temperature sources");
    }

    std::unordered_map<std::string, bool> active_state;
    int current_pwm = try_read_int(cfg.pwm_path).value_or(fancontrol::core::min_cooling_pwm(cfg));
    fancontrol::core::RampAccumulator ramp_accumulator {};
    const std::string control_mode_target = desired_control_mode_value(cfg);
    std::optional<std::string> last_observed_mode;
    if (fancontrol_should_own_pwm) {
        const int startup_pwm = fancontrol::core::min_cooling_pwm(cfg);
        if (current_pwm != startup_pwm) {
            if (!try_write_int(cfg.pwm_path, startup_pwm)) {
                throw std::runtime_error("Error writing startup PWM value to " + cfg.pwm_path);
            }
            current_pwm = startup_pwm;
        }
    }
    mgr.start();
    std::cerr << "Starting board-mode fan control...\n";
    while (!g_stop) {
        std::string control_mode_value = try_read_text(cfg.control_mode_path).value_or("");
        if (control_mode_value != control_mode_target) {
            if (!try_write_text(cfg.control_mode_path, control_mode_target)) {
                throw std::runtime_error("failed to enforce control mode via " + cfg.control_mode_path);
            }
            control_mode_value = try_read_text(cfg.control_mode_path).value_or("");
        }
        if (!last_observed_mode || *last_observed_mode != control_mode_value) {
            last_observed_mode = control_mode_value;
        }

        std::vector<fancontrol::core::SourceTelemetry> telemetry;
        const fancontrol::core::TargetDecision decision = fancontrol::core::compute_target_decision(
            cfg, mgr, by_id, active_state, telemetry);
        const int target = decision.target_pwm;

        if (const auto observed_pwm = try_read_int(cfg.pwm_path)) {
            current_pwm = *observed_pwm;
        }

        int applied_pwm = current_pwm;
        if (fancontrol_should_own_pwm) {
            int next_pwm = fancontrol::core::apply_ramp(current_pwm, target, cfg, ramp_accumulator);

            if (next_pwm != current_pwm) {
                if (!try_write_int(cfg.pwm_path, next_pwm)) {
                    throw std::runtime_error("Error writing PWM value to " + cfg.pwm_path);
                }
                current_pwm = next_pwm;
            }
            applied_pwm = current_pwm;
        }

        const std::string status_payload =
            fancontrol::core::build_runtime_status_json(cfg, decision, current_pwm, target, applied_pwm, telemetry);
        (void)ownership_guard.write_runtime_status(status_payload);

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
    return std::string(fancontrol::core::kDefaultConfigPath);
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
        {"control_mode_path", cfg.control_mode_path},
        {"pwm_min", cfg.pwm_min},
        {"pwm_max", cfg.pwm_max},
        {"ramp_up", cfg.ramp_up},
        {"ramp_down", cfg.ramp_down},
        {"hysteresis_mC", cfg.hysteresis_mC},
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
    return build_board_config_json(fancontrol::core::default_board_config(), path, false);
}

std::string render_board_config_from_rpc_json(const nlohmann::json &payload) {
    fancontrol::core::BoardConfig cfg = fancontrol::core::default_board_config();

    if (const auto v = parse_optional_int(json_value_to_text(payload, "interval"), "INTERVAL")) {
        cfg.interval_sec = *v;
    }

    const std::string control_mode_raw = json_value_to_text(payload, "control_mode");
    if (!trim_ascii(control_mode_raw).empty()) {
        cfg.control_mode = normalize_control_mode(control_mode_raw);
    }

    const std::string pwm_path = sanitize_field(json_value_to_text(payload, "pwm_path"));
    if (!pwm_path.empty()) {
        cfg.pwm_path = pwm_path;
    }

    const std::string pwm_enable_path = sanitize_field(json_value_to_text(payload, "pwm_enable_path"));
    if (!pwm_enable_path.empty()) {
        cfg.pwm_enable_path = pwm_enable_path;
    }

    const std::string control_mode_path = sanitize_field(json_value_to_text(payload, "control_mode_path"));
    if (!control_mode_path.empty()) {
        cfg.control_mode_path = control_mode_path;
    }

    if (const auto v = parse_optional_int(json_value_to_text(payload, "pwm_min"), "PWM_MIN")) {
        cfg.pwm_min = *v;
    }
    if (const auto v = parse_optional_int(json_value_to_text(payload, "pwm_max"), "PWM_MAX")) {
        cfg.pwm_max = *v;
    }
    if (const auto v = parse_optional_int(json_value_to_text(payload, "ramp_up"), "RAMP_UP")) {
        cfg.ramp_up = *v;
    }
    if (const auto v = parse_optional_int(json_value_to_text(payload, "ramp_down"), "RAMP_DOWN")) {
        cfg.ramp_down = *v;
    }
    if (const auto v = parse_optional_int(json_value_to_text(payload, "hysteresis_mC"), "HYSTERESIS_MC")) {
        cfg.hysteresis_mC = *v;
    }

    if (const auto v = parse_optional_int(json_value_to_text(payload, "failsafe_pwm"), "FAILSAFE_PWM")) {
        cfg.failsafe_pwm = *v;
    }

    const bool has_sources_field = payload.find("sources") != payload.end();
    if (has_sources_field) {
        cfg.sources = parse_sources_from_rpc_payload(payload, cfg.interval_sec);
    }

    fancontrol::core::validate_board_config(cfg);
    return fancontrol::core::render_board_config_text(cfg);
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
            std::cout << build_board_config_json(fancontrol::core::default_board_config(), config, false) << '\n';
            return 0;
        }
        if (args.size() > 1 && args[1] == "--dump-default-config-text") {
            BoardConfig cfg = fancontrol::core::default_board_config();
            fancontrol::core::validate_board_config(cfg);
            std::cout << fancontrol::core::render_board_config_text(cfg);
            return 0;
        }
        if (args.size() > 1 && args[1] == "--dump-schema-json") {
            std::cout << fancontrol::core::dump_board_schema_json() << '\n';
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
        return run_board_mode(bcfg);
    } catch (const std::exception &e) {
        std::cerr << "fancontrol: " << e.what() << '\n';
        return 1;
    }
}

} // namespace fancontrol::core
