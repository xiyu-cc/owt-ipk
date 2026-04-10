# owt-net 云控架构方案（Ubuntu Control Plane + Gateway Agent）

## 0. 决策状态（本轮）

- 本轮采用 clean switch（破坏式切换）。
- 不考虑任何旧版本兼容，不保留旧 API/旧执行路径。
- 统一约束以 `clean-switch-constraints.md` 为准。

## 1. 背景与目标

当前架构已拆分为 `owt-net`（Ubuntu 控制面）+ `owt-agent`（OpenWrt 执行面）。
目标是：

- 公网用户访问 VPS 页面完成控制。
- 本地网关仅主动出站连接 VPS，不暴露入站端口。
- 控制面与执行面解耦，支持后续能力扩展与多站点管理。

## 2. 设计原则

- 控制面与执行面分离：VPS 负责“谁能做什么”，Agent 负责“怎么做”。
- 异步任务模型：所有控制动作以任务方式下发与回传。
- 默认安全：鉴权、审计、最小权限、敏感信息最小暴露。
- 可扩展优先：能力按任务类型扩展，不把设备执行细节放入控制面。
- 业务内核单实现：调度/状态机/执行模型只实现一次，传输层只保留 WebSocket（公网 `WSS`，内网 `WS`）。

## 3. 角色与职责

### 3.1 `owt-net`（Ubuntu，控制面）

- 外部访问控制（当前迭代不引入账号体系，采用反向代理边缘认证）。
- 设备与 Agent 注册信息管理。
- 任务编排与路由（创建、排队、下发、重试、超时、取消）。
- 审计日志与操作追踪。
- Web/API 对外提供统一控制入口。

`owt-net` 不负责：

- SSH/WOL 的具体执行逻辑。
- 局域网探测细节与命令拼装细节。

### 3.2 `owt-agent`（网关，执行面）

- 主动连接 `owt-net` 并保持长连接。
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
[Nginx :443 + SNI + TLS Termination]
    |
    | WS (upstream)
    v
[owt-net / Ubuntu]
    |  (Access Control, API, Scheduler, Audit, Task Store)
    |
    | WebSocket(JSON)
    v
[owt-agent / Gateway]
    |  (Executor: WOL/SSH/Probe/Params)
    v
[LAN Devices]
```

## 5. 通信与任务模型

### 5.0 通道使用范围

- 控制通道仅用于 `VPS <-> Agent`，不用于浏览器直连 Agent。
- 公网段（`owt-agent -> Nginx`）必须使用 `WSS`。
- `Nginx` 完成 TLS 终止后，向 `owt-net` 上游转发 `WS`。
- `owt-net` 不承担 TLS 终止，也不要求兼容 `WSS` 服务端。
- 浏览器访问 VPS 的 `HTTPS API`（任务创建、状态查询、审计查询），并可建立同源 `WSS` 状态订阅通道接收主动推送。
- 同一 `command_id` 只允许第一个终态结果生效，重复结果按幂等规则丢弃。

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
- 协议元信息必须包含 `protocol_version`；`trace_id` 可选但建议透传。

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

- 控制通道不使用 token 鉴权，入口安全由反向代理边缘认证与网络策略保障。
- Agent 仅允许主动出站连接，禁用公网入站控制接口。

### 6.2 用户鉴权

- 当前迭代不引入账号体系，公网访问统一依赖边缘无感鉴权（如反向代理侧身份认证）。
- 关键动作（重启/关机）需记录来源 IP、时间、任务参数摘要与结果。

### 6.3 敏感数据

- VPS 不保存明文设备密码。
- 密码类参数仅在 Agent 进程内运行态保存，不做长期落盘写回。
- 日志默认脱敏（password/token/private key）。

## 7. 数据模型（最小集合）

当前存储实现确定为 `SQLite`（个人使用，启用 WAL 模式）。

- `agents`：Agent 基础信息、在线状态、最后心跳。
- `sites`：站点与 Agent 归属关系。
- `commands`：任务主表（类型、参数、状态、超时、重试）。
- `command_events`：状态流转事件与时间线。
- `audit_logs`：用户操作审计。

## 8. 与当前代码的映射

- `owt-net` 仅保留控制面能力（会话、调度、状态机、审计、查询）。
- `owt-net` 删除本地执行实现与对应旧路由（`/api/v1/wol/*`、`/api/v1/host/*`、`/api/v1/monitoring/*`、`/api/v1/params/*`）。
- `owt-agent` 保留并承接全部执行能力（WOL/SSH/Probe/Params）。
- Web 前端保留 `control/command/*` 异步命令模型（HTTP API 不变），并新增状态 `WSS` 主动推送接收。
- 前端默认页移除任务/审计栏，状态区保留“状态监控开关”（`MONITORING_SET`）。

## 9. 实施策略（Clean Switch）

- 直接切换到新边界，不做过渡双栈。
- 代码与依赖按新边界一次性收敛：
  - `owt-net` 去除本地执行模块与相关依赖（例如 `libssh2`）。
  - `owt-agent` 保持执行依赖与能力完整。
- 所有对外控制动作都通过 `POST /api/v1/control/command/push` 进入状态机。

## 10. 已确认项（当前迭代）

- 通道：公网使用 `WSS(JSON)`，`Nginx -> owt-net` 上游使用 `WS(JSON)`。
- 443 与 TLS：公网 `443` 由 `Nginx` 接管并按 `SNI` 分流，TLS 仅在 `Nginx` 终止。
- 存储介质：采用 `SQLite`（个人使用，WAL 模式）。
- 账号体系：当前不考虑；公网鉴权由反向代理边缘认证负责。
- 本轮为 clean switch：不做旧版本兼容，不保留旧 API。
