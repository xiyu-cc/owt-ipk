#include <cerrno>
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
    std::optional<int> orig_pwm;
    std::optional<int> orig_enable;
    bool has_enable = false;
};

bool setup_board_pwm(const fancontrol::core::BoardConfig &cfg, BoardPwmSnapshot &snap, bool debug) {
    snap.orig_pwm = try_read_int(cfg.pwm_path);
    snap.has_enable = file_exists(cfg.pwm_enable_path);
    if (snap.has_enable) {
        snap.orig_enable = try_read_int(cfg.pwm_enable_path);
    }

    if (debug && snap.orig_pwm) {
        std::cerr << "Saving " << cfg.pwm_path << " original value as " << *snap.orig_pwm << "\n";
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

int run_board_mode(const fancontrol::core::BoardConfig &cfg, bool debug) {
    if (::access(cfg.pwm_path.c_str(), W_OK) != 0) {
        throw std::runtime_error("PWM path is not writable: " + cfg.pwm_path);
    }
    if (file_exists(cfg.pwm_enable_path) && ::access(cfg.pwm_enable_path.c_str(), W_OK) != 0) {
        throw std::runtime_error("PWM enable path is not writable: " + cfg.pwm_enable_path);
    }
    if (::access(cfg.thermal_mode_path.c_str(), R_OK) != 0) {
        throw std::runtime_error("thermal mode path is not readable: " + cfg.thermal_mode_path);
    }

    BoardOwnershipGuard ownership_guard(cfg, debug);

    fancontrol::core::SourceManager mgr;
    std::unordered_map<std::string, fancontrol::core::BoardSourceConfig> by_id;
    if (!create_board_sources(cfg, mgr, by_id)) {
        throw std::runtime_error("failed to create temperature sources");
    }

    std::unordered_map<std::string, bool> active_state;
    int current_pwm = try_read_int(cfg.pwm_path).value_or(fancontrol::core::min_cooling_pwm(cfg));
    std::optional<bool> last_fancontrol_owner;
    mgr.start(debug);
    std::cerr << "Starting board-mode fan control...\n";
    while (!g_stop) {
        const std::string thermal_mode = try_read_text(cfg.thermal_mode_path).value_or("");
        const bool fancontrol_owns_pwm = (thermal_mode == "disabled");
        if (!last_fancontrol_owner || *last_fancontrol_owner != fancontrol_owns_pwm) {
            if (debug) {
                std::cerr << "control owner changed: "
                          << (fancontrol_owns_pwm ? "fancontrol" : "kernel")
                          << " (thermal mode=" << (thermal_mode.empty() ? "<unknown>" : thermal_mode) << ")\n";
            }
            last_fancontrol_owner = fancontrol_owns_pwm;
        }

        std::vector<fancontrol::core::SourceTelemetry> telemetry;
        const fancontrol::core::TargetDecision decision = fancontrol::core::compute_target_decision(
            cfg, mgr, by_id, active_state, telemetry, debug);
        const int target = decision.target_pwm;

        if (const auto observed_pwm = try_read_int(cfg.pwm_path)) {
            current_pwm = *observed_pwm;
        }

        int applied_pwm = current_pwm;
        if (fancontrol_owns_pwm) {
            int next_pwm = fancontrol::core::apply_ramp(current_pwm, target, cfg);
            next_pwm = fancontrol::core::apply_startup_boost(cfg, next_pwm, current_pwm);

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
    return "/etc/fancontrol.r3mini";
}

std::string build_board_config_json(const fancontrol::core::BoardConfig &cfg, const std::string &path) {
    nlohmann::json root = {
        {"ok", 1},
        {"path", path},
        {"exists", 1},
        {"interval", cfg.interval_sec},
        {"pwm_path", cfg.pwm_path},
        {"pwm_enable_path", cfg.pwm_enable_path},
        {"thermal_mode_path", cfg.thermal_mode_path},
        {"pwm_min", cfg.pwm_min},
        {"pwm_max", cfg.pwm_max},
        {"pwm_inverted", cfg.pwm_inverted ? 1 : 0},
        {"pwm_startup_pwm", cfg.pwm_startup_pwm},
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
            std::cout << build_board_config_json(bcfg, config) << '\n';
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
