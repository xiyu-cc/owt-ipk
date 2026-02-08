#include "libcore/temp_source.hpp"

#include <array>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>

namespace fancontrol::core {
namespace {

std::optional<int> read_int_file(const std::string &path) {
    std::ifstream in(path);
    if (!in) {
        return std::nullopt;
    }

    long long value = 0;
    in >> value;
    if (in.fail()) {
        return std::nullopt;
    }

    if (value < static_cast<long long>(std::numeric_limits<int>::min()) ||
        value > static_cast<long long>(std::numeric_limits<int>::max())) {
        return std::nullopt;
    }

    return static_cast<int>(value);
}

std::string shell_quote_single(const std::string &in) {
    std::string out;
    out.reserve(in.size() + 2);
    out.push_back('\'');
    for (char c : in) {
        if (c == '\'') {
            out += "'\\''";
        } else {
            out.push_back(c);
        }
    }
    out.push_back('\'');
    return out;
}

std::string run_command_capture(const std::string &cmd) {
    std::array<char, 256> buf {};
    std::string out;

    FILE *fp = ::popen(cmd.c_str(), "r");
    if (!fp) {
        return {};
    }

    while (std::fgets(buf.data(), static_cast<int>(buf.size()), fp) != nullptr) {
        out.append(buf.data());
    }

    const int rc = ::pclose(fp);
    if (rc != 0) {
        return {};
    }

    return out;
}

std::optional<int> parse_json_int_for_key(const std::string &json, const std::string &key) {
    const std::string quoted = "\"" + key + "\"";
    const std::size_t key_pos = json.find(quoted);
    if (key_pos == std::string::npos) {
        return std::nullopt;
    }

    std::size_t pos = json.find(':', key_pos + quoted.size());
    if (pos == std::string::npos) {
        return std::nullopt;
    }
    ++pos;

    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) {
        ++pos;
    }
    if (pos >= json.size()) {
        return std::nullopt;
    }

    bool quoted_num = false;
    if (json[pos] == '\"') {
        quoted_num = true;
        ++pos;
    }

    std::size_t start = pos;
    if (pos < json.size() && (json[pos] == '-' || json[pos] == '+')) {
        ++pos;
    }
    while (pos < json.size() && std::isdigit(static_cast<unsigned char>(json[pos]))) {
        ++pos;
    }

    if (start == pos || (start + 1 == pos && (json[start] == '-' || json[start] == '+'))) {
        return std::nullopt;
    }

    const std::string num = json.substr(start, pos - start);

    if (quoted_num) {
        if (pos >= json.size() || json[pos] != '\"') {
            return std::nullopt;
        }
    }

    try {
        long long v = std::stoll(num);
        if (v < static_cast<long long>(std::numeric_limits<int>::min()) ||
            v > static_cast<long long>(std::numeric_limits<int>::max())) {
            return std::nullopt;
        }
        return static_cast<int>(v);
    } catch (...) {
        return std::nullopt;
    }
}

} // namespace

SysfsTempSource::SysfsTempSource(std::string id, std::string path, std::chrono::seconds poll_interval)
    : id_(std::move(id)), path_(std::move(path)), poll_interval_(poll_interval) {
    if (poll_interval_ < std::chrono::seconds(1)) {
        poll_interval_ = std::chrono::seconds(1);
    }
}

const std::string &SysfsTempSource::id() const {
    return id_;
}

std::chrono::seconds SysfsTempSource::poll_interval() const {
    return poll_interval_;
}

TempSample SysfsTempSource::sample() {
    TempSample s;
    s.sample_ts = std::chrono::steady_clock::now();

    const auto v = read_int_file(path_);
    if (!v) {
        s.ok = false;
        s.error = "cannot read " + path_;
        return s;
    }

    s.ok = true;
    s.temp_mC = *v;
    return s;
}

UbusTempSource::UbusTempSource(std::string id,
                               std::string object,
                               std::string method,
                               std::string key,
                               std::string args_json,
                               std::chrono::seconds poll_interval)
    : id_(std::move(id)),
      object_(std::move(object)),
      method_(std::move(method)),
      key_(std::move(key)),
      args_json_(std::move(args_json)),
      poll_interval_(poll_interval) {
    if (poll_interval_ < std::chrono::seconds(1)) {
        poll_interval_ = std::chrono::seconds(1);
    }
    if (args_json_.empty()) {
        args_json_ = "{}";
    }
}

const std::string &UbusTempSource::id() const {
    return id_;
}

std::chrono::seconds UbusTempSource::poll_interval() const {
    return poll_interval_;
}

TempSample UbusTempSource::sample() {
    TempSample s;
    s.sample_ts = std::chrono::steady_clock::now();

    const std::string cmd = "ubus call " + shell_quote_single(object_) + " " + shell_quote_single(method_) +
                            " " + shell_quote_single(args_json_) + " 2>/dev/null";
    const std::string output = run_command_capture(cmd);
    if (output.empty()) {
        s.ok = false;
        s.error = "ubus call failed for " + object_ + "." + method_;
        return s;
    }

    const auto parsed = parse_json_int_for_key(output, key_);
    if (!parsed) {
        s.ok = false;
        s.error = "ubus key not found or invalid: " + key_;
        return s;
    }

    s.ok = true;
    s.temp_mC = *parsed;
    return s;
}

void SourceManager::add(std::unique_ptr<ITempSource> source) {
    SourceRuntime rt;
    rt.source = std::move(source);
    rt.last_poll = std::chrono::steady_clock::time_point::min();
    rt.has_polled = false;
    runtimes_.push_back(std::move(rt));
}

void SourceManager::poll(bool debug) {
    const auto now = std::chrono::steady_clock::now();

    for (auto &rt : runtimes_) {
        if (!rt.source) {
            continue;
        }

        if (rt.has_polled && now - rt.last_poll < rt.source->poll_interval()) {
            continue;
        }

        rt.last_sample = rt.source->sample();
        rt.last_poll = now;
        rt.has_polled = true;

        if (debug && rt.last_sample) {
            if (rt.last_sample->ok) {
                std::cerr << "source[" << rt.source->id() << "]=" << rt.last_sample->temp_mC << "mC\n";
            } else {
                std::cerr << "source[" << rt.source->id() << "] error: " << rt.last_sample->error << "\n";
            }
        }
    }
}

const std::vector<SourceRuntime> &SourceManager::runtimes() const {
    return runtimes_;
}

} // namespace fancontrol::core
