#include <algorithm>
#include <cerrno>
#include <cctype>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>

#include <sys/stat.h>
#include <unistd.h>

#include "libcore/board_config.hpp"
#include "libcore/fancontrol_core.hpp"
#include "libcore/temp_source.hpp"

namespace {

volatile std::sig_atomic_t g_stop = 0;
volatile std::sig_atomic_t g_restore_status = 0;

constexpr int kPwmMax = 255;

int min_cooling_pwm(const fancontrol::core::BoardConfig &cfg) {
    return cfg.pwm_inverted ? cfg.pwm_max : cfg.pwm_min;
}

int max_cooling_pwm(const fancontrol::core::BoardConfig &cfg) {
    return cfg.pwm_inverted ? cfg.pwm_min : cfg.pwm_max;
}

bool is_stronger_cooling_pwm(int candidate, int baseline, const fancontrol::core::BoardConfig &cfg) {
    if (cfg.pwm_inverted) {
        return candidate < baseline;
    }
    return candidate > baseline;
}

int stronger_cooling_pwm(int lhs, int rhs, const fancontrol::core::BoardConfig &cfg) {
    return is_stronger_cooling_pwm(rhs, lhs, cfg) ? rhs : lhs;
}

int clamp_pwm(const fancontrol::core::BoardConfig &cfg, int pwm) {
    return std::clamp(pwm, cfg.pwm_min, cfg.pwm_max);
}

int apply_startup_boost(const fancontrol::core::BoardConfig &cfg, int target_pwm, int current_pwm) {
    if (cfg.pwm_startup_pwm < 0) {
        return target_pwm;
    }

    const int startup_pwm = clamp_pwm(cfg, cfg.pwm_startup_pwm);
    const int idle_pwm = min_cooling_pwm(cfg);

    const bool requesting_active_cooling = is_stronger_cooling_pwm(target_pwm, idle_pwm, cfg);
    const bool startup_stronger_than_target = is_stronger_cooling_pwm(startup_pwm, target_pwm, cfg);
    const bool current_weaker_than_startup = is_stronger_cooling_pwm(startup_pwm, current_pwm, cfg);

    if (requesting_active_cooling && startup_stronger_than_target && current_weaker_than_startup) {
        return startup_pwm;
    }
    return target_pwm;
}

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

bool try_write_text(const std::string &path, const std::string &value) {
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

bool pid_is_alive(pid_t pid) {
    if (pid <= 0) {
        return false;
    }
    if (::kill(pid, 0) == 0) {
        return true;
    }
    return errno == EPERM;
}

void ensure_pidfile_available(const std::string &pidfile) {
    if (!file_exists(pidfile)) {
        return;
    }

    const auto existing_pid = try_read_pid(pidfile);
    if (existing_pid && pid_is_alive(*existing_pid)) {
        throw std::runtime_error("File " + pidfile + " exists and process " +
                                 std::to_string(*existing_pid) + " is running, is fancontrol already running?");
    }

    if (::unlink(pidfile.c_str()) != 0 && errno != ENOENT) {
        std::string msg = "stale pidfile " + pidfile + " cannot be removed: " + std::strerror(errno);
        if (existing_pid) {
            msg = "stale pidfile " + pidfile + " (pid " + std::to_string(*existing_pid) +
                  ") cannot be removed: " + std::strerror(errno);
        }
        throw std::runtime_error(msg);
    }
}

void remove_pidfile(const std::string &pidfile) {
    (void)::unlink(pidfile.c_str());
}

struct BoardPwmSnapshot {
    std::optional<int> orig_pwm;
    bool has_enable = false;
};

struct BoardThermalSnapshot {
    std::optional<std::string> orig_mode;
};

bool setup_board_pwm(const fancontrol::core::BoardConfig &cfg, BoardPwmSnapshot &snap, bool debug) {
    snap.orig_pwm = try_read_int(cfg.pwm_path);
    snap.has_enable = file_exists(cfg.pwm_enable_path);

    if (debug) {
        if (snap.orig_pwm) {
            std::cerr << "Saving " << cfg.pwm_path << " original value as " << *snap.orig_pwm << "\n";
        }
    }

    if (snap.has_enable && !try_write_int(cfg.pwm_enable_path, 1)) {
        return false;
    }

    return try_write_int(cfg.pwm_path, max_cooling_pwm(cfg));
}

void restore_board_pwm(const fancontrol::core::BoardConfig &cfg, const BoardPwmSnapshot &snap, bool debug) {
    if (snap.orig_pwm) {
        if (debug) {
            std::cerr << "Restoring " << cfg.pwm_path << " original value of " << *snap.orig_pwm << "\n";
        }
        (void)try_write_int(cfg.pwm_path, *snap.orig_pwm);
    } else {
        (void)try_write_int(cfg.pwm_path, max_cooling_pwm(cfg));
    }

    if (snap.has_enable) {
        if (debug) {
            std::cerr << "Setting " << cfg.pwm_enable_path << " to manual mode (1) for kernel handover\n";
        }
        if (!try_write_int(cfg.pwm_enable_path, 1)) {
            (void)try_write_int(cfg.pwm_enable_path, 1);
            (void)try_write_int(cfg.pwm_path, max_cooling_pwm(cfg));
        }
    }
}

bool setup_board_thermal(const fancontrol::core::BoardConfig &cfg, BoardThermalSnapshot &snap, bool debug) {
    snap.orig_mode = try_read_text(cfg.thermal_mode_path);
    if (debug && snap.orig_mode) {
        std::cerr << "Saving " << cfg.thermal_mode_path << " original value as " << *snap.orig_mode << "\n";
    }
    if (!try_write_text(cfg.thermal_mode_path, "disabled")) {
        return false;
    }
    if (debug) {
        std::cerr << "Set " << cfg.thermal_mode_path << " to disabled (fancontrol owns PWM)\n";
    }
    return true;
}

void restore_board_thermal(const fancontrol::core::BoardConfig &cfg, const BoardThermalSnapshot &snap, bool debug) {
    std::string target_mode = "enabled";
    if (snap.orig_mode && !snap.orig_mode->empty()) {
        target_mode = *snap.orig_mode;
    }

    if (debug) {
        std::cerr << "Restoring " << cfg.thermal_mode_path << " to " << target_mode << "\n";
    }
    if (!try_write_text(cfg.thermal_mode_path, target_mode)) {
        std::cerr << "Warning: failed to restore " << cfg.thermal_mode_path << " to " << target_mode << "\n";
    }
}

int apply_ramp(int current_pwm, int target_pwm, const fancontrol::core::BoardConfig &cfg) {
    if (target_pwm == current_pwm) {
        return current_pwm;
    }

    if (is_stronger_cooling_pwm(target_pwm, current_pwm, cfg)) {
        if (cfg.pwm_inverted) {
            return std::max(target_pwm, current_pwm - cfg.ramp_up);
        }
        return std::min(target_pwm, current_pwm + cfg.ramp_up);
    }

    if (cfg.pwm_inverted) {
        return std::min(target_pwm, current_pwm + cfg.ramp_down);
    }
    return std::max(target_pwm, current_pwm - cfg.ramp_down);
}

int demand_from_source(const fancontrol::core::BoardConfig &cfg,
                       const fancontrol::core::BoardSourceConfig &src,
                       int temp_mC,
                       bool &active,
                       bool &critical) {
    const int idle_pwm = min_cooling_pwm(cfg);
    const int full_pwm = max_cooling_pwm(cfg);

    if (temp_mC >= src.t_crit_mC) {
        critical = true;
        active = true;
        return full_pwm;
    }

    const int on_threshold = src.t_start_mC + cfg.hysteresis_mC;
    const int off_threshold = src.t_start_mC - cfg.hysteresis_mC;

    if (!active) {
        if (temp_mC < on_threshold) {
            return idle_pwm;
        }
        active = true;
    } else {
        if (temp_mC <= off_threshold) {
            active = false;
            return idle_pwm;
        }
    }

    double ratio = 0.0;
    if (temp_mC <= src.t_start_mC) {
        ratio = 0.0;
    } else if (temp_mC >= src.t_full_mC) {
        ratio = 1.0;
    } else {
        ratio = static_cast<double>(temp_mC - src.t_start_mC) /
                static_cast<double>(src.t_full_mC - src.t_start_mC);
    }

    ratio *= static_cast<double>(src.weight) / 100.0;
    ratio = std::clamp(ratio, 0.0, 1.0);

    const int span = cfg.pwm_max - cfg.pwm_min;
    int demand = idle_pwm;
    if (cfg.pwm_inverted) {
        demand = cfg.pwm_max - static_cast<int>(std::lround(ratio * static_cast<double>(span)));
    } else {
        demand = cfg.pwm_min + static_cast<int>(std::lround(ratio * static_cast<double>(span)));
    }
    demand = clamp_pwm(cfg, demand);
    return demand;
}

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

int compute_board_target(const fancontrol::core::BoardConfig &cfg,
                         const fancontrol::core::SourceManager &mgr,
                         std::unordered_map<std::string, fancontrol::core::BoardSourceConfig> &by_id,
                         std::unordered_map<std::string, bool> &active_state,
                         bool debug) {
    const auto now = std::chrono::steady_clock::now();

    int target = min_cooling_pwm(cfg);
    bool any_valid = false;
    bool any_timeout = false;
    bool critical = false;

    for (const auto &rt : mgr.runtimes()) {
        if (!rt.source) {
            continue;
        }

        const std::string sid = rt.source->id();
        if (!by_id.count(sid)) {
            continue;
        }
        const auto &src = by_id[sid];

        if (!rt.last_sample || !rt.last_sample->ok) {
            if (rt.has_polled) {
                any_timeout = true;
            }
            continue;
        }

        const auto age = std::chrono::duration_cast<std::chrono::seconds>(now - rt.last_sample->sample_ts).count();
        if (age > src.ttl_sec) {
            any_timeout = true;
            if (debug) {
                std::cerr << "source[" << sid << "] stale age=" << age << "s ttl=" << src.ttl_sec << "s\n";
            }
            continue;
        }

        any_valid = true;
        bool &active = active_state[sid];
        const int source_demand = demand_from_source(cfg, src, rt.last_sample->temp_mC, active, critical);
        target = stronger_cooling_pwm(target, source_demand, cfg);

        if (debug) {
            std::cerr << "source[" << sid << "] temp=" << rt.last_sample->temp_mC
                      << " demand=" << source_demand << " active=" << (active ? 1 : 0) << "\n";
        }
    }

    if (critical) {
        target = max_cooling_pwm(cfg);
    }
    if (!any_valid) {
        target = max_cooling_pwm(cfg);
    }
    if (any_timeout) {
        target = stronger_cooling_pwm(target, clamp_pwm(cfg, cfg.failsafe_pwm), cfg);
    }

    return clamp_pwm(cfg, target);
}

int run_board_mode(const fancontrol::core::BoardConfig &cfg, bool debug) {
    if (::access(cfg.pwm_path.c_str(), W_OK) != 0) {
        throw std::runtime_error("PWM path is not writable: " + cfg.pwm_path);
    }
    if (file_exists(cfg.pwm_enable_path) && ::access(cfg.pwm_enable_path.c_str(), W_OK) != 0) {
        throw std::runtime_error("PWM enable path is not writable: " + cfg.pwm_enable_path);
    }
    if (::access(cfg.thermal_mode_path.c_str(), W_OK) != 0) {
        throw std::runtime_error("thermal mode path is not writable: " + cfg.thermal_mode_path);
    }

    ensure_pidfile_available(cfg.pidfile);

    {
        std::ofstream pid(cfg.pidfile);
        if (!pid) {
            throw std::runtime_error("cannot create pidfile: " + cfg.pidfile);
        }
        pid << ::getpid() << '\n';
    }

    BoardThermalSnapshot thermal_snapshot;
    if (!setup_board_thermal(cfg, thermal_snapshot, debug)) {
        remove_pidfile(cfg.pidfile);
        throw std::runtime_error("failed to disable kernel thermal control");
    }

    BoardPwmSnapshot pwm_snapshot;
    if (!setup_board_pwm(cfg, pwm_snapshot, debug)) {
        restore_board_thermal(cfg, thermal_snapshot, debug);
        remove_pidfile(cfg.pidfile);
        throw std::runtime_error("failed to enable PWM in board mode");
    }

    fancontrol::core::SourceManager mgr;
    std::unordered_map<std::string, fancontrol::core::BoardSourceConfig> by_id;
    if (!create_board_sources(cfg, mgr, by_id)) {
        restore_board_pwm(cfg, pwm_snapshot, debug);
        restore_board_thermal(cfg, thermal_snapshot, debug);
        remove_pidfile(cfg.pidfile);
        throw std::runtime_error("failed to create temperature sources");
    }

    std::unordered_map<std::string, bool> active_state;
    int current_pwm = try_read_int(cfg.pwm_path).value_or(max_cooling_pwm(cfg));

    try {
        std::cerr << "Starting board-mode fan control...\n";
        while (!g_stop) {
            mgr.poll(debug);

            const int target = compute_board_target(cfg, mgr, by_id, active_state, debug);
            int next_pwm = apply_ramp(current_pwm, target, cfg);
            next_pwm = apply_startup_boost(cfg, next_pwm, current_pwm);

            if (next_pwm != current_pwm) {
                if (!try_write_int(cfg.pwm_path, next_pwm)) {
                    throw std::runtime_error("Error writing PWM value to " + cfg.pwm_path);
                }
                current_pwm = next_pwm;
                if (debug) {
                    std::cerr << "board pwm target=" << target << " applied=" << current_pwm << "\n";
                }
            }

            for (int i = 0; i < cfg.interval_sec && !g_stop; ++i) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }
    } catch (...) {
        restore_board_pwm(cfg, pwm_snapshot, debug);
        restore_board_thermal(cfg, thermal_snapshot, debug);
        remove_pidfile(cfg.pidfile);
        throw;
    }

    restore_board_pwm(cfg, pwm_snapshot, debug);
    restore_board_thermal(cfg, thermal_snapshot, debug);
    remove_pidfile(cfg.pidfile);
    return (g_restore_status == 0) ? 0 : 1;
}

std::string pick_config_path(int argc, char **argv) {
    if (argc > 1) {
        return argv[1];
    }
    return "/etc/fancontrol.r3mini";
}

} // namespace

namespace fancontrol::core {

int run(int argc, char **argv) {
    const bool debug = []() {
        const char *d = std::getenv("DEBUG");
        return d && *d && std::string(d) != "0";
    }();

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
    std::signal(SIGHUP, on_signal);
    std::signal(SIGQUIT, on_signal);

    try {
        const std::string config = pick_config_path(argc, argv);
        std::cerr << "Loading board configuration from " << config << " ...\n";

        const BoardConfig bcfg = load_board_config(config);
        return run_board_mode(bcfg, debug);
    } catch (const std::exception &e) {
        std::cerr << "fancontrol: " << e.what() << '\n';
        return 1;
    }
}

} // namespace fancontrol::core
