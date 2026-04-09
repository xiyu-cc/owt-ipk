# 总体架构（owt-ctrl）

## 组件

- `owt-net`（Ubuntu）：
  - 提供 HTTP API。
  - 维护 Agent 长连接（WSS/gRPC）。
  - 负责任务调度、命令状态机与审计。

- `owt-agent`（OpenWrt IPK）：
  - 主动连接 `owt-net`。
  - 执行本地命令（WOL/SSH/Probe/Params）。
  - 回传 ACK/RESULT。

## 数据流

1. 浏览器访问 `owt-net`。
2. `owt-net` 下发 `COMMAND_PUSH` 到 `owt-agent`。
3. `owt-agent` 执行后回传 `COMMAND_RESULT`。
4. `owt-net` 持久化命令与事件并对外查询。

## 文档分层

- 需求：`requirements.md`
- 架构：`vps-agent-architecture.md`
- 协议：`phase0-command-protocol.md`
