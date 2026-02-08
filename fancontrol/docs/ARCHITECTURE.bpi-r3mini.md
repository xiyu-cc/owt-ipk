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
- `SafetyGuard`
  - Enforce critical-temp and source-loss fail-safe rules.
- `PwmController`
  - Write `pwmX_enable` and `pwmX`, apply ramp-up/ramp-down limiting.
- `FanControlService`
  - Main loop scheduler: sample -> policy -> safety -> output.

## Thermal policy for this board

Use max-demand arbitration on cooling demand:

`target_pwm = max(d_soc, d_nvme, d_rm500)`

Where each demand is linearly interpolated between `T_START` and `T_FULL`.
For BPI R3 Mini, `PWM_INVERTED=1` means lower PWM value => stronger cooling (`255` is minimum speed, `0` is maximum speed).

Safety rules:

- Any source `>= T_CRIT`: force full speed (`0` when `PWM_INVERTED=1`)
- Source timeout beyond TTL: clamp to `FAILSAFE_PWM`
- All sources invalid: force full speed

## Suggested board profile defaults

- Loop interval: 2s
- RM500 poll interval: 8-10s
- Hysteresis: 2C (2000 mC)
- Ramp-up: +25 PWM/s
- Ramp-down: -8 PWM/s
- PWM direction: inverted (`PWM_INVERTED=1`)
- Startup threshold: `PWM_STARTUP_PWM=128`
- Fail-safe PWM: around `64` (for inverted PWM)

## Example board config model

```ini
INTERVAL=2
PWM_PATH=/sys/class/hwmon/hwmon2/pwm1
PWM_ENABLE_PATH=/sys/class/hwmon/hwmon2/pwm1_enable
THERMAL_MODE_PATH=/sys/class/thermal/thermal_zone0/mode
PWM_MIN=0
PWM_MAX=255
PWM_INVERTED=1
PWM_STARTUP_PWM=128
RAMP_UP=25
RAMP_DOWN=8
HYSTERESIS_MC=2000
POLICY=max
FAILSAFE_PWM=64

SOURCE_soc=type=sysfs,path=/sys/class/thermal/thermal_zone0/temp,t_start=60000,t_full=82000,t_crit=90000,ttl=6
SOURCE_nvme=type=sysfs,path=/sys/class/hwmon/hwmon3/temp1_input,t_start=50000,t_full=70000,t_crit=80000,ttl=6
SOURCE_rm500=type=ubus,object=qmodem,method=get_temperature,key=temp_mC,args={"config_section":"modem1"},t_start=58000,t_full=76000,t_crit=85000,ttl=20
```

## Migration stages

1. Done: split monolith into `libcore` + `app` without behavior changes.
2. Done: introduced `SourceManager` and `ITempSource`.
3. Done: added sysfs + ubus source adapters for SoC/NVMe/RM500 classes of inputs.
4. Done: integrated `max` demand policy with timeout and critical-temperature fail-safe.
5. Done: LuCI page can edit and write board profile (`/etc/fancontrol.r3mini`).
6. Next: add source health/status telemetry in LuCI (last temp, stale flag, poll age).
