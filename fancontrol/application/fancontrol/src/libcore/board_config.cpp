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

#include "libcore/config_spec.hpp"
#include "libcore/json.hpp"

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

std::string to_lower_ascii(std::string s) {
    for (char &c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

int to_int(const std::string &in, const std::string &name) {
    try {
        std::size_t idx = 0;
        const long long v = std::stoll(in, &idx, 10);
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

bool enum_contains(const EnumFieldSpec &spec, const std::string &value) {
    for (std::size_t i = 0; i < spec.allowed_count; ++i) {
        if (value == spec.allowed_values[i]) {
            return true;
        }
    }
    return false;
}

void ensure_int_in_range(const std::string &name, int value, int min_value, int max_value, bool has_max) {
    if (value < min_value) {
        throw std::runtime_error(name + " must be >= " + std::to_string(min_value));
    }
    if (has_max && value > max_value) {
        throw std::runtime_error(name + " must be in range [" + std::to_string(min_value) + ", " +
                                 std::to_string(max_value) + "]");
    }
}

nlohmann::json int_field_spec_to_json(const IntFieldSpec &field) {
    nlohmann::json out = {
        {"key", field.key},
        {"type", "integer"},
        {"default", field.default_value},
        {"min", field.min_value},
        {"description", field.description},
    };
    if (field.has_max) {
        out["max"] = field.max_value;
    }
    return out;
}

nlohmann::json string_field_spec_to_json(const StringFieldSpec &field) {
    return {
        {"key", field.key},
        {"type", "string"},
        {"default", field.default_value ? field.default_value : ""},
        {"required", field.required ? 1 : 0},
        {"description", field.description},
    };
}

nlohmann::json enum_field_spec_to_json(const EnumFieldSpec &field) {
    nlohmann::json values = nlohmann::json::array();
    for (std::size_t i = 0; i < field.allowed_count; ++i) {
        values.push_back(field.allowed_values[i]);
    }
    return {
        {"key", field.key},
        {"type", "enum"},
        {"default", field.default_value},
        {"values", values},
        {"description", field.description},
    };
}

nlohmann::json source_field_spec_to_json(const SourceFieldSpec &field) {
    nlohmann::json out = {
        {"key", field.key},
        {"type", "integer"},
        {"default", field.default_value},
        {"min", field.min_value},
        {"description", field.description},
    };
    if (field.has_max) {
        out["max"] = field.max_value;
    }
    return out;
}

std::string canonicalize_json_object_text(const std::string &json_text, const std::string &name) {
    try {
        const nlohmann::json parsed = nlohmann::json::parse(json_text);
        if (!parsed.is_object()) {
            throw std::runtime_error(name + " must be a JSON object");
        }
        return parsed.dump();
    } catch (const std::exception &e) {
        throw std::runtime_error("invalid JSON for " + name + ": " + std::string(e.what()));
    }
}

bool is_valid_source_id(const std::string &id) {
    if (id.empty()) {
        return false;
    }
    for (const char c : id) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (!(std::isalnum(uc) || c == '_' || c == '-')) {
            return false;
        }
    }
    return true;
}

std::string canonicalize_path_text(const std::string &path) {
    const std::string in = trim(path);
    if (in.empty()) {
        return {};
    }

    const bool absolute = (in.front() == '/');
    std::vector<std::string> parts;
    std::size_t i = 0;

    while (i < in.size()) {
        while (i < in.size() && in[i] == '/') {
            ++i;
        }
        if (i >= in.size()) {
            break;
        }

        const std::size_t start = i;
        while (i < in.size() && in[i] != '/') {
            ++i;
        }
        std::string seg = in.substr(start, i - start);

        if (seg.empty() || seg == ".") {
            continue;
        }
        if (seg == "..") {
            if (!parts.empty() && parts.back() != "..") {
                parts.pop_back();
            } else if (!absolute) {
                parts.push_back(seg);
            }
            continue;
        }
        parts.push_back(std::move(seg));
    }

    std::ostringstream out;
    if (absolute) {
        out << '/';
    }
    for (std::size_t idx = 0; idx < parts.size(); ++idx) {
        if (idx > 0) {
            out << '/';
        }
        out << parts[idx];
    }

    std::string normalized = out.str();
    if (normalized.empty()) {
        return absolute ? std::string("/") : std::string(".");
    }
    return normalized;
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
        if (token.empty()) {
            continue;
        }

        const std::size_t eq = token.find('=');
        if (eq == std::string::npos || eq == 0 || eq + 1 >= token.size()) {
            throw std::runtime_error("bad source token: " + token);
        }
        const std::string k = trim(token.substr(0, eq));
        const std::string val = trim(token.substr(eq + 1));
        if (k.empty()) {
            throw std::runtime_error("bad source token: " + token);
        }

        const auto [it, inserted] = kv.emplace(k, val);
        if (!inserted) {
            throw std::runtime_error("duplicate source field: " + k);
        }
        (void)it;
    }

    return kv;
}

bool is_allowed_source_field(const std::string &type, const std::string &field) {
    static const std::unordered_set<std::string> common = {
        "type",
        "t_start",
        "t_full",
        "t_crit",
        "ttl",
        "poll",
        "weight",
    };
    static const std::unordered_set<std::string> sysfs_only = {
        "path",
    };
    static const std::unordered_set<std::string> ubus_only = {
        "object",
        "method",
        "key",
        "args",
    };

    if (common.find(field) != common.end()) {
        return true;
    }
    if (type == "sysfs") {
        return sysfs_only.find(field) != sysfs_only.end();
    }
    if (type == "ubus") {
        return ubus_only.find(field) != ubus_only.end();
    }
    return false;
}

std::string strip_inline_comment(const std::string &line) {
    std::string out;
    out.reserve(line.size());

    int brace_depth = 0;
    int bracket_depth = 0;
    bool in_quote = false;
    bool escape = false;
    char quote = '\0';

    for (char ch : line) {
        if (in_quote) {
            out.push_back(ch);
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
            out.push_back(ch);
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

        if (ch == '#' && brace_depth == 0 && bracket_depth == 0) {
            break;
        }

        out.push_back(ch);
    }

    return out;
}

BoardSourceConfig parse_source_line(const std::string &id, const std::string &rhs, int fallback_poll_sec) {
    const ConfigSpec &spec = board_config_spec();
    BoardSourceConfig src;
    src.id = trim(id);

    const auto kv = parse_csv_pairs(rhs);
    if (!kv.count("type")) {
        throw std::runtime_error("SOURCE_" + id + " missing required field: type");
    }

    src.type = to_lower_ascii(trim(kv.at("type")));
    for (const auto &entry : kv) {
        if (!is_allowed_source_field(src.type, entry.first)) {
            throw std::runtime_error("unknown field for SOURCE_" + id + ": " + entry.first);
        }
    }

    src.poll_sec = kv.count("poll") ? to_int(kv.at("poll"), "poll") : fallback_poll_sec;

    if (kv.count("ttl")) {
        src.ttl_sec = to_int(kv.at("ttl"), "ttl");
    } else {
        const long long ttl_default =
            std::max(static_cast<long long>(src.poll_sec) * 2LL, static_cast<long long>(fallback_poll_sec) * 2LL);
        if (ttl_default < static_cast<long long>(std::numeric_limits<int>::min()) ||
            ttl_default > static_cast<long long>(std::numeric_limits<int>::max())) {
            throw std::runtime_error("ttl default is out of range for SOURCE_" + id);
        }
        src.ttl_sec = static_cast<int>(ttl_default);
    }

    src.weight = kv.count("weight") ? to_int(kv.at("weight"), "weight") : spec.source_weight.default_value;
    src.t_start_mC = kv.count("t_start") ? to_int(kv.at("t_start"), "t_start") : spec.source_t_start_mC.default_value;
    src.t_full_mC = kv.count("t_full") ? to_int(kv.at("t_full"), "t_full") : spec.source_t_full_mC.default_value;
    src.t_crit_mC = kv.count("t_crit") ? to_int(kv.at("t_crit"), "t_crit") : spec.source_t_crit_mC.default_value;

    if (src.type == "sysfs") {
        if (kv.count("path")) {
            src.path = trim(kv.at("path"));
        }
    } else if (src.type == "ubus") {
        if (kv.count("object")) {
            src.object = trim(kv.at("object"));
        }
        if (kv.count("method")) {
            src.method = trim(kv.at("method"));
        }
        if (kv.count("key")) {
            src.key = trim(kv.at("key"));
        }
        src.args_json = kv.count("args") ? trim(kv.at("args")) : "{}";
    } else {
        throw std::runtime_error("unsupported source type for SOURCE_" + id + ": " + src.type);
    }

    return src;
}

std::string source_resource_key(const BoardSourceConfig &src) {
    if (src.type == "sysfs") {
        return "sysfs:" + src.path;
    }
    if (src.type == "ubus") {
        return "ubus:" + src.object + "|" + src.method + "|" + src.key + "|" + src.args_json;
    }
    return src.type + ":";
}

bool is_known_top_level_key(const std::string &key) {
    const ConfigSpec &spec = board_config_spec();
    static const std::unordered_set<std::string> known = {
        spec.interval_sec.key,
        spec.control_mode.key,
        spec.pwm_path.key,
        spec.pwm_enable_path.key,
        spec.control_mode_path.key,
        spec.pwm_min.key,
        spec.pwm_max.key,
        spec.ramp_up.key,
        spec.ramp_down.key,
        spec.hysteresis_mC.key,
        spec.failsafe_pwm.key,
    };
    return known.find(key) != known.end();
}

nlohmann::json source_to_json(const BoardSourceConfig &src) {
    return {
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
        {"weight", src.weight},
    };
}

nlohmann::json board_config_to_json(const BoardConfig &cfg) {
    nlohmann::json root = {
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
        {"sources", nlohmann::json::array()},
    };

    for (const auto &src : cfg.sources) {
        root["sources"].push_back(source_to_json(src));
    }

    return root;
}

} // namespace

BoardConfig default_board_config() {
    const ConfigSpec &spec = board_config_spec();
    BoardConfig cfg;
    cfg.interval_sec = spec.interval_sec.default_value;
    cfg.control_mode = spec.control_mode.default_value;
    cfg.pwm_path = spec.pwm_path.default_value;
    cfg.pwm_enable_path = spec.pwm_enable_path.default_value;
    cfg.control_mode_path = spec.control_mode_path.default_value;
    cfg.pwm_min = spec.pwm_min.default_value;
    cfg.pwm_max = spec.pwm_max.default_value;
    cfg.ramp_up = spec.ramp_up.default_value;
    cfg.ramp_down = spec.ramp_down.default_value;
    cfg.hysteresis_mC = spec.hysteresis_mC.default_value;
    cfg.failsafe_pwm = spec.failsafe_pwm.default_value;

    cfg.sources.clear();
    cfg.sources.reserve(spec.source_template_count);
    for (std::size_t i = 0; i < spec.source_template_count; ++i) {
        const SourceTemplateSpec &tpl = spec.source_templates[i];
        cfg.sources.push_back({
            tpl.id,
            tpl.type,
            tpl.path,
            tpl.object,
            tpl.method,
            tpl.key,
            tpl.args_json,
            tpl.t_start_mC,
            tpl.t_full_mC,
            tpl.t_crit_mC,
            tpl.ttl_sec,
            tpl.poll_sec,
            tpl.weight,
        });
    }

    return cfg;
}

void validate_board_config(BoardConfig &cfg) {
    const ConfigSpec &spec = board_config_spec();
    cfg.control_mode = to_lower_ascii(trim(cfg.control_mode));
    if (cfg.control_mode.empty()) {
        cfg.control_mode = spec.control_mode.default_value;
    }
    if (!enum_contains(spec.control_mode, cfg.control_mode)) {
        throw std::runtime_error("CONTROL_MODE must be one of: kernel, user");
    }

    cfg.pwm_path = trim(cfg.pwm_path);
    cfg.pwm_enable_path = trim(cfg.pwm_enable_path);
    cfg.control_mode_path = trim(cfg.control_mode_path);

    ensure_int_in_range(spec.interval_sec.key, cfg.interval_sec, spec.interval_sec.min_value, spec.interval_sec.max_value,
                        spec.interval_sec.has_max);
    if (cfg.pwm_path.empty()) {
        throw std::runtime_error("missing mandatory setting: PWM_PATH");
    }
    if (cfg.pwm_enable_path.empty()) {
        cfg.pwm_enable_path = cfg.pwm_path + "_enable";
    }
    if (cfg.control_mode_path.empty()) {
        cfg.control_mode_path = spec.control_mode_path.default_value;
    }

    ensure_int_in_range(spec.pwm_min.key, cfg.pwm_min, spec.pwm_min.min_value, spec.pwm_min.max_value, spec.pwm_min.has_max);
    ensure_int_in_range(spec.pwm_max.key, cfg.pwm_max, spec.pwm_max.min_value, spec.pwm_max.max_value, spec.pwm_max.has_max);
    ensure_int_in_range(spec.failsafe_pwm.key,
                        cfg.failsafe_pwm,
                        spec.failsafe_pwm.min_value,
                        spec.failsafe_pwm.max_value,
                        spec.failsafe_pwm.has_max);
    ensure_int_in_range(spec.ramp_up.key, cfg.ramp_up, spec.ramp_up.min_value, spec.ramp_up.max_value, spec.ramp_up.has_max);
    ensure_int_in_range(spec.ramp_down.key,
                        cfg.ramp_down,
                        spec.ramp_down.min_value,
                        spec.ramp_down.max_value,
                        spec.ramp_down.has_max);
    ensure_int_in_range(spec.hysteresis_mC.key,
                        cfg.hysteresis_mC,
                        spec.hysteresis_mC.min_value,
                        spec.hysteresis_mC.max_value,
                        spec.hysteresis_mC.has_max);

    std::unordered_set<std::string> seen_source_ids;
    std::unordered_map<std::string, std::string> seen_resource_owner;

    for (auto &src : cfg.sources) {
        src.id = trim(src.id);
        src.type = to_lower_ascii(trim(src.type));
        src.path = trim(src.path);
        src.object = trim(src.object);
        src.method = trim(src.method);
        src.key = trim(src.key);
        src.args_json = trim(src.args_json);

        if (!is_valid_source_id(src.id)) {
            throw std::runtime_error("invalid SOURCE id: " + src.id + " (expected pattern " +
                                     std::string(spec.source_id_pattern) + ")");
        }
        if (!seen_source_ids.insert(src.id).second) {
            throw std::runtime_error("duplicate SOURCE id: " + src.id);
        }

        const std::string source_field_prefix = "SOURCE_" + src.id + " ";
        if (src.poll_sec < spec.source_poll_sec.min_value) {
            throw std::runtime_error(source_field_prefix + "poll must be >= " + std::to_string(spec.source_poll_sec.min_value));
        }
        if (spec.source_poll_sec.has_max && src.poll_sec > spec.source_poll_sec.max_value) {
            throw std::runtime_error(source_field_prefix + "poll must be <= " + std::to_string(spec.source_poll_sec.max_value));
        }
        if (src.ttl_sec < spec.source_ttl_sec.min_value) {
            throw std::runtime_error(source_field_prefix + "ttl must be >= " + std::to_string(spec.source_ttl_sec.min_value));
        }
        if (src.ttl_sec < src.poll_sec) {
            throw std::runtime_error("SOURCE_" + src.id + " ttl must be >= poll");
        }
        if (src.weight < spec.source_weight.min_value ||
            (spec.source_weight.has_max && src.weight > spec.source_weight.max_value)) {
            throw std::runtime_error("SOURCE_" + src.id + " weight must be in range [" +
                                     std::to_string(spec.source_weight.min_value) + ", " +
                                     std::to_string(spec.source_weight.max_value) + "]");
        }
        if (src.t_start_mC < spec.source_t_start_mC.min_value ||
            (spec.source_t_start_mC.has_max && src.t_start_mC > spec.source_t_start_mC.max_value)) {
            throw std::runtime_error("SOURCE_" + src.id + " t_start out of allowed range");
        }
        if (src.t_full_mC < spec.source_t_full_mC.min_value ||
            (spec.source_t_full_mC.has_max && src.t_full_mC > spec.source_t_full_mC.max_value)) {
            throw std::runtime_error("SOURCE_" + src.id + " t_full out of allowed range");
        }
        if (src.t_crit_mC < spec.source_t_crit_mC.min_value ||
            (spec.source_t_crit_mC.has_max && src.t_crit_mC > spec.source_t_crit_mC.max_value)) {
            throw std::runtime_error("SOURCE_" + src.id + " t_crit out of allowed range");
        }
        if (!(src.t_start_mC < src.t_full_mC && src.t_full_mC <= src.t_crit_mC)) {
            throw std::runtime_error("invalid thermal thresholds for SOURCE_" + src.id);
        }

        if (src.type == "sysfs") {
            if (src.path.empty()) {
                throw std::runtime_error("SOURCE_" + src.id + " missing required field: path");
            }
            src.path = canonicalize_path_text(src.path);
            if (src.path.empty() || src.path == "." || src.path.front() != '/') {
                throw std::runtime_error("SOURCE_" + src.id + " path must be an absolute sysfs path");
            }
            src.object.clear();
            src.method.clear();
            src.key.clear();
            src.args_json.clear();
        } else if (src.type == "ubus") {
            if (src.object.empty() || src.method.empty() || src.key.empty()) {
                throw std::runtime_error("SOURCE_" + src.id + " missing required fields for ubus");
            }
            if (src.args_json.empty()) {
                src.args_json = "{}";
            }
            src.args_json = canonicalize_json_object_text(src.args_json, "SOURCE_" + src.id + " args");
            src.path.clear();
        } else {
            throw std::runtime_error("unsupported source type for SOURCE_" + src.id + ": " + src.type);
        }

        const std::string resource = source_resource_key(src);
        const auto owner = seen_resource_owner.find(resource);
        if (owner != seen_resource_owner.end()) {
            throw std::runtime_error("duplicate source resource: SOURCE_" + src.id +
                                     " conflicts with SOURCE_" + owner->second);
        }
        seen_resource_owner.emplace(resource, src.id);
    }

    if (cfg.sources.empty()) {
        throw std::runtime_error("no SOURCE_* entries found in board config");
    }
}

std::string render_board_config_text(const BoardConfig &cfg) {
    std::ostringstream out;
    out << "# Configuration file generated by fancontrol\n";
    out << "INTERVAL=" << cfg.interval_sec << '\n';
    out << "CONTROL_MODE=" << cfg.control_mode << '\n';
    out << "PWM_PATH=" << cfg.pwm_path << '\n';
    out << "PWM_ENABLE_PATH=" << cfg.pwm_enable_path << '\n';
    out << "CONTROL_MODE_PATH=" << cfg.control_mode_path << '\n';
    out << "PWM_MIN=" << cfg.pwm_min << '\n';
    out << "PWM_MAX=" << cfg.pwm_max << '\n';
    out << "RAMP_UP=" << cfg.ramp_up << '\n';
    out << "RAMP_DOWN=" << cfg.ramp_down << '\n';
    out << "HYSTERESIS_MC=" << cfg.hysteresis_mC << '\n';
    out << "FAILSAFE_PWM=" << cfg.failsafe_pwm << '\n';

    for (const auto &src : cfg.sources) {
        out << "SOURCE_" << src.id << "=type=" << src.type;
        if (src.type == "sysfs") {
            out << ",path=" << src.path;
        } else {
            out << ",object=" << src.object
                << ",method=" << src.method
                << ",key=" << src.key
                << ",args=" << src.args_json;
        }
        out << ",t_start=" << src.t_start_mC
            << ",t_full=" << src.t_full_mC
            << ",t_crit=" << src.t_crit_mC
            << ",ttl=" << src.ttl_sec
            << ",poll=" << src.poll_sec
            << ",weight=" << src.weight
            << '\n';
    }

    return out.str();
}

std::string dump_board_schema_json() {
    const ConfigSpec &spec = board_config_spec();
    BoardConfig defaults = default_board_config();
    validate_board_config(defaults);

    nlohmann::json source_templates = nlohmann::json::object();
    for (const auto &src : defaults.sources) {
        if (source_templates.find(src.type) == source_templates.end()) {
            source_templates[src.type] = source_to_json(src);
        }
    }

    nlohmann::json root = {
        {"ok", 1},
        {"constants",
         {
             {"config_path", kDefaultConfigPath},
             {"pidfile_path", kFixedPidfilePath},
             {"runtime_status_path", kRuntimeStatusPath},
             {"default_pwm_path", kDefaultPwmPath},
             {"default_pwm_enable_path", kDefaultPwmEnablePath},
             {"default_control_mode_path", kDefaultControlModePath},
         }},
        {"limits",
         {
             {"interval", {{"min", spec.interval_sec.min_value}}},
             {"pwm", {{"min", spec.pwm_min.min_value}, {"max", spec.pwm_max.max_value}}},
             {"ramp", {{"min", spec.ramp_up.min_value}}},
             {"hysteresis_mC", {{"min", spec.hysteresis_mC.min_value}}},
             {"source_weight", {{"min", spec.source_weight.min_value}, {"max", spec.source_weight.max_value}}},
             {"source_poll", {{"min", spec.source_poll_sec.min_value}}},
         }},
        {"config_spec",
         {
             {"top_level",
              nlohmann::json::array(
                  {int_field_spec_to_json(spec.interval_sec),
                   enum_field_spec_to_json(spec.control_mode),
                   string_field_spec_to_json(spec.pwm_path),
                   string_field_spec_to_json(spec.pwm_enable_path),
                   string_field_spec_to_json(spec.control_mode_path),
                   int_field_spec_to_json(spec.pwm_min),
                   int_field_spec_to_json(spec.pwm_max),
                   int_field_spec_to_json(spec.ramp_up),
                   int_field_spec_to_json(spec.ramp_down),
                   int_field_spec_to_json(spec.hysteresis_mC),
                   int_field_spec_to_json(spec.failsafe_pwm)})},
             {"source_common",
              nlohmann::json::array({source_field_spec_to_json(spec.source_t_start_mC),
                                     source_field_spec_to_json(spec.source_t_full_mC),
                                     source_field_spec_to_json(spec.source_t_crit_mC),
                                     source_field_spec_to_json(spec.source_ttl_sec),
                                     source_field_spec_to_json(spec.source_poll_sec),
                                     source_field_spec_to_json(spec.source_weight)})},
         }},
        {"source",
         {
             {"id_pattern", spec.source_id_pattern},
             {"types", nlohmann::json::array()},
             {"fields",
              {
                  {"common", nlohmann::json::array({"type", "t_start", "t_full", "t_crit", "ttl", "poll", "weight"})},
                  {"sysfs", nlohmann::json::array({"path"})},
                  {"ubus", nlohmann::json::array({"object", "method", "key", "args"})},
              }},
             {"templates", source_templates},
         }},
        {"defaults", board_config_to_json(defaults)},
    };

    for (std::size_t i = 0; i < spec.source_type_count; ++i) {
        root["source"]["types"].push_back(spec.source_types[i]);
    }

    return root.dump();
}

BoardConfig load_board_config(const std::string &path) {
    const ConfigSpec &spec = board_config_spec();
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("cannot open board config: " + path);
    }

    std::map<std::string, std::string> plain;
    std::vector<std::pair<std::string, std::string>> sources;

    std::string line;
    int line_no = 0;
    while (std::getline(in, line)) {
        ++line_no;
        line = strip_inline_comment(line);
        line = trim(line);
        if (line.empty()) {
            continue;
        }

        const std::size_t eq = line.find('=');
        if (eq == std::string::npos) {
            throw std::runtime_error("invalid config line " + std::to_string(line_no) + ": missing '='");
        }

        const std::string key = trim(line.substr(0, eq));
        const std::string value = trim(line.substr(eq + 1));

        if (key.rfind("SOURCE_", 0) == 0 && key.size() > 7) {
            sources.emplace_back(key.substr(7), value);
        } else {
            if (!is_known_top_level_key(key)) {
                throw std::runtime_error("unknown top-level key at line " + std::to_string(line_no) + ": " + key);
            }
            if (plain.find(key) != plain.end()) {
                throw std::runtime_error("duplicate top-level key at line " + std::to_string(line_no) + ": " + key);
            }
            plain[key] = value;
        }
    }

    BoardConfig cfg = default_board_config();
    cfg.sources.clear();

    if (plain.count(spec.interval_sec.key)) {
        cfg.interval_sec = to_int(plain[spec.interval_sec.key], spec.interval_sec.key);
    }
    if (plain.count(spec.control_mode.key)) {
        cfg.control_mode = plain[spec.control_mode.key];
    }
    if (plain.count(spec.pwm_path.key)) {
        cfg.pwm_path = plain[spec.pwm_path.key];
    }
    if (plain.count(spec.pwm_enable_path.key)) {
        cfg.pwm_enable_path = plain[spec.pwm_enable_path.key];
    }
    if (plain.count(spec.control_mode_path.key)) {
        cfg.control_mode_path = plain[spec.control_mode_path.key];
    }
    if (plain.count(spec.pwm_min.key)) {
        cfg.pwm_min = to_int(plain[spec.pwm_min.key], spec.pwm_min.key);
    }
    if (plain.count(spec.pwm_max.key)) {
        cfg.pwm_max = to_int(plain[spec.pwm_max.key], spec.pwm_max.key);
    }
    if (plain.count(spec.ramp_up.key)) {
        cfg.ramp_up = to_int(plain[spec.ramp_up.key], spec.ramp_up.key);
    }
    if (plain.count(spec.ramp_down.key)) {
        cfg.ramp_down = to_int(plain[spec.ramp_down.key], spec.ramp_down.key);
    }
    if (plain.count(spec.hysteresis_mC.key)) {
        cfg.hysteresis_mC = to_int(plain[spec.hysteresis_mC.key], spec.hysteresis_mC.key);
    }
    if (plain.count(spec.failsafe_pwm.key)) {
        cfg.failsafe_pwm = to_int(plain[spec.failsafe_pwm.key], spec.failsafe_pwm.key);
    }

    for (const auto &src_line : sources) {
        cfg.sources.push_back(parse_source_line(src_line.first, src_line.second, cfg.interval_sec));
    }

    validate_board_config(cfg);
    return cfg;
}

} // namespace fancontrol::core
