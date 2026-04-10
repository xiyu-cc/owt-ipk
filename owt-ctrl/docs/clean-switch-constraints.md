# Clean Switch 硬约束（owt-ctrl）

本文档是 `owt-ctrl` 本轮改造的唯一约束源（source of truth）。

生效策略：

- 本工程为个人工程，允许破坏式变更。
- 不做旧版本兼容，不保留旧接口兜底，不做灰度双栈。
- 文档约束优先于历史实现；如代码与本文冲突，以本文为准并立即修改代码。

## 0. 约束盘点（现有 + 需要）

现有已确认约束（来自既有文档与代码方向）：

- 工程拆分为 `owt-net` 控制面 + `owt-agent` 执行面。
- `owt-agent` 主动连接 `owt-net`，控制通道使用 `WebSocket(JSON)`。
- 控制命令模型已统一到 `COMMAND_PUSH/ACK/RESULT` 与状态机。
- 命令类型集合已收敛为 `WOL/HOST_CONTROL/PROBE/MONITORING/PARAMS`。

本轮新增且必须落地的约束（clean switch）：

- `owt-net` 禁止本地执行设备动作；执行能力必须全部下沉到 `owt-agent`。
- 历史控制 API（`/api/v1/wol/*`、`/api/v1/host/*`、`/api/v1/monitoring/*`、`/api/v1/params/*`）直接移除。
- 控制入口统一为 `POST /api/v1/control/command/push` + `GET /api/v1/control/*` 查询接口。
- `owt-net` 去除执行依赖（如 `libssh2`）；`owt-agent` 保持执行依赖完整。
- 不做旧版本兼容、不过渡、不双栈。

## 1. 网络与连接约束

- `owt-net` 部署在 VPS，公网 `443` 端口由 `Nginx` 统一接管。
- `Nginx` 必须基于 `SNI` 分流到 `owt-net` 对应上游。
- TLS 终止点固定在 `Nginx`；`owt-net` 不承担 TLS 终止职责。
- `owt-agent` 部署在内网网关侧，不假设拥有公网 IP。
- `owt-agent` 仅主动出站连接 `owt-net`，不要求也不允许 `owt-net` 主动入站访问 `owt-agent`。
- 控制通道连接方向固定为 `owt-agent -> owt-net`。
- 公网链路（`owt-agent -> Nginx`）必须使用 `WSS`，禁止公网明文 `WS`。
- `owt-net` 仅接收 `Nginx` 转发的内网 `WS`，不要求也不保留 `WSS` 服务端兼容。
- 浏览器访问 `owt-net` 的 HTTP API，并可订阅同源状态 `WSS`；浏览器不直连 `owt-agent`。

## 2. 角色边界硬约束

`owt-net`（控制面）只负责：

- HTTP API、鉴权、审计。
- Agent 会话管理。
- 命令创建、下发、状态机、查询。

`owt-net` 明确禁止：

- 执行任何设备侧动作（WOL/SSH/Probe/Params）。
- 持有设备执行实现（如本地 SSH 执行器、本地 WOL 发送器、本地探测线程、本地参数存储）。

`owt-agent`（执行面）只负责：

- 接收 `COMMAND_PUSH` 并执行 `WOL_WAKE/HOST_REBOOT/HOST_POWEROFF/HOST_PROBE_GET/MONITORING_SET/PARAMS_GET/PARAMS_SET`。
- 上报 `COMMAND_ACK/COMMAND_RESULT`。
- 使用 `owt-net` 下发的执行参数并仅在内存中维护，不写本地参数文件。

## 3. API 边界硬约束

统一控制入口：

- `POST /api/v1/control/command/push`

统一查询入口：

- `GET /api/v1/control/command/get`
- `GET /api/v1/control/commands/get`
- `GET /api/v1/control/agent/get`
- `GET /api/v1/control/agents/get`
- `GET /api/v1/control/audit/get`

HTTP 边界补充：

- 现有 `control/*` HTTP API 路径与语义保持不变。
- 状态主动推送通过 `WSS` 增量补充，不新增替代性 HTTP 控制入口。

本轮 clean switch 直接移除的旧控制接口（不兼容）：

- `POST /api/v1/wol/wake`
- `POST /api/v1/host/reboot`
- `POST /api/v1/host/poweroff`
- `GET /api/v1/host/probe`
- `GET /api/v1/monitoring/get`
- `POST /api/v1/monitoring/set`
- `GET /api/v1/params/get`
- `POST /api/v1/params/set`

## 4. 协议与状态机约束

- 协议仅使用 `phase0-command-protocol.md` 定义的 envelope + payload 模型。
- 命令状态机固定：`CREATED -> DISPATCHED -> ACKED -> RUNNING -> SUCCEEDED|FAILED|TIMED_OUT|CANCELLED`。
- 同一 `command_id` 仅接受首个终态，重复结果必须丢弃并记审计。
- 控制通道元信息必须包含：`protocol_version`。
- `trace_id` 为可选但强烈建议透传的链路追踪字段（用于审计与排障）。

兼容策略（本轮）：

- 不考虑与历史实现兼容。
- 对历史 API、历史执行路径、历史依赖可直接删除。
- `protocol_version` 仅接受当前约定版本（`v1.0-draft`），不做旧版本兼容。

## 5. 安全与数据约束

- `owt-net` 不允许存储任何 token，不采用 token 作为核心鉴权方式。
- 个人使用场景采用“边缘无感鉴权（反向代理身份认证）”作为主防护层。
- 设备侧敏感参数（如 SSH 密码）仅允许在 `owt-agent` 进程内运行态持有。
- `owt-net` 不得以明文长期存储设备密码。
- 审计日志必须记录关键动作、操作者标识、时间、任务标识。

## 6. 构建与依赖约束

- `owt-net` 不再依赖 `libssh2`（因为不再本地执行 SSH）。
- `owt-net` 不承担 TLS 终止，不要求也不保留 `WSS` 服务端兼容路径（证书加载/`wss://` 监听）。
- `owt-agent` 必须具备 `libssh2` 与 TLS/WSS 能力（OpenSSL）。
- gRPC 已移除，禁止重新引入。

## 7. 代码落地约束（执行清单）

`owt-net` 侧必须完成：

- 删除本地执行实现与对应路由注册。
- 删除本地执行相关依赖与编译项。
- 前端与后端统一走 `control/command/*` 异步命令模型。
- 新增前端状态推送 `WSS` 通道（用于主动上报在线/探测状态），且不改现有 HTTP API。
- 前端默认页移除“任务与审计”栏位。
- 状态区操作从“刷新探测”切换为“状态监控开关（MONITORING_SET）”。

`owt-agent` 侧必须完成：

- 保持执行能力完整（WOL/SSH/Probe/Params）。
- 保持对 `owt-net` 的主动长连接、断线重连、命令回报。
- 移除命令数据库落盘，不再使用本地 sqlite 命令库。
- 参数持久化统一在 `owt-net`，`owt-agent` 连接建立与参数更新后由 `owt-net` 主动下发。
- `owt-agent` 不写任何业务文件（参数文件、日志文件等）。

## 8. 文档一致性约束

- `docs/requirements.md`、`docs/architecture.md`、`docs/vps-agent-architecture.md`、`docs/phase0-command-protocol.md` 必须与本文一致。
- 子工程 README 只能补充实现细节，不得与本文冲突。

## 9. server 分层硬约束（新增）

- `include/server/http_session.h` 与 `include/server/websocket_session.h` 仅允许承担传输层职责：连接建立、读写循环、超时、关闭、背压与基础错误处理。
- `websocket_session` 必须与其首次引入版本的职责边界一致：禁止包含协议编解码、命令状态机、持久化、会话注册、审计等业务逻辑。
- WebSocket 业务处理必须通过接口注入（observer/handler factory）实现，`server` 层只依赖抽象接口，不得直接依赖 `service` 业务实现。
- 业务层（`service/*`）负责控制协议解析、鉴权后业务处理、命令状态迁移、存储与审计；网络层（`server/*`）不得直接访问这些能力。
- 新增 WebSocket 能力时，先扩展接口与业务 handler，再接入 `main` 注册；禁止在 `websocket_session` 内直接添加 `switch(message.type)` 之类业务分发。
