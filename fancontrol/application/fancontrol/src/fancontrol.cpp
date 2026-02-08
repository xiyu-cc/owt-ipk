#include <algorithm>
#include <chrono>
#include <csignal>
#include <cctype>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

namespace {

volatile std::sig_atomic_t g_stop = 0;
volatile std::sig_atomic_t g_restore_status = 0;

constexpr int kPwmMax = 255;

enum class PathMode {
    Absolute,
    Hwmon,
    I2c,
};

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

bool try_write_int(const std::string &path, int value) {
    std::ofstream out(path);
    if (!out) {
        return false;
    }
    out << value << '\n';
    return out.good();
}

std::vector<std::pair<std::string, std::string>> parse_pair_list(const std::string &value, const std::string &key_name) {
    std::vector<std::pair<std::string, std::string>> out;
    std::istringstream iss(value);
    std::string token;
    while (iss >> token) {
        const auto pos = token.find('=');
        if (pos == std::string::npos || pos == 0 || pos + 1 >= token.size()) {
            throw std::runtime_error("bad token in " + key_name + ": " + token);
        }
        out.emplace_back(token.substr(0, pos), token.substr(pos + 1));
    }
    if (out.empty()) {
        throw std::runtime_error("empty value for " + key_name);
    }
    return out;
}

std::unordered_map<std::string, std::string> parse_pairs(const std::string &value, const std::string &key_name) {
    std::unordered_map<std::string, std::string> out;
    for (const auto &kv : parse_pair_list(value, key_name)) {
        out[kv.first] = kv.second;
    }
    return out;
}

std::unordered_map<std::string, int> parse_int_pairs(const std::string &value, const std::string &key_name) {
    std::unordered_map<std::string, int> out;
    for (const auto &kv : parse_pair_list(value, key_name)) {
        try {
            out[kv.first] = std::stoi(kv.second);
        } catch (...) {
            throw std::runtime_error("invalid numeric value in " + key_name + ": " + kv.second);
        }
    }
    return out;
}

bool is_hwmon_path(const std::string &p) {
    if (p.rfind("hwmon", 0) != 0) {
        return false;
    }
    std::size_t i = 5;
    if (i >= p.size() || !std::isdigit(static_cast<unsigned char>(p[i]))) {
        return false;
    }
    while (i < p.size() && std::isdigit(static_cast<unsigned char>(p[i]))) {
        ++i;
    }
    return i < p.size() && p[i] == '/';
}

bool is_i2c_path(const std::string &p) {
    const auto dash = p.find('-');
    const auto slash = p.find('/');
    if (dash == std::string::npos || slash == std::string::npos || dash == 0 || dash > slash) {
        return false;
    }
    for (std::size_t i = 0; i < dash; ++i) {
        if (!std::isdigit(static_cast<unsigned char>(p[i]))) {
            return false;
        }
    }
    if (slash - dash != 5) {
        return false;
    }
    for (std::size_t i = dash + 1; i < slash; ++i) {
        if (!std::isxdigit(static_cast<unsigned char>(p[i]))) {
            return false;
        }
    }
    return true;
}

PathMode detect_mode(const std::string &first_pwm) {
    if (first_pwm.empty()) {
        throw std::runtime_error("empty PWM path in configuration");
    }
    if (first_pwm[0] == '/') {
        return PathMode::Absolute;
    }
    if (is_hwmon_path(first_pwm)) {
        return PathMode::Hwmon;
    }
    if (is_i2c_path(first_pwm)) {
        return PathMode::I2c;
    }
    throw std::runtime_error("invalid path to sensors: " + first_pwm);
}

std::string base_dir(PathMode mode) {
    switch (mode) {
    case PathMode::Absolute:
        return "/";
    case PathMode::Hwmon:
        return "/sys/class/hwmon";
    case PathMode::I2c:
        return "/sys/bus/i2c/devices";
    }
    return "/";
}

std::string sanitize_device_name(std::string in) {
    for (char &c : in) {
        if (std::isspace(static_cast<unsigned char>(c)) || c == '=') {
            c = '_';
        }
    }
    return in;
}

std::string join_path(const std::string &base, const std::string &p) {
    if (p.empty()) {
        return base;
    }
    if (base == "/") {
        if (p[0] == '/') {
            return p;
        }
        return "/" + p;
    }
    if (p[0] == '/') {
        return p;
    }
    return base + "/" + p;
}

std::string device_path(const std::string &base, const std::string &device_rel) {
    const std::string device = join_path(base, device_rel);
    const std::string link = device + "/device";
    struct stat st {};
    if (::lstat(link.c_str(), &st) != 0 || !S_ISLNK(st.st_mode)) {
        return {};
    }

    char realbuf[PATH_MAX] = {};
    if (::realpath(link.c_str(), realbuf) == nullptr) {
        return {};
    }

    std::string real(realbuf);
    constexpr const char prefix[] = "/sys/";
    if (real.rfind(prefix, 0) == 0) {
        real.erase(0, sizeof(prefix) - 1);
    } else if (real == "/sys") {
        real.clear();
    }
    return real;
}

std::string read_first_line(const std::string &path) {
    std::ifstream in(path);
    if (!in) {
        return {};
    }
    std::string line;
    std::getline(in, line);
    return trim(line);
}

std::string device_name(const std::string &base, const std::string &device_rel) {
    const std::string device = join_path(base, device_rel);
    std::string name = read_first_line(device + "/name");
    if (name.empty()) {
        name = read_first_line(device + "/device/name");
    }
    if (name.empty()) {
        return {};
    }
    return sanitize_device_name(name);
}

void replace_all_inplace(std::string &s, const std::string &from, const std::string &to) {
    if (from.empty()) {
        return;
    }
    std::size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
}

std::vector<std::string> split_plus(const std::string &in) {
    std::vector<std::string> parts;
    std::string current;
    for (char c : in) {
        if (c == '+') {
            if (!current.empty()) {
                parts.push_back(current);
                current.clear();
            }
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) {
        parts.push_back(current);
    }
    return parts;
}

struct Channel {
    std::string pwm_key;
    std::string pwm_rel;
    std::string temp_rel;
    std::vector<std::string> fan_rel;

    std::string pwm_path;
    std::string temp_path;
    std::vector<std::string> fan_paths;

    int min_temp_c = 0;
    int max_temp_c = 0;
    int min_start_pwm = 0;
    int min_stop_pwm = 0;
    int min_pwm = 0;
    int max_pwm = kPwmMax;
    int average = 1;

    std::deque<int> temp_hist;
};

struct State {
    std::string config_path;
    PathMode mode = PathMode::Absolute;
    std::string base;
    int interval = 2;
    bool debug = false;
    std::string pidfile = "/var/run/fancontrol.pid";
    std::vector<Channel> channels;

    std::unordered_map<std::string, int> orig_pwm;
    std::unordered_map<std::string, int> orig_enable;
};

bool validate_devices(const std::string &base,
                      const std::vector<std::pair<std::string, std::string>> &devpath,
                      const std::vector<std::pair<std::string, std::string>> &devname) {
    bool outdated = false;

    for (const auto &entry : devpath) {
        const std::string actual = device_path(base, entry.first);
        if (actual != entry.second) {
            std::cerr << "Device path of " << entry.first << " has changed\n";
            outdated = true;
        }
    }

    for (const auto &entry : devname) {
        const std::string actual = device_name(base, entry.first);
        if (actual != entry.second) {
            std::cerr << "Device name of " << entry.first << " has changed\n";
            outdated = true;
        }
    }

    return !outdated;
}

void fixup_device_files(std::vector<Channel> &channels, const std::string &device_rel) {
    const std::string from = device_rel + "/device";
    const std::string to = device_rel;

    for (auto &ch : channels) {
        const std::string old_pwm = ch.pwm_rel;
        const std::string old_temp = ch.temp_rel;

        replace_all_inplace(ch.pwm_rel, from, to);
        replace_all_inplace(ch.temp_rel, from, to);

        if (ch.pwm_rel != old_pwm) {
            std::cerr << "Adjusting " << old_pwm << " -> " << ch.pwm_rel << "\n";
        }
        if (ch.temp_rel != old_temp) {
            std::cerr << "Adjusting " << old_temp << " -> " << ch.temp_rel << "\n";
        }

        for (auto &fan : ch.fan_rel) {
            const std::string old_fan = fan;
            replace_all_inplace(fan, from, to);
            if (fan != old_fan) {
                std::cerr << "Adjusting " << old_fan << " -> " << fan << "\n";
            }
        }
    }
}

bool check_files(const std::vector<Channel> &channels) {
    bool outdated = false;

    for (const auto &ch : channels) {
        if (::access(ch.pwm_path.c_str(), W_OK) != 0) {
            std::cerr << "Error: file " << ch.pwm_path << " doesn't exist or isn't writable\n";
            outdated = true;
        }
    }

    for (const auto &ch : channels) {
        if (::access(ch.temp_path.c_str(), R_OK) != 0) {
            std::cerr << "Error: file " << ch.temp_path << " doesn't exist or isn't readable\n";
            outdated = true;
        }
    }

    for (const auto &ch : channels) {
        for (const auto &fan : ch.fan_paths) {
            if (::access(fan.c_str(), R_OK) != 0) {
                std::cerr << "Error: file " << fan << " doesn't exist or isn't readable\n";
                outdated = true;
            }
        }
    }

    if (outdated) {
        std::cerr << "\n"
                  << "At least one referenced file is missing or doesn't have\n"
                  << "correct privileges. Either some required kernel\n"
                  << "modules haven't been loaded, or your configuration file is outdated.\n"
                  << "In the latter case, you should run pwmconfig again.\n";
    }

    return !outdated;
}

State load_config(const std::string &config_file, bool debug) {
    std::ifstream in(config_file);
    if (!in) {
        throw std::runtime_error("cannot open config file: " + config_file);
    }

    std::map<std::string, std::string> cfg;
    std::string line;
    while (std::getline(in, line)) {
        const auto comment_pos = line.find('#');
        if (comment_pos != std::string::npos) {
            line = line.substr(0, comment_pos);
        }
        line = trim(line);
        if (line.empty()) {
            continue;
        }
        const auto pos = line.find('=');
        if (pos == std::string::npos) {
            continue;
        }
        const std::string key = trim(line.substr(0, pos));
        const std::string val = trim(line.substr(pos + 1));
        if (!key.empty()) {
            cfg[key] = val;
        }
    }

    const std::vector<std::string> required = {
        "INTERVAL", "FCTEMPS", "MINTEMP", "MAXTEMP", "MINSTART", "MINSTOP"
    };
    for (const auto &k : required) {
        if (!cfg.count(k) || cfg[k].empty()) {
            throw std::runtime_error("missing mandatory setting: " + k);
        }
    }

    State st;
    st.config_path = config_file;
    st.debug = debug;

    try {
        st.interval = std::stoi(cfg["INTERVAL"]);
    } catch (...) {
        throw std::runtime_error("invalid INTERVAL value");
    }
    if (st.interval <= 0) {
        throw std::runtime_error("INTERVAL must be at least 1");
    }

    const auto fctemps = parse_pair_list(cfg["FCTEMPS"], "FCTEMPS");
    const auto mintemp = parse_int_pairs(cfg["MINTEMP"], "MINTEMP");
    const auto maxtemp = parse_int_pairs(cfg["MAXTEMP"], "MAXTEMP");
    const auto minstart = parse_int_pairs(cfg["MINSTART"], "MINSTART");
    const auto minstop = parse_int_pairs(cfg["MINSTOP"], "MINSTOP");
    const auto has_nonempty = [&cfg](const char *k) {
        return cfg.count(k) && !trim(cfg.at(k)).empty();
    };

    const auto minpwm = has_nonempty("MINPWM") ? parse_int_pairs(cfg["MINPWM"], "MINPWM")
                                            : std::unordered_map<std::string, int>{};
    const auto maxpwm = has_nonempty("MAXPWM") ? parse_int_pairs(cfg["MAXPWM"], "MAXPWM")
                                            : std::unordered_map<std::string, int>{};
    const auto average = has_nonempty("AVERAGE") ? parse_int_pairs(cfg["AVERAGE"], "AVERAGE")
                                              : std::unordered_map<std::string, int>{};
    const auto fcfans = has_nonempty("FCFANS") ? parse_pairs(cfg["FCFANS"], "FCFANS")
                                            : std::unordered_map<std::string, std::string>{};

    st.mode = detect_mode(fctemps.front().first);
    st.base = base_dir(st.mode);

    if (!file_exists(st.base)) {
        throw std::runtime_error("No sensors found! (did you load the necessary modules?)");
    }

    const auto devpath = has_nonempty("DEVPATH") ? parse_pair_list(cfg["DEVPATH"], "DEVPATH")
                                               : std::vector<std::pair<std::string, std::string>>{};
    const auto devname = has_nonempty("DEVNAME") ? parse_pair_list(cfg["DEVNAME"], "DEVNAME")
                                               : std::vector<std::pair<std::string, std::string>>{};

    if (st.mode != PathMode::Absolute && (devpath.empty() || devname.empty())) {
        throw std::runtime_error("configuration is too old, please run pwmconfig again");
    }
    if (st.mode == PathMode::Absolute && !devpath.empty()) {
        throw std::runtime_error("unneeded DEVPATH with absolute device paths");
    }

    if (st.mode != PathMode::Absolute) {
        if (!validate_devices(st.base, devpath, devname)) {
            throw std::runtime_error("configuration appears to be outdated, please run pwmconfig again");
        }
    }

    for (const auto &kv : fctemps) {
        const auto &pwm_key = kv.first;
        Channel ch;
        ch.pwm_key = pwm_key;
        ch.pwm_rel = pwm_key;
        ch.temp_rel = kv.second;

        if (!mintemp.count(pwm_key) || !maxtemp.count(pwm_key) || !minstart.count(pwm_key) ||
            !minstop.count(pwm_key)) {
            throw std::runtime_error("incomplete settings for " + pwm_key);
        }

        ch.min_temp_c = mintemp.at(pwm_key);
        ch.max_temp_c = maxtemp.at(pwm_key);
        ch.min_start_pwm = minstart.at(pwm_key);
        ch.min_stop_pwm = minstop.at(pwm_key);
        ch.min_pwm = minpwm.count(pwm_key) ? minpwm.at(pwm_key) : 0;
        ch.max_pwm = maxpwm.count(pwm_key) ? maxpwm.at(pwm_key) : kPwmMax;
        ch.average = average.count(pwm_key) ? average.at(pwm_key) : 1;

        if (ch.min_temp_c >= ch.max_temp_c) {
            throw std::runtime_error("MINTEMP must be less than MAXTEMP for " + pwm_key);
        }
        if (ch.max_pwm < 0 || ch.max_pwm > kPwmMax) {
            throw std::runtime_error("MAXPWM must be between 0 and 255 for " + pwm_key);
        }
        if (ch.min_stop_pwm >= ch.max_pwm) {
            throw std::runtime_error("MINSTOP must be less than MAXPWM for " + pwm_key);
        }
        if (ch.min_stop_pwm < ch.min_pwm) {
            throw std::runtime_error("MINSTOP must be >= MINPWM for " + pwm_key);
        }
        if (ch.min_pwm < 0) {
            throw std::runtime_error("MINPWM must be >= 0 for " + pwm_key);
        }
        if (ch.average < 1) {
            throw std::runtime_error("AVERAGE must be >= 1 for " + pwm_key);
        }

        if (fcfans.count(pwm_key)) {
            ch.fan_rel = split_plus(fcfans.at(pwm_key));
        }

        st.channels.push_back(ch);
    }

    if (st.mode == PathMode::Hwmon) {
        for (const auto &entry : devpath) {
            const std::string abs_device = join_path(st.base, entry.first);
            if (file_exists(abs_device + "/name")) {
                fixup_device_files(st.channels, entry.first);
            }
        }
    }

    for (auto &ch : st.channels) {
        ch.pwm_path = join_path(st.base, ch.pwm_rel);
        ch.temp_path = join_path(st.base, ch.temp_rel);
        ch.fan_paths.clear();
        ch.fan_paths.reserve(ch.fan_rel.size());
        for (const auto &f : ch.fan_rel) {
            ch.fan_paths.push_back(join_path(st.base, f));
        }

        std::cerr << "\nSettings for " << ch.pwm_rel << ":\n"
                  << "  Depends on " << ch.temp_rel << "\n"
                  << "  Controls ";
        if (ch.fan_rel.empty()) {
            std::cerr << "\n";
        } else {
            for (std::size_t i = 0; i < ch.fan_rel.size(); ++i) {
                if (i != 0) {
                    std::cerr << '+';
                }
                std::cerr << ch.fan_rel[i];
            }
            std::cerr << "\n";
        }
        std::cerr << "  MINTEMP=" << ch.min_temp_c << "\n"
                  << "  MAXTEMP=" << ch.max_temp_c << "\n"
                  << "  MINSTART=" << ch.min_start_pwm << "\n"
                  << "  MINSTOP=" << ch.min_stop_pwm << "\n"
                  << "  MINPWM=" << ch.min_pwm << "\n"
                  << "  MAXPWM=" << ch.max_pwm << "\n"
                  << "  AVERAGE=" << ch.average << "\n";
    }

    std::cerr << "\nCommon settings:\n"
              << "  INTERVAL=" << st.interval << "\n\n";

    if (!check_files(st.channels)) {
        throw std::runtime_error("configuration check failed");
    }

    return st;
}

bool pwmenable(State &st, const std::string &pwm_path) {
    const std::string enable = pwm_path + "_enable";

    if (file_exists(enable)) {
        const auto orig_mode = try_read_int(enable);
        const auto orig_pwm = try_read_int(pwm_path);

        if (orig_mode && orig_pwm) {
            st.orig_enable[pwm_path] = *orig_mode;
            st.orig_pwm[pwm_path] = *orig_pwm;
            if (st.debug) {
                std::cerr << "Saving " << enable << " original value as " << *orig_mode << "\n";
                std::cerr << "Saving " << pwm_path << " original value as " << *orig_pwm << "\n";
            }
        }

        if (!try_write_int(enable, 1)) {
            return false;
        }
    }

    return try_write_int(pwm_path, kPwmMax);
}

bool pwmdisable(const State &st, const std::string &pwm_path) {
    const std::string enable = pwm_path + "_enable";

    if (!file_exists(enable)) {
        return try_write_int(pwm_path, kPwmMax);
    }

    const auto it_mode = st.orig_enable.find(pwm_path);
    const auto it_pwm = st.orig_pwm.find(pwm_path);
    if (it_mode != st.orig_enable.end() && it_pwm != st.orig_pwm.end()) {
        if (st.debug) {
            std::cerr << "Restoring " << pwm_path << " original value of " << it_pwm->second << "\n";
        }
        (void)try_write_int(pwm_path, it_pwm->second);

        if (it_mode->second != 1) {
            if (st.debug) {
                std::cerr << "Restoring " << enable << " original value of " << it_mode->second << "\n";
            }
            if (try_write_int(enable, it_mode->second)) {
                const auto cur = try_read_int(enable);
                if (cur && *cur == it_mode->second) {
                    return true;
                }
            }
        } else {
            const auto cur_pwm = try_read_int(pwm_path);
            if (cur_pwm && *cur_pwm == it_pwm->second) {
                return true;
            }
        }
    }

    if (try_write_int(enable, 0)) {
        const auto cur = try_read_int(enable);
        if (cur && *cur == 0) {
            return true;
        }
    }

    (void)try_write_int(enable, 1);
    (void)try_write_int(pwm_path, kPwmMax);
    const auto cur_enable = try_read_int(enable);
    const auto cur_pwm = try_read_int(pwm_path);
    if (cur_enable && cur_pwm && *cur_enable == 1 && *cur_pwm >= 190) {
        return true;
    }

    std::cerr << enable << " stuck to ";
    if (cur_enable) {
        std::cerr << *cur_enable << "\n";
    } else {
        std::cerr << "unknown\n";
    }

    return false;
}

void remove_pidfile(const std::string &pidfile) {
    (void)::unlink(pidfile.c_str());
}

void restorefans(const State &st, int status) {
    std::cerr << "Aborting, restoring fans...\n";

    std::set<std::string> done;
    for (const auto &ch : st.channels) {
        if (done.insert(ch.pwm_path).second) {
            (void)pwmdisable(st, ch.pwm_path);
        }
    }

    std::cerr << "Verify fans have returned to full speed\n";
    remove_pidfile(st.pidfile);

    if (status == 0) {
        std::exit(0);
    }
    std::exit(1);
}

int average_temp(Channel &ch, int new_value) {
    ch.temp_hist.push_back(new_value);
    while (static_cast<int>(ch.temp_hist.size()) > ch.average) {
        ch.temp_hist.pop_front();
    }

    long long total = 0;
    for (int v : ch.temp_hist) {
        total += v;
    }
    return static_cast<int>(total / static_cast<long long>(ch.temp_hist.size()));
}

bool update_channel(Channel &ch, bool debug) {
    const auto t_last_opt = try_read_int(ch.temp_path);
    if (!t_last_opt) {
        std::cerr << "Error reading temperature from " << ch.temp_path << "\n";
        return false;
    }

    const auto pwm_prev_opt = try_read_int(ch.pwm_path);
    if (!pwm_prev_opt) {
        std::cerr << "Error reading PWM value from " << ch.pwm_path << "\n";
        return false;
    }

    const int t_avg = average_temp(ch, *t_last_opt);
    const int pwm_prev = *pwm_prev_opt;

    int min_fan = 1;
    if (!ch.fan_paths.empty()) {
        min_fan = std::numeric_limits<int>::max();
        for (const auto &fan : ch.fan_paths) {
            const auto fan_val = try_read_int(fan);
            if (!fan_val) {
                std::cerr << "Error reading Fan value from " << fan << "\n";
                return false;
            }
            min_fan = std::min(min_fan, *fan_val);
        }
    }

    const int mint = ch.min_temp_c * 1000;
    const int maxt = ch.max_temp_c * 1000;

    int pwm_new = ch.min_pwm;
    if (t_avg <= mint) {
        pwm_new = ch.min_pwm;
    } else if (t_avg >= maxt) {
        pwm_new = ch.max_pwm;
    } else {
        const long long num = static_cast<long long>(t_avg - mint) * (ch.max_pwm - ch.min_stop_pwm);
        const long long den = (maxt - mint);
        pwm_new = static_cast<int>(num / den + ch.min_stop_pwm);

        if (pwm_prev == 0 || min_fan == 0) {
            if (!try_write_int(ch.pwm_path, ch.min_start_pwm)) {
                std::cerr << "Error writing PWM value to " << ch.pwm_path << "\n";
                return false;
            }
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    pwm_new = std::clamp(pwm_new, 0, kPwmMax);
    if (!try_write_int(ch.pwm_path, pwm_new)) {
        std::cerr << "Error writing PWM value to " << ch.pwm_path << "\n";
        return false;
    }

    if (debug) {
        std::cerr << "pwmo=" << ch.pwm_path << "\n"
                  << "tsens=" << ch.temp_path << "\n"
                  << "mint=" << mint << "\n"
                  << "maxt=" << maxt << "\n"
                  << "minsa=" << ch.min_start_pwm << "\n"
                  << "minso=" << ch.min_stop_pwm << "\n"
                  << "minpwm=" << ch.min_pwm << "\n"
                  << "maxpwm=" << ch.max_pwm << "\n"
                  << "tlastval=" << *t_last_opt << "\n"
                  << "tval=" << t_avg << "\n"
                  << "pwmpval=" << pwm_prev << "\n"
                  << "min_fanval=" << min_fan << "\n"
                  << "new pwmval=" << pwm_new << "\n";
    }

    return true;
}

std::string pick_config_path(int argc, char **argv) {
    if (argc > 1 && file_exists(argv[1])) {
        return argv[1];
    }
    return "/etc/fancontrol";
}

} // namespace

int main(int argc, char **argv) {
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
        std::cerr << "Loading configuration from " << config << " ...\n";

        State st = load_config(config, debug);

        if (file_exists(st.pidfile)) {
            throw std::runtime_error("File " + st.pidfile + " exists, is fancontrol already running?");
        }

        {
            std::ofstream pid(st.pidfile);
            if (!pid) {
                throw std::runtime_error("cannot create pidfile: " + st.pidfile);
            }
            pid << ::getpid() << '\n';
        }

        std::cerr << "Enabling PWM on fans...\n";
        std::set<std::string> done;
        for (const auto &ch : st.channels) {
            if (done.insert(ch.pwm_path).second) {
                if (!pwmenable(st, ch.pwm_path)) {
                    std::cerr << "Error enabling PWM on " << ch.pwm_path << "\n";
                    restorefans(st, 1);
                }
            }
        }

        std::cerr << "Starting automatic fan control...\n";

        while (!g_stop) {
            for (auto &ch : st.channels) {
                if (!update_channel(ch, st.debug)) {
                    restorefans(st, 1);
                }
            }

            for (int i = 0; i < st.interval && !g_stop; ++i) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }

        restorefans(st, static_cast<int>(g_restore_status));
    } catch (const std::exception &e) {
        std::cerr << "fancontrol: " << e.what() << '\n';
        return 1;
    }

    return 0;
}
