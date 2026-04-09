# owt-ctrl 云控架构方案（VPS Control Plane + Gateway Agent）

## 1. 背景与目标

当前 `owt-ctrl` 运行在本地网关，以本地 HTTP API 直接执行 WOL/SSH/探测任务。  
目标是迁移为：

- 公网用户访问 VPS 页面完成控制。
- 本地网关仅主动出站连接 VPS，不暴露入站端口。
- 控制面与执行面解耦，支持后续能力扩展与多站点管理。

## 2. 设计原则

- 控制面与执行面分离：VPS 负责“谁能做什么”，Agent 负责“怎么做”。
- 异步任务模型：所有控制动作以任务方式下发与回传。
- 默认安全：鉴权、审计、最小权限、敏感信息最小暴露。
- 可扩展优先：能力按任务类型扩展，不把设备执行细节放入控制面。
- 业务内核单实现：调度/状态机/执行模型只实现一次，传输层用适配器并行扩展。

## 3. 角色与职责

### 3.1 `owt-ctrl`（VPS，控制面）

- 外部访问控制（当前迭代不引入账号体系，采用单管理令牌）。
- 设备与 Agent 注册信息管理。
- 任务编排与路由（创建、排队、下发、重试、超时、取消）。
- 审计日志与操作追踪。
- Web/API 对外提供统一控制入口。

`owt-ctrl` 不负责：

- SSH/WOL 的具体执行逻辑。
- 局域网探测细节与命令拼装细节。

### 3.2 `owt-agent`（网关，执行面）

- 主动连接 `owt-ctrl` 并保持长连接。
- 接收任务并执行本地动作（WOL/SSH/probe/参数管理）。
- 上报执行状态与结果。
- 本地能力声明（capabilities）与版本上报。

`owt-agent` 不负责：

- 用户体系、权限模型、审计策略。
- 跨站点任务编排。

## 4. 总体架构

```text
[Browser]
    |
    | HTTPS
    v
[owt-ctrl / VPS]
    |  (Access Control, API, Scheduler, Audit, Task Store)
    |
    | WSS(JSON, primary) + gRPC(TLS, experimental)
    v
[owt-agent / Gateway]
    |  (Executor: WOL/SSH/Probe/Params)
    v
[LAN Devices]
```

## 5. 通信与任务模型

### 5.0 通道使用范围与并行策略

- `WSS/gRPC` 仅用于 `VPS <-> Agent` 控制通道，不用于浏览器直连 Agent。
- 浏览器仅访问 VPS 的 `HTTPS API`（任务创建、状态查询、审计查询）。
- 当前并行策略：`WSS` 为生产主通道，`gRPC` 为实验并行通道。
- 同一 `command_id` 只允许由一个通道成功提交结果，重复结果按幂等规则丢弃。

### 5.1 连接模型（Agent -> Ctrl）

- `REGISTER`：Agent 上线注册（`agent_id/site_id/version/capabilities`）。
- `HEARTBEAT`：周期心跳，携带负载信息（可选）。
- `COMMAND_PUSH`：Ctrl 下发命令。
- `COMMAND_ACK`：Agent 收到后确认。
- `COMMAND_RESULT`：Agent 回传结果与输出。

### 5.2 任务状态机（控制面统一）

`CREATED -> DISPATCHED -> ACKED -> RUNNING -> SUCCEEDED|FAILED|TIMED_OUT|CANCELLED`

约束：

- 每个任务有全局唯一 `command_id`。
- 支持 `idempotency_key`，避免重复执行。
- 支持 `expire_at/timeout_ms/max_retry`。
- Agent 回传结果必须带 `command_id` 与最终状态。
- 通道元信息必须包含 `channel_type`（`wss` / `grpc`）与 `protocol_version`。

### 5.3 任务类型（首批）

- `WOL_WAKE`
- `HOST_REBOOT`
- `HOST_POWEROFF`
- `HOST_PROBE_GET`
- `MONITORING_SET`
- `PARAMS_GET`
- `PARAMS_SET`

## 6. 安全模型

### 6.1 Agent 鉴权

- 建议首选 mTLS（设备证书）。
- 过渡方案可用短期 token + 自动轮换。
- Agent 仅允许主动出站连接，禁用公网入站控制接口。

### 6.2 用户鉴权

- 当前迭代不引入账号体系，仅单管理令牌保护公网 API。
- 关键动作（重启/关机）仍需记录来源 IP、时间、任务参数摘要与结果。
- 后续如进入多人使用，再升级为账号 + RBAC。

### 6.3 敏感数据

- VPS 不保存明文设备密码。
- 密码类参数仅在 Agent 本地存储（加密落盘）。
- 日志默认脱敏（password/token/private key）。

## 7. 数据模型（最小集合）

当前存储实现确定为 `SQLite`（个人使用，启用 WAL 模式）。

- `agents`：Agent 基础信息、在线状态、最后心跳。
- `sites`：站点与 Agent 归属关系。
- `commands`：任务主表（类型、参数、状态、超时、重试）。
- `command_events`：状态流转事件与时间线。
- `audit_logs`：用户操作审计。

## 8. 与当前代码的映射

- 保留现有执行能力实现：`wol_* / host_control_* / host_probe_* / params_*`。
- 抽出执行层为 Agent 内部服务接口，不直接暴露公网 HTTP。
- 现有前端 API 从“直连本地网关”改为“调用 VPS 控制面 API”。
- 本地 HTTP 运维口当前不纳入方案（默认关闭，不实现）。

## 9. 分阶段实施计划（文档确认后执行）

### Phase 0：冻结协议与边界

- 确认角色边界与非目标项。
- 确认任务协议字段与状态机。
- 确认安全基线与最小权限策略。
- 固化“单内核 + 双传输适配器（WSS/gRPC）”接口边界。

验收：

- 本文档评审通过。
- 形成规范协议：统一字段语义 + JSON 映射 + proto 映射。
- 协议草案文档落地：`docs/phase0-command-protocol.md`。
- proto 草案文件落地：`proto/control_channel.proto`。

### Phase 1：生产主线（WSS）落地

- 在网关侧引入 `agent connector`（WSS 长连接、心跳、收发、断线重连）。
- 将现有 API 处理逻辑下沉为 `executor` 接口。
- 完成任务执行与结果回传闭环。
- VPS 持久层落地 SQLite（含任务与审计表）。

验收：

- 单 Agent 可从 Ctrl 收任务并回传结果（WSS）。
- 断线重连后能继续处理新任务。
- SQLite 持久化可恢复未完成任务状态。

### Phase 2：gRPC 并行通道（实验线）

- 在不改业务内核前提下增加 gRPC transport adapter。
- 复用同一命令模型、状态机、幂等规则与存储结构。
- 增加通道优先级与去重策略（WSS 主，gRPC 备/灰度）。

验收：

- 同一 Agent 可按配置选择 WSS 或 gRPC 上线。
- 双通道并行情况下不会重复执行同一任务。

### Phase 3：VPS 控制面增强

- 完善 Agent 管理、任务下发、状态查询、审计日志。
- 提供最小 Web 控制页（下发命令 + 查看状态）。
- 增加管理令牌轮换与失效机制。

验收：

- 公网页面可对在线 Agent 发起 WOL/reboot/poweroff。
- 全链路可追踪到来源、任务、结果。

### Phase 4：可靠性与安全加固

- 增加重试策略、超时治理、幂等防重。
- 完成证书管理或 token 轮换机制。
- 加入限流、告警、监控指标。

验收：

- 异常场景（Agent 离线、超时、重复提交）行为可预测。
- 安全策略通过自测清单。

## 10. 非目标（当前阶段不做）

- 多云多区域调度。
- 复杂工作流编排引擎。
- 通用插件市场。

## 11. 已确认项（当前迭代）

- Agent 通信：`WSS(JSON)` 与 `gRPC` 并行实现；`WSS` 为生产主通道，`gRPC` 为实验并行通道。
- 通道范围：`WSS/gRPC` 仅用于 `VPS <-> Agent`；浏览器仅访问 VPS `HTTPS API`。
- 存储介质：采用 `SQLite`（个人使用，WAL 模式）。
- 账号体系：当前不考虑；使用单管理令牌保护公网 API。
- 本地运维口：当前不考虑，默认关闭。
