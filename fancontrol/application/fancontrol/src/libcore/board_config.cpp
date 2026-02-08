#include "libcore/board_config.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fancontrol::core {
namespace {

std::string trim(const std::string &s) {
    std::size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) {
        ++start;
    }
    std::size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
        --end;
    }
    return s.substr(start, end - start);
}

int to_int(const std::string &in, const std::string &name) {
    try {
        std::size_t idx = 0;
        long long v = std::stoll(in, &idx, 10);
        if (idx != in.size()) {
            throw std::runtime_error("");
        }
        if (v < static_cast<long long>(std::numeric_limits<int>::min()) ||
            v > static_cast<long long>(std::numeric_limits<int>::max())) {
            throw std::runtime_error("");
        }
        return static_cast<int>(v);
    } catch (...) {
        throw std::runtime_error("invalid integer for " + name + ": " + in);
    }
}

bool to_bool(const std::string &in, const std::string &name) {
    std::string lower = trim(in);
    for (char &c : lower) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    if (lower == "1" || lower == "true" || lower == "yes" || lower == "on") {
        return true;
    }
    if (lower == "0" || lower == "false" || lower == "no" || lower == "off") {
        return false;
    }

    throw std::runtime_error("invalid boolean for " + name + ": " + in);
}

void clamp_range(int &v, int minv, int maxv) {
    v = std::max(minv, std::min(v, maxv));
}

std::unordered_map<std::string, std::string> parse_csv_pairs(const std::string &v) {
    std::vector<std::string> tokens;
    std::string current;
    int brace_depth = 0;
    int bracket_depth = 0;
    bool in_quote = false;
    bool escape = false;
    char quote = '\0';

    for (char ch : v) {
        if (in_quote) {
            current.push_back(ch);
            if (escape) {
                escape = false;
                continue;
            }
            if (ch == '\\') {
                escape = true;
                continue;
            }
            if (ch == quote) {
                in_quote = false;
            }
            continue;
        }

        if (ch == '"' || ch == '\'') {
            in_quote = true;
            quote = ch;
            current.push_back(ch);
            continue;
        }
        if (ch == '{') {
            ++brace_depth;
        } else if (ch == '}' && brace_depth > 0) {
            --brace_depth;
        } else if (ch == '[') {
            ++bracket_depth;
        } else if (ch == ']' && bracket_depth > 0) {
            --bracket_depth;
        }

        if (ch == ',' && brace_depth == 0 && bracket_depth == 0) {
            tokens.push_back(trim(current));
            current.clear();
            continue;
        }

        current.push_back(ch);
    }
    tokens.push_back(trim(current));

    std::unordered_map<std::string, std::string> kv;
    for (const auto &token_raw : tokens) {
        const std::string token = trim(token_raw);
        if (!token.empty()) {
            const std::size_t eq = token.find('=');
            if (eq == std::string::npos || eq == 0 || eq + 1 >= token.size()) {
                throw std::runtime_error("bad source token: " + token);
            }
            const std::string k = trim(token.substr(0, eq));
            const std::string val = trim(token.substr(eq + 1));
            kv[k] = val;
        }
    }

    return kv;
}

BoardSourceConfig parse_source_line(const std::string &id, const std::string &rhs, int fallback_poll_sec) {
    BoardSourceConfig src;
    src.id = id;

    const auto kv = parse_csv_pairs(rhs);
    if (!kv.count("type")) {
        throw std::runtime_error("SOURCE_" + id + " missing required field: type");
    }

    src.type = kv.at("type");
    src.poll_sec = kv.count("poll") ? to_int(kv.at("poll"), "poll") : fallback_poll_sec;
    src.ttl_sec = kv.count("ttl") ? to_int(kv.at("ttl"), "ttl") : std::max(src.poll_sec * 2, fallback_poll_sec * 2);
    src.weight = kv.count("weight") ? to_int(kv.at("weight"), "weight") : 100;
    src.t_start_mC = kv.count("t_start") ? to_int(kv.at("t_start"), "t_start") : 60000;
    src.t_full_mC = kv.count("t_full") ? to_int(kv.at("t_full"), "t_full") : 80000;
    src.t_crit_mC = kv.count("t_crit") ? to_int(kv.at("t_crit"), "t_crit") : 90000;

    if (src.poll_sec < 1) {
        src.poll_sec = 1;
    }
    if (src.ttl_sec < src.poll_sec) {
        src.ttl_sec = src.poll_sec;
    }
    clamp_range(src.weight, 1, 200);

    if (!(src.t_start_mC < src.t_full_mC && src.t_full_mC <= src.t_crit_mC)) {
        throw std::runtime_error("invalid thermal thresholds for SOURCE_" + id);
    }

    if (src.type == "sysfs") {
        if (!kv.count("path")) {
            throw std::runtime_error("SOURCE_" + id + " missing required field: path");
        }
        src.path = kv.at("path");
    } else if (src.type == "ubus") {
        if (!kv.count("object") || !kv.count("method") || !kv.count("key")) {
            throw std::runtime_error("SOURCE_" + id + " missing required fields for ubus");
        }
        src.object = kv.at("object");
        src.method = kv.at("method");
        src.key = kv.at("key");
        src.args_json = kv.count("args") ? kv.at("args") : "{}";
    } else {
        throw std::runtime_error("unsupported source type for SOURCE_" + id + ": " + src.type);
    }

    return src;
}

} // namespace

BoardConfig load_board_config(const std::string &path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("cannot open board config: " + path);
    }

    std::map<std::string, std::string> plain;
    std::vector<std::pair<std::string, std::string>> sources;

    std::string line;
    while (std::getline(in, line)) {
        const auto comment = line.find('#');
        if (comment != std::string::npos) {
            line = line.substr(0, comment);
        }
        line = trim(line);
        if (line.empty()) {
            continue;
        }

        const std::size_t eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }

        const std::string key = trim(line.substr(0, eq));
        const std::string value = trim(line.substr(eq + 1));

        if (key.rfind("SOURCE_", 0) == 0 && key.size() > 7) {
            sources.emplace_back(key.substr(7), value);
        } else {
            plain[key] = value;
        }
    }

    BoardConfig cfg;
    if (plain.count("INTERVAL")) {
        cfg.interval_sec = to_int(plain["INTERVAL"], "INTERVAL");
    }
    if (plain.count("PWM_PATH")) {
        cfg.pwm_path = plain["PWM_PATH"];
    }
    if (plain.count("PWM_ENABLE_PATH")) {
        cfg.pwm_enable_path = plain["PWM_ENABLE_PATH"];
    }
    if (plain.count("THERMAL_MODE_PATH")) {
        cfg.thermal_mode_path = plain["THERMAL_MODE_PATH"];
    }
    if (plain.count("PWM_MIN")) {
        cfg.pwm_min = to_int(plain["PWM_MIN"], "PWM_MIN");
    }
    if (plain.count("PWM_MAX")) {
        cfg.pwm_max = to_int(plain["PWM_MAX"], "PWM_MAX");
    }
    if (plain.count("PWM_INVERTED")) {
        cfg.pwm_inverted = to_bool(plain["PWM_INVERTED"], "PWM_INVERTED");
    }
    if (plain.count("PWM_STARTUP_PWM")) {
        cfg.pwm_startup_pwm = to_int(plain["PWM_STARTUP_PWM"], "PWM_STARTUP_PWM");
    }
    if (plain.count("RAMP_UP")) {
        cfg.ramp_up = to_int(plain["RAMP_UP"], "RAMP_UP");
    }
    if (plain.count("RAMP_DOWN")) {
        cfg.ramp_down = to_int(plain["RAMP_DOWN"], "RAMP_DOWN");
    }
    if (plain.count("HYSTERESIS_MC")) {
        cfg.hysteresis_mC = to_int(plain["HYSTERESIS_MC"], "HYSTERESIS_MC");
    }
    if (plain.count("POLICY")) {
        cfg.policy = plain["POLICY"];
    }
    if (plain.count("FAILSAFE_PWM")) {
        cfg.failsafe_pwm = to_int(plain["FAILSAFE_PWM"], "FAILSAFE_PWM");
    }
    if (plain.count("PIDFILE")) {
        cfg.pidfile = plain["PIDFILE"];
    }

    if (cfg.interval_sec < 1) {
        cfg.interval_sec = 1;
    }
    clamp_range(cfg.pwm_min, 0, 255);
    clamp_range(cfg.pwm_max, 0, 255);
    clamp_range(cfg.failsafe_pwm, 0, 255);
    if (cfg.pwm_startup_pwm < -1) {
        cfg.pwm_startup_pwm = -1;
    }
    if (cfg.pwm_startup_pwm > 255) {
        cfg.pwm_startup_pwm = 255;
    }
    if (cfg.pwm_min > cfg.pwm_max) {
        cfg.pwm_min = cfg.pwm_max;
    }
    if (cfg.ramp_up < 1) {
        cfg.ramp_up = 1;
    }
    if (cfg.ramp_down < 1) {
        cfg.ramp_down = 1;
    }
    if (cfg.hysteresis_mC < 0) {
        cfg.hysteresis_mC = 0;
    }

    if (cfg.pwm_path.empty()) {
        throw std::runtime_error("missing mandatory setting: PWM_PATH");
    }

    if (cfg.pwm_enable_path.empty()) {
        cfg.pwm_enable_path = cfg.pwm_path + "_enable";
    }
    if (cfg.thermal_mode_path.empty()) {
        cfg.thermal_mode_path = "/sys/class/thermal/thermal_zone0/mode";
    }

    if (!cfg.policy.empty()) {
        std::string lower = cfg.policy;
        for (char &c : lower) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        cfg.policy = lower;
    }
    if (cfg.policy != "max") {
        throw std::runtime_error("unsupported POLICY: " + cfg.policy + " (only 'max' is supported)");
    }

    std::unordered_set<std::string> seen_source_ids;
    for (const auto &src_line : sources) {
        auto src = parse_source_line(src_line.first, src_line.second, cfg.interval_sec);
        if (!seen_source_ids.insert(src.id).second) {
            throw std::runtime_error("duplicate SOURCE id: " + src.id);
        }
        cfg.sources.push_back(std::move(src));
    }

    if (cfg.sources.empty()) {
        throw std::runtime_error("no SOURCE_* entries found in board config");
    }

    return cfg;
}

} // namespace fancontrol::core
