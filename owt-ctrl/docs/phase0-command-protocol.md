# Phase 0 协议草案（统一字段 + JSON 映射）

## 1. 适用范围

- 本文协议仅用于 `VPS (owt-net) <-> Gateway (owt-agent)` 控制通道。
- 浏览器不使用本协议；浏览器调用 VPS 的 `HTTPS API`，并可通过独立状态 `WSS` 通道接收推送。
- 控制通道使用 `WS/WSS(JSON)`。
- 本轮为 clean switch：不对历史实现提供兼容层。

### 1.1 部署剖面（生产固定形态）

- `owt-net` 部署在 VPS，公网 `443` 由 `Nginx` 接管并按 `SNI` 分流。
- TLS 在 `Nginx` 终止，`owt-net` 不承担 TLS 握手与证书管理。
- 公网链路 `owt-agent -> Nginx` 必须使用 `WSS`（禁止公网明文 `WS`）。
- `Nginx -> owt-net` 上游使用内网 `WS`。
- 协议层不再携带链路类型字段；链路类型由部署与接入层约束保证。

## 2. 协议版本与兼容策略

- 协议版本：`v1.0-draft`。
- 所有消息必须携带 `protocol_version`。
- 当前实现仅接受 `v1.0-draft`。
- 非当前协议版本直接拒绝并返回 `UNSUPPORTED_PROTOCOL_VERSION`。
- 不考虑旧协议兼容与灰度双栈。

## 3. 统一消息信封（Envelope）

所有消息先包裹统一信封，再携带具体 payload。

必填字段：

- `message_id`：消息唯一 ID（推荐 UUID）。
- `message_type`：消息类型（见第 4 节）。
- `protocol_version`：协议版本。
- `sent_at_ms`：发送时间戳（Unix ms）。
- `trace_id`：链路追踪 ID（可选但建议全链路透传）。
- `agent_id`：Agent 标识；`REGISTER` 前可为空，之后必须存在。

## 4. 消息类型

- `REGISTER`：Agent 注册上线。
- `REGISTER_ACK`：Ctrl 注册确认。
- `HEARTBEAT`：Agent 心跳。
- `HEARTBEAT_ACK`：Ctrl 心跳确认。
- `COMMAND_PUSH`：Ctrl 下发任务。
- `COMMAND_ACK`：Agent 收到任务确认。
- `COMMAND_RESULT`：Agent 执行结果上报。
- `ERROR`：协议级错误。

## 5. 统一命令模型（Command）

必填字段：

- `command_id`：全局唯一任务 ID。
- `idempotency_key`：幂等键（重复下发/重连去重）。
- `command_type`：
  - `WOL_WAKE`
  - `HOST_REBOOT`
  - `HOST_POWEROFF`
  - `HOST_PROBE_GET`
  - `MONITORING_SET`
  - `PARAMS_GET`
  - `PARAMS_SET`
- `issued_at_ms`：任务创建时间。
- `expires_at_ms`：过期时间，过期不得执行。
- `timeout_ms`：单次执行超时。
- `max_retry`：控制面最大重试次数。
- `payload_json`：命令参数（JSON 字符串；WebSocket 原生对象可直接映射）。

## 6. 状态与生命周期

状态机：

`CREATED -> DISPATCHED -> ACKED -> RUNNING -> SUCCEEDED|FAILED|TIMED_OUT|CANCELLED`

约束：

- `COMMAND_ACK` 到达后状态进入 `ACKED`。
- Agent 开始执行时上报 `RUNNING`（可选，但建议支持）。
- `COMMAND_RESULT` 必须上报终态之一：`SUCCEEDED|FAILED|TIMED_OUT|CANCELLED`。
- 同一 `command_id` 只接受第一个终态结果；后续重复结果丢弃并记审计。

## 7. 去重规则

- 去重主键：`command_id`。
- 幂等辅助键：`idempotency_key`。
- Ctrl 侧接受规则：
  - 第一个合法 `COMMAND_ACK` 生效。
  - 第一个合法终态 `COMMAND_RESULT` 生效。
  - 后续重复回包返回 `DUPLICATE_COMMAND_RESULT`，不改变终态。

## 8. 错误码（最小集）

- `BAD_MESSAGE_FORMAT`
- `UNSUPPORTED_PROTOCOL_VERSION`
- `AGENT_NOT_REGISTERED`
- `COMMAND_EXPIRED`
- `DUPLICATE_COMMAND_RESULT`
- `INTERNAL_ERROR`

## 9. JSON 映射示例

### 9.1 COMMAND_PUSH

```json
{
  "message_id": "71f0c8ec-8957-4ad8-8972-4a6c8f84c040",
  "message_type": "COMMAND_PUSH",
  "protocol_version": "v1.0-draft",
  "sent_at_ms": 1775664000000,
  "trace_id": "trc-20260408-0001",
  "agent_id": "agent-sh-01",
  "payload": {
    "command": {
      "command_id": "cmd-20260408-0001",
      "idempotency_key": "idem-20260408-0001",
      "command_type": "HOST_REBOOT",
      "issued_at_ms": 1775664000000,
      "expires_at_ms": 1775664060000,
      "timeout_ms": 5000,
      "max_retry": 1,
      "payload_json": "{\"host\":\"192.168.1.10\",\"port\":22,\"user\":\"root\"}"
    }
  }
}
```

### 9.2 COMMAND_ACK

```json
{
  "message_id": "ad0068cb-e66b-4ed2-a77d-7c709c4a75f2",
  "message_type": "COMMAND_ACK",
  "protocol_version": "v1.0-draft",
  "sent_at_ms": 1775664000123,
  "trace_id": "trc-20260408-0001",
  "agent_id": "agent-sh-01",
  "payload": {
    "command_id": "cmd-20260408-0001",
    "status": "ACKED",
    "message": "accepted"
  }
}
```

### 9.3 COMMAND_RESULT

```json
{
  "message_id": "0a7b10bf-19cf-4e9f-a8e3-8b62e8e2e2bf",
  "message_type": "COMMAND_RESULT",
  "protocol_version": "v1.0-draft",
  "sent_at_ms": 1775664002650,
  "trace_id": "trc-20260408-0001",
  "agent_id": "agent-sh-01",
  "payload": {
    "command_id": "cmd-20260408-0001",
    "final_status": "SUCCEEDED",
    "exit_code": 0,
    "result_json": "{\"output\":\"ok\"}"
  }
}
```

## 10. Phase 0 交付清单

- 本文档：统一协议语义定义（source of truth）。
- 后续代码实现只允许在此基础上加可选字段，不允许改动既有字段语义。
- 历史执行路径（`owt-net` 本地执行 WOL/SSH/Probe/Params）不在协议支持范围内。
