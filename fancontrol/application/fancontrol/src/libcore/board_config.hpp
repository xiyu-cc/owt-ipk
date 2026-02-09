#pragma once

#include <string>
#include <vector>

namespace fancontrol::core {

struct BoardSourceConfig {
    std::string id;
    std::string type;

    std::string path;

    std::string object;
    std::string method;
    std::string key;
    std::string args_json;

    int t_start_mC = 60000;
    int t_full_mC = 80000;
    int t_crit_mC = 90000;
    int ttl_sec = 10;
    int poll_sec = 2;
    int weight = 100;
};

struct BoardConfig {
    int interval_sec = 1;

    std::string pwm_path;
    std::string pwm_enable_path;
    std::string thermal_mode_path = "/sys/class/thermal/thermal_zone0/mode";
    int pwm_min = 0;
    int pwm_max = 255;
    bool pwm_inverted = true;
    int pwm_startup_pwm = 128;
    int ramp_up = 25;
    int ramp_down = 8;
    int hysteresis_mC = 2000;
    std::string policy = "max";
    int failsafe_pwm = 64;

    std::string pidfile = "/var/run/fancontrol.pid";

    std::vector<BoardSourceConfig> sources;
};

BoardConfig load_board_config(const std::string &path);

} // namespace fancontrol::core
