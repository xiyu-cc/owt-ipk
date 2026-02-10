# Fancontrol Architecture for BPI R3 Mini (OpenWrt 24.10.5)

This project is intended for a single-board deployment only (BPI R3 Mini), not for generic distribution.

## Design goals

- Single writer for PWM output (`fancontrol` only)
- Multi-source thermal input: SoC + NVMe + RM500Q-GL
- Deterministic fail-safe behavior
- Remove legacy `/etc/fancontrol` mode and keep only board profile mode

## Layered layout

- `src/libcore/`: control loop, source adapters, and board profile parsing
- `src/app/main.cpp`: process entry and wiring

## Core components (planned)

- `ITempSource`
  - Uniform sampling interface for all thermal sources.
  - Output: `ok`, `temp_mC`, `sample_ts`.
- `SourceManager`
  - Poll sources, apply TTL timeout, keep last-good samples.
- `DemandPolicy`
  - Convert each source temperature to PWM demand and arbitrate final target.
- Source resource ownership
  - One thermal resource can only be bound by one source definition.
  - Duplicate bindings (same sysfs path or same ubus object/method/key/args) are rejected at config-parse time.
- `SafetyGuard`
  - Enforce critical-temp and source-loss fail-safe rules.
- `PwmController`
  - Write `pwmX_enable` and `pwmX`, apply ramp-up/ramp-down limiting.
- `FanControlService`
  - Main loop scheduler: sample -> demand -> safety -> output.

## Thermal control model for this board

Use max-demand arbitration on cooling demand:

`target_pwm = max(d_soc, d_nvme, d_rm500q_gl)`

Where each demand is linearly interpolated between `T_START` and `T_FULL`.
Cooling strength is interpreted along `PWM_MIN -> PWM_MAX`, so either numeric direction is valid (including `PWM_MIN > PWM_MAX`).

Safety rules:

- Any source `>= T_CRIT`: force full speed (`PWM_MAX`)
- Source timeout beyond TTL: clamp to `FAILSAFE_PWM`
- All sources invalid: force full speed

## Suggested board profile defaults

- Loop interval: 1s
- RM500 poll interval: 8-10s
- Hysteresis: 2C (2000 mC)
- Ramp-up: 5s (PWM_MIN -> PWM_MAX)
- Ramp-down: 10s (PWM_MAX -> PWM_MIN)
- Fail-safe PWM: around `64` (board-dependent)

## Example board config model

```ini
INTERVAL=1
PWM_PATH=/sys/class/hwmon/hwmon2/pwm1
PWM_ENABLE_PATH=/sys/class/hwmon/hwmon2/pwm1_enable
CONTROL_MODE_PATH=/sys/class/thermal/thermal_zone0/mode
PWM_MIN=0
PWM_MAX=255
RAMP_UP=5
RAMP_DOWN=10
HYSTERESIS_MC=2000
FAILSAFE_PWM=64

SOURCE_soc=type=sysfs,path=/sys/class/thermal/thermal_zone0/temp,t_start=60000,t_full=82000,t_crit=90000,ttl=6
SOURCE_nvme=type=sysfs,path=/sys/class/hwmon/hwmon3/temp1_input,t_start=50000,t_full=70000,t_crit=80000,ttl=6
SOURCE_rm500q-gl=type=ubus,object=qmodem,method=get_temperature,key=temp_mC,args={"config_section":"modem1"},t_start=58000,t_full=76000,t_crit=85000,ttl=20
```

## Migration stages

1. Done: split monolith into `libcore` + `app` without behavior changes.
2. Done: introduced `SourceManager` and `ITempSource`.
3. Done: added sysfs + ubus source adapters for SoC/NVMe/RM500 classes of inputs.
4. Done: integrated max-demand arbitration with timeout and critical-temperature fail-safe.
5. Done: LuCI page can edit and write board profile (`/etc/fancontrol.conf`).
6. Next: add source health/status telemetry in LuCI (last temp, stale flag, poll age).
7. Done: runtime telemetry exported to `/var/run/fancontrol.status.json` and exposed by LuCI RPC `runtimeStatus`.
