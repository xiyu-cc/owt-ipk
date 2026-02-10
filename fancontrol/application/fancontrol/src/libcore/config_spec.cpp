#include "libcore/config_spec.hpp"

#include "libcore/board_config.hpp"

namespace fancontrol::core {

const ConfigSpec &board_config_spec() {
    static constexpr const char *kControlModeValues[] = {"kernel", "user"};
    static constexpr const char *kSourceTypes[] = {"sysfs", "ubus"};

    static constexpr SourceTemplateSpec kSourceTemplates[] = {
        {
            "soc",
            "sysfs",
            "/sys/class/thermal/thermal_zone0/temp",
            "",
            "",
            "",
            "",
            60000,
            82000,
            90000,
            6,
            1,
            100,
            "SoC thermal source",
        },
        {
            "nvme",
            "sysfs",
            "/sys/class/nvme/nvme0/hwmon1/temp1_input",
            "",
            "",
            "",
            "",
            50000,
            70000,
            80000,
            6,
            1,
            120,
            "NVMe thermal source",
        },
        {
            "rm500q-gl",
            "ubus",
            "",
            "qmodem",
            "get_temperature",
            "temp_mC",
            "{\"config_section\":\"2_1\"}",
            58000,
            76000,
            85000,
            20,
            10,
            130,
            "RM500 thermal source via ubus",
        },
    };

    static constexpr ConfigSpec kSpec = {
        {"INTERVAL", 1, 1, 0, false, "Main control loop interval in seconds"},
        {"CONTROL_MODE", "kernel", kControlModeValues, 2, "PWM owner: kernel or fancontrol user-mode"},
        {"PWM_PATH", kDefaultPwmPath, true, "Target PWM sysfs path"},
        {"PWM_ENABLE_PATH", kDefaultPwmEnablePath, false, "PWM enable sysfs path"},
        {"CONTROL_MODE_PATH", kDefaultControlModePath, true, "Control mode sysfs path"},
        {"PWM_MIN", 0, 0, 255, true, "Minimum PWM register value"},
        {"PWM_MAX", 255, 0, 255, true, "Maximum PWM register value"},
        {"RAMP_UP", 5, 1, 0, false, "Seconds from PWM_MIN to PWM_MAX (stronger cooling)"},
        {"RAMP_DOWN", 10, 1, 0, false, "Seconds from PWM_MAX to PWM_MIN (weaker cooling)"},
        {"HYSTERESIS_MC", 2000, 0, 0, false, "Per-source hysteresis in milli-Celsius"},
        {"FAILSAFE_PWM", 64, 0, 255, true, "Failsafe PWM clamp when a source times out"},
        {"t_start", 60000, -273150, 300000, true, "Source demand ramp start temperature (mC)"},
        {"t_full", 80000, -273150, 300000, true, "Source full-demand temperature (mC)"},
        {"t_crit", 90000, -273150, 300000, true, "Source critical temperature (mC)"},
        {"ttl", 10, 1, 0, false, "Source sample TTL in seconds"},
        {"poll", 2, 1, 0, false, "Source polling interval in seconds"},
        {"weight", 100, 1, 200, true, "Source demand weight percentage"},
        kSourceIdPattern,
        kSourceTypes,
        2,
        kSourceTemplates,
        3,
    };

    return kSpec;
}

} // namespace fancontrol::core
