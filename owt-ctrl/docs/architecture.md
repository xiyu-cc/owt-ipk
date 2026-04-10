# 总体架构（owt-ctrl）

## 决策前提

- 本轮采用 clean switch，不做任何旧版本兼容。
- 详细硬约束见：`clean-switch-constraints.md`。

## 组件

- `owt-net`（Ubuntu）：
  - 提供 HTTP API（控制入口保持不变）。
  - 提供面向前端的状态推送 WebSocket 通道（同源接入）。
  - 维护 Agent 长连接（部署形态为：公网 `WSS` 由 `Nginx:443` 终止后转内网 `WS`）。
  - `server/*` 网络层仅负责会话与传输；控制协议与状态机由 `service/*` 通过接口注入处理。
  - 负责任务调度、命令状态机与审计。
  - 不执行设备动作（不做本地 WOL/SSH/Probe/Params）。

- `owt-agent`（OpenWrt IPK）：
  - 主动连接 `owt-net`。
  - 执行本地命令（WOL/SSH/Probe/Params）。
  - 回传 ACK/RESULT。

## 数据流

1. 浏览器通过 `HTTPS` 调用 `owt-net` 的控制 API。
2. 浏览器通过同源 `WSS` 接收 `owt-net` 主动推送的状态更新。
3. `owt-net` 下发 `COMMAND_PUSH` 到 `owt-agent`。
4. `owt-agent` 执行后回传 `COMMAND_RESULT` / `HEARTBEAT`。
5. `owt-net` 持久化命令与事件并对外查询，同时向前端推送状态快照。

## 文档分层

- 需求：`requirements.md`
- 硬约束：`clean-switch-constraints.md`
- 架构：`vps-agent-architecture.md`
- 协议：`phase0-command-protocol.md`
