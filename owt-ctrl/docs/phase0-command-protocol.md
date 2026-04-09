# Phase 0 协议草案（统一字段 + JSON/proto 映射）

## 1. 适用范围

- 本文协议仅用于 `VPS (owt-ctrl) <-> Gateway (owt-agent)` 控制通道。
- 浏览器不使用本协议，浏览器只调用 VPS 的 `HTTPS API`。
- 当前并行实现：`WSS(JSON)` 生产主线，`gRPC` 实验并行线。

## 2. 协议版本与兼容策略

- 协议版本：`v1.0-draft`。
- 所有消息必须携带 `protocol_version`。
- 同主版本向前兼容：新增字段必须可选；接收方忽略未知字段。
- 主版本变更时拒绝连接并返回 `UNSUPPORTED_PROTOCOL_VERSION`。

## 3. 统一消息信封（Envelope）

所有消息先包裹统一信封，再携带具体 payload。

必填字段：

- `message_id`：消息唯一 ID（推荐 UUID）。
- `message_type`：消息类型（见第 4 节）。
- `protocol_version`：协议版本。
- `channel_type`：`wss` 或 `grpc`。
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
- `payload_json`：命令参数（JSON 字符串；WSS 原生对象可直接映射）。

## 6. 状态与生命周期

状态机：

`CREATED -> DISPATCHED -> ACKED -> RUNNING -> SUCCEEDED|FAILED|TIMED_OUT|CANCELLED`

约束：

- `COMMAND_ACK` 到达后状态进入 `ACKED`。
- Agent 开始执行时上报 `RUNNING`（可选，但建议支持）。
- `COMMAND_RESULT` 必须上报终态之一：`SUCCEEDED|FAILED|TIMED_OUT|CANCELLED`。
- 同一 `command_id` 只接受第一个终态结果；后续重复结果丢弃并记审计。

## 7. 双通道并行去重规则（WSS + gRPC）

- 去重主键：`command_id`。
- 幂等辅助键：`idempotency_key`。
- Ctrl 侧接受规则：
  - 第一个合法 `COMMAND_ACK` 生效。
  - 第一个合法终态 `COMMAND_RESULT` 生效。
  - 后续重复回包返回 `DUPLICATE_COMMAND_RESULT`，不改变终态。
- 通道优先级：
  - 默认 `wss` 为主通道。
  - `grpc` 仅在灰度或主通道不可用时承载流量。

## 8. 错误码（最小集）

- `BAD_MESSAGE_FORMAT`
- `UNSUPPORTED_PROTOCOL_VERSION`
- `AUTH_FAILED`
- `AGENT_NOT_REGISTERED`
- `COMMAND_EXPIRED`
- `DUPLICATE_COMMAND_RESULT`
- `INTERNAL_ERROR`

## 9. WSS(JSON) 映射示例

### 9.1 COMMAND_PUSH

```json
{
  "message_id": "71f0c8ec-8957-4ad8-8972-4a6c8f84c040",
  "message_type": "COMMAND_PUSH",
  "protocol_version": "v1.0-draft",
  "channel_type": "wss",
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
  "channel_type": "wss",
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
  "channel_type": "wss",
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

## 10. gRPC(proto) 映射

- proto 草案文件：`proto/control_channel.proto`
- gRPC 服务模式：双向流
  - `rpc Connect(stream Envelope) returns (stream Envelope);`

约束：

- proto 字段语义必须与 JSON 字段一一对应。
- 双通道必须共用同一命令模型与状态机，不允许出现“通道特有状态”。

## 11. Phase 0 交付清单

- 本文档：统一协议语义定义（source of truth）。
- `proto/control_channel.proto`：gRPC 草案接口。
- 后续代码实现只允许在此基础上加可选字段，不允许改动既有字段语义。

