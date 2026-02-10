#pragma once

#include <string>
#include <vector>

namespace fancontrol::core {

inline constexpr const char *kFixedPidfilePath = "/var/run/fancontrol.pid";
inline constexpr const char *kRuntimeStatusPath = "/var/run/fancontrol.status.json";
inline constexpr const char *kDefaultConfigPath = "/etc/fancontrol.conf";
inline constexpr const char *kDefaultControlModePath = "/sys/class/thermal/thermal_zone0/mode";
inline constexpr const char *kDefaultPwmPath = "/sys/class/hwmon/hwmon2/pwm1";
inline constexpr const char *kDefaultPwmEnablePath = "/sys/class/hwmon/hwmon2/pwm1_enable";
inline constexpr const char *kSourceIdPattern = "^[A-Za-z0-9_-]+$";

struct BoardSourceConfig {
    std::string id;
    std::string type;

    std::string path;

    std::string object;
    std::string method;
    std::string key;
    std::string args_json;

    int t_start_mC = 0;
    int t_full_mC = 0;
    int t_crit_mC = 0;
    int ttl_sec = 0;
    int poll_sec = 0;
    int weight = 0;
};

struct BoardConfig {
    int interval_sec = 0;
    std::string control_mode;

    std::string pwm_path;
    std::string pwm_enable_path;
    std::string control_mode_path;
    int pwm_min = 0;
    int pwm_max = 0;
    int ramp_up = 0;
    int ramp_down = 0;
    int hysteresis_mC = 0;
    int failsafe_pwm = 0;

    std::vector<BoardSourceConfig> sources;
};

BoardConfig default_board_config();
void validate_board_config(BoardConfig &cfg);
std::string render_board_config_text(const BoardConfig &cfg);
std::string dump_board_schema_json();
BoardConfig load_board_config(const std::string &path);

} // namespace fancontrol::core
