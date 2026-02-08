#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <limits.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {

struct Candidate {
    std::string hwmon_dir;
    std::string hwmon_name;
    std::string devpath_rel;
    std::string devname_sanitized;
    std::string pwm_rel;
    std::string temp_rel;
    std::string fan_rel;
};

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

std::string read_line(const fs::path &p) {
    std::ifstream in(p);
    if (!in) {
        return {};
    }
    std::string line;
    std::getline(in, line);
    return trim(line);
}

std::string sanitize_device_name(std::string in) {
    for (char &c : in) {
        if (std::isspace(static_cast<unsigned char>(c)) || c == '=') {
            c = '_';
        }
    }
    return in;
}

bool is_regular_writable(const fs::path &p) {
    struct stat st {};
    if (::stat(p.c_str(), &st) != 0) {
        return false;
    }
    if (!S_ISREG(st.st_mode)) {
        return false;
    }
    return ::access(p.c_str(), W_OK) == 0;
}

std::string realpath_string(const fs::path &p) {
    char buf[PATH_MAX] = {};
    if (::realpath(p.c_str(), buf) == nullptr) {
        return {};
    }
    return std::string(buf);
}

std::string rel_from_sys(const std::string &abs_path) {
    static const std::string prefix = "/sys/";
    if (abs_path.rfind(prefix, 0) == 0) {
        return abs_path.substr(prefix.size());
    }
    return abs_path;
}

std::optional<int> extract_index(const std::string &name, const std::string &prefix, const std::string &suffix) {
    if (name.rfind(prefix, 0) != 0 || name.size() <= prefix.size() + suffix.size()) {
        return std::nullopt;
    }
    if (name.substr(name.size() - suffix.size()) != suffix) {
        return std::nullopt;
    }
    const std::string mid = name.substr(prefix.size(), name.size() - prefix.size() - suffix.size());
    if (mid.empty()) {
        return std::nullopt;
    }
    for (char c : mid) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            return std::nullopt;
        }
    }
    try {
        return std::stoi(mid);
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<fs::path> first_match_sorted(const fs::path &dir, const std::regex &rx, bool need_readable) {
    std::vector<fs::path> files;
    for (const auto &ent : fs::directory_iterator(dir)) {
        if (!ent.is_regular_file()) {
            continue;
        }
        const std::string name = ent.path().filename().string();
        if (!std::regex_match(name, rx)) {
            continue;
        }
        if (need_readable && ::access(ent.path().c_str(), R_OK) != 0) {
            continue;
        }
        files.push_back(ent.path());
    }
    std::sort(files.begin(), files.end());
    if (files.empty()) {
        return std::nullopt;
    }
    return files.front();
}

std::vector<Candidate> detect_candidates() {
    std::vector<Candidate> out;
    const fs::path hwmon_root("/sys/class/hwmon");
    if (!fs::exists(hwmon_root) || !fs::is_directory(hwmon_root)) {
        throw std::runtime_error("cannot find /sys/class/hwmon");
    }

    std::vector<fs::path> hwmons;
    for (const auto &ent : fs::directory_iterator(hwmon_root)) {
        if (!ent.is_directory()) {
            continue;
        }
        const std::string name = ent.path().filename().string();
        if (name.rfind("hwmon", 0) == 0) {
            hwmons.push_back(ent.path());
        }
    }
    std::sort(hwmons.begin(), hwmons.end());

    for (const auto &hwmon_dir : hwmons) {
        const std::string hwname = hwmon_dir.filename().string();
        const std::string devname = sanitize_device_name(read_line(hwmon_dir / "name"));
        const std::string devreal = realpath_string(hwmon_dir / "device");
        const std::string devpath_rel = devreal.empty() ? "" : rel_from_sys(devreal);

        std::vector<fs::path> pwms;
        for (const auto &ent : fs::directory_iterator(hwmon_dir)) {
            if (!ent.is_regular_file()) {
                continue;
            }
            const std::string fname = ent.path().filename().string();
            if (!extract_index(fname, "pwm", "")) {
                continue;
            }
            if (fname.find("_") != std::string::npos) {
                continue;
            }
            if (is_regular_writable(ent.path())) {
                pwms.push_back(ent.path());
            }
        }
        std::sort(pwms.begin(), pwms.end());

        for (const auto &pwm : pwms) {
            const std::string pwm_name = pwm.filename().string();
            const auto pwm_idx = extract_index(pwm_name, "pwm", "");
            if (!pwm_idx) {
                continue;
            }

            fs::path temp = hwmon_dir / ("temp" + std::to_string(*pwm_idx) + "_input");
            if (!fs::exists(temp) || ::access(temp.c_str(), R_OK) != 0) {
                const auto fallback = first_match_sorted(hwmon_dir, std::regex(R"(temp[0-9]+_input)"), true);
                if (!fallback) {
                    continue;
                }
                temp = *fallback;
            }

            fs::path fan = hwmon_dir / ("fan" + std::to_string(*pwm_idx) + "_input");
            std::string fan_rel;
            if (fs::exists(fan) && ::access(fan.c_str(), R_OK) == 0) {
                fan_rel = hwname + "/" + fan.filename().string();
            } else {
                const auto fallback = first_match_sorted(hwmon_dir, std::regex(R"(fan[0-9]+_input)"), true);
                if (fallback) {
                    fan_rel = hwname + "/" + fallback->filename().string();
                }
            }

            Candidate c;
            c.hwmon_dir = hwmon_dir.string();
            c.hwmon_name = hwname;
            c.devpath_rel = devpath_rel;
            c.devname_sanitized = devname.empty() ? hwname : devname;
            c.pwm_rel = hwname + "/" + pwm_name;
            c.temp_rel = hwname + "/" + temp.filename().string();
            c.fan_rel = fan_rel;
            out.push_back(c);
        }
    }

    return out;
}

std::string ask_string(const std::string &prompt, const std::string &def) {
    std::cout << prompt << " [" << def << "]: " << std::flush;
    std::string line;
    std::getline(std::cin, line);
    line = trim(line);
    return line.empty() ? def : line;
}

int ask_int(const std::string &prompt, int def, int minv, int maxv) {
    while (true) {
        const std::string in = ask_string(prompt, std::to_string(def));
        try {
            long v = std::stol(in);
            if (v < minv || v > maxv) {
                std::cout << "Value must be in [" << minv << ", " << maxv << "]\n";
                continue;
            }
            return static_cast<int>(v);
        } catch (...) {
            std::cout << "Please input a valid integer.\n";
        }
    }
}

bool ask_yesno(const std::string &prompt, bool def) {
    const std::string d = def ? "Y/n" : "y/N";
    while (true) {
        std::cout << prompt << " [" << d << "]: " << std::flush;
        std::string line;
        std::getline(std::cin, line);
        line = trim(line);
        if (line.empty()) {
            return def;
        }
        const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(line[0])));
        if (c == 'y') {
            return true;
        }
        if (c == 'n') {
            return false;
        }
    }
}

struct Selected {
    Candidate c;
    int mintemp = 45;
    int maxtemp = 65;
    int minstart = 150;
    int minstop = 80;
    int minpwm = 0;
    int maxpwm = 255;
    int average = 1;
    std::string fan_rel;
};

void write_config(const std::string &path, int interval, const std::vector<Selected> &items) {
    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("cannot write output file: " + path);
    }

    std::set<std::string> hwmons;
    std::unordered_map<std::string, std::string> devpath;
    std::unordered_map<std::string, std::string> devname;

    for (const auto &it : items) {
        hwmons.insert(it.c.hwmon_name);
        if (!it.c.devpath_rel.empty()) {
            devpath[it.c.hwmon_name] = it.c.devpath_rel;
        }
        devname[it.c.hwmon_name] = it.c.devname_sanitized;
    }

    auto join_pairs = [](const std::vector<std::pair<std::string, std::string>> &v) {
        std::ostringstream oss;
        for (std::size_t i = 0; i < v.size(); ++i) {
            if (i != 0) {
                oss << ' ';
            }
            oss << v[i].first << "=" << v[i].second;
        }
        return oss.str();
    };

    std::vector<std::pair<std::string, std::string>> devpath_pairs;
    std::vector<std::pair<std::string, std::string>> devname_pairs;
    for (const auto &h : hwmons) {
        if (devpath.count(h)) {
            devpath_pairs.emplace_back(h, devpath[h]);
        }
        devname_pairs.emplace_back(h, devname[h]);
    }

    std::vector<std::pair<std::string, std::string>> fctemps;
    std::vector<std::pair<std::string, std::string>> fcfans;
    std::vector<std::pair<std::string, std::string>> mintemp;
    std::vector<std::pair<std::string, std::string>> maxtemp;
    std::vector<std::pair<std::string, std::string>> minstart;
    std::vector<std::pair<std::string, std::string>> minstop;
    std::vector<std::pair<std::string, std::string>> minpwm;
    std::vector<std::pair<std::string, std::string>> maxpwm;
    std::vector<std::pair<std::string, std::string>> average;

    for (const auto &it : items) {
        fctemps.emplace_back(it.c.pwm_rel, it.c.temp_rel);
        if (!it.fan_rel.empty()) {
            fcfans.emplace_back(it.c.pwm_rel, it.fan_rel);
        }
        mintemp.emplace_back(it.c.pwm_rel, std::to_string(it.mintemp));
        maxtemp.emplace_back(it.c.pwm_rel, std::to_string(it.maxtemp));
        minstart.emplace_back(it.c.pwm_rel, std::to_string(it.minstart));
        minstop.emplace_back(it.c.pwm_rel, std::to_string(it.minstop));
        minpwm.emplace_back(it.c.pwm_rel, std::to_string(it.minpwm));
        maxpwm.emplace_back(it.c.pwm_rel, std::to_string(it.maxpwm));
        average.emplace_back(it.c.pwm_rel, std::to_string(it.average));
    }

    out << "INTERVAL=" << interval << "\n";
    if (!devpath_pairs.empty()) {
        out << "DEVPATH=" << join_pairs(devpath_pairs) << "\n";
    }
    if (!devname_pairs.empty()) {
        out << "DEVNAME=" << join_pairs(devname_pairs) << "\n";
    }
    out << "FCTEMPS=" << join_pairs(fctemps) << "\n";
    if (!fcfans.empty()) {
        out << "FCFANS=" << join_pairs(fcfans) << "\n";
    }
    out << "MINTEMP=" << join_pairs(mintemp) << "\n";
    out << "MAXTEMP=" << join_pairs(maxtemp) << "\n";
    out << "MINSTART=" << join_pairs(minstart) << "\n";
    out << "MINSTOP=" << join_pairs(minstop) << "\n";
    out << "MINPWM=" << join_pairs(minpwm) << "\n";
    out << "MAXPWM=" << join_pairs(maxpwm) << "\n";
    out << "AVERAGE=" << join_pairs(average) << "\n";
}

} // namespace

int main() {
    try {
        std::cout << "pwmconfig (C++)\n";
        std::cout << "This tool scans hwmon PWM/temperature sensors and writes /etc/fancontrol.\n\n";

        auto candidates = detect_candidates();
        if (candidates.empty()) {
            std::cerr << "No writable PWM controls found under /sys/class/hwmon.\n";
            return 1;
        }

        std::vector<Selected> selected;
        for (const auto &c : candidates) {
            std::cout << "Detected: PWM=" << c.pwm_rel
                      << " TEMP=" << c.temp_rel
                      << " FAN=" << (c.fan_rel.empty() ? "-" : c.fan_rel)
                      << " NAME=" << c.devname_sanitized << "\n";

            if (!ask_yesno("Use this PWM channel?", true)) {
                continue;
            }

            Selected s;
            s.c = c;
            s.fan_rel = c.fan_rel;

            s.mintemp = ask_int("MINTEMP (C)", 45, -100, 200);
            s.maxtemp = ask_int("MAXTEMP (C)", 65, -100, 250);
            if (s.mintemp >= s.maxtemp) {
                std::cout << "MINTEMP must be lower than MAXTEMP, applying MAXTEMP=MINTEMP+10.\n";
                s.maxtemp = s.mintemp + 10;
            }
            s.minstart = ask_int("MINSTART (0-255)", 150, 0, 255);
            s.minstop = ask_int("MINSTOP (0-255)", 80, 0, 255);
            s.minpwm = ask_int("MINPWM (0-255)", 0, 0, 255);
            s.maxpwm = ask_int("MAXPWM (0-255)", 255, 0, 255);
            s.average = ask_int("AVERAGE (>=1)", 1, 1, 100);

            if (!s.fan_rel.empty() && !ask_yesno("Use fan feedback sensor " + s.fan_rel + " ?", true)) {
                s.fan_rel.clear();
            }

            if (s.minstop >= s.maxpwm) {
                std::cout << "MINSTOP must be lower than MAXPWM, clamping MINSTOP to MAXPWM-1.\n";
                s.minstop = std::max(0, s.maxpwm - 1);
            }
            if (s.minstop < s.minpwm) {
                std::cout << "MINSTOP must be >= MINPWM, setting MINSTOP=MINPWM.\n";
                s.minstop = s.minpwm;
            }

            selected.push_back(s);
            std::cout << "\n";
        }

        if (selected.empty()) {
            std::cerr << "No channels selected, nothing written.\n";
            return 1;
        }

        const int interval = ask_int("Update interval in seconds", 10, 1, 3600);
        const std::string output = ask_string("Output config file", "/etc/fancontrol");

        write_config(output, interval, selected);
        std::cout << "Configuration written: " << output << "\n";
        std::cout << "Now run: /etc/init.d/fancontrol start\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "pwmconfig: " << e.what() << "\n";
        return 1;
    }
}
