#pragma once

#include <cstddef>

namespace fancontrol::core {

struct IntFieldSpec {
    const char *key;
    int default_value;
    int min_value;
    int max_value;
    bool has_max;
    const char *description;
};

struct StringFieldSpec {
    const char *key;
    const char *default_value;
    bool required;
    const char *description;
};

struct EnumFieldSpec {
    const char *key;
    const char *default_value;
    const char *const *allowed_values;
    std::size_t allowed_count;
    const char *description;
};

struct SourceFieldSpec {
    const char *key;
    int default_value;
    int min_value;
    int max_value;
    bool has_max;
    const char *description;
};

struct SourceTemplateSpec {
    const char *id;
    const char *type;
    const char *path;
    const char *object;
    const char *method;
    const char *key;
    const char *args_json;
    int t_start_mC;
    int t_full_mC;
    int t_crit_mC;
    int ttl_sec;
    int poll_sec;
    int weight;
    const char *description;
};

struct ConfigSpec {
    IntFieldSpec interval_sec;
    EnumFieldSpec control_mode;
    StringFieldSpec pwm_path;
    StringFieldSpec pwm_enable_path;
    StringFieldSpec control_mode_path;
    IntFieldSpec pwm_min;
    IntFieldSpec pwm_max;
    IntFieldSpec ramp_up;
    IntFieldSpec ramp_down;
    IntFieldSpec hysteresis_mC;
    IntFieldSpec failsafe_pwm;

    SourceFieldSpec source_t_start_mC;
    SourceFieldSpec source_t_full_mC;
    SourceFieldSpec source_t_crit_mC;
    SourceFieldSpec source_ttl_sec;
    SourceFieldSpec source_poll_sec;
    SourceFieldSpec source_weight;

    const char *source_id_pattern;
    const char *const *source_types;
    std::size_t source_type_count;
    const SourceTemplateSpec *source_templates;
    std::size_t source_template_count;
};

const ConfigSpec &board_config_spec();

} // namespace fancontrol::core
