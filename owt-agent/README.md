# owt-agent

`owt-agent` 是运行在网关侧（OpenWrt）的执行面进程，职责：

- 主动连接 `owt-ctrl`（WSS / gRPC）
- 接收命令并执行本地动作（WOL / SSH / Probe / Params）
- 回传 ACK 与执行结果，并落地本地命令审计库

## 目录结构

- `include/control/*`：控制通道抽象、协议模型、编解码
- `include/service/*`：执行能力与本地存储接口
- `src/core/*`：agent 主程序、配置、日志、执行逻辑
- `proto/*`：控制通道 proto
- `docs/*`：agent 侧设计文档

## 配置

默认配置文件：`config.ini`

```ini
[agent]
agent_id = agent-local
protocol_version = v1.0-draft
management_token =
enable_wss = true
wss_endpoint = wss://127.0.0.1:9527/ws/control
enable_grpc = true
grpc_endpoint = 127.0.0.1:50051
primary_channel = wss
```

## 构建

```bash
cmake --preset gcc-debug
cmake --build --preset gcc-debug
```

生成：`build/gcc-debug/owt-agent`

说明：默认会优先使用 `owt-agent/third_party`；不存在时自动回退到 `../owt-ctrl/third_party`。

## 运行

```bash
./build/gcc-debug/owt-agent ./config.ini
```

## OpenWrt 打包目录

- `application/owt-agent/`

默认部署文件：

- `/usr/sbin/owt-agent`
- `/etc/init.d/owt-agent`
- `/etc/config/owt-agent`
- `/etc/owt-agent/config.ini`
- `/etc/owt-agent/params.ini`
