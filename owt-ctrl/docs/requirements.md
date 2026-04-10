# 需求文档

## 本轮决策状态（必须遵守）

- 本轮采用 clean switch（破坏式切换）。
- 不考虑任何旧版本兼容，不保留旧接口。
- 本工程不考虑任何旧接口、旧字段、旧方法兼容；凡历史兼容性目标均视为非目标。
- 已完成并持续执行对兼容性目标的排查与移除；后续需求与实现均不得以“兼容旧实现”为约束。
- 统一硬约束见：`clean-switch-constraints.md`。

## 项目拆分要求

- 统一顶层工程名为 `owt-ctrl`。
- `owt-ctrl` 下包含两个子工程：`owt-agent` 与 `owt-net`。
- `owt-agent` 负责网关执行面，交付物为 OpenWrt 安装包 `owt-agent.ipk`。
- `owt-net` 负责公网控制面，交付物为 Ubuntu 安装包 `owt-net.deb`（安装后以 systemd 服务运行）。

## 已确认关键句

`owt-agent 以 owt-agent.ipk 交付，owt-net 以 owt-net.deb 交付（安装后作为 Ubuntu systemd 服务运行）。`

`owt-net 只做控制面，owt-agent 才执行具体指令；owt-agent 仅主动连接 owt-net，不要求公网入站访问。`

`owt-net 部署在 VPS，公网 443 统一由 Nginx 接管并按 SNI 分流；TLS 在 Nginx 终止。owt-net 不使用也不需兼容 WSS，仅接收 Nginx 转发的内网 WS。owt-agent 经公网主动连接时必须使用 WSS。`

## 交付物要求

- `owt-agent.ipk`：用于 OpenWrt 网关侧安装，提供执行面能力（WOL/SSH/Probe/Params）。
- `owt-net.deb`：用于 Ubuntu 控制面安装，提供控制通道、任务调度、审计与 Web/API 服务。

## 部署与链路安全要求

- `owt-net` 前置 `Nginx`，公网入口固定为 `443`，由 `Nginx` 基于 `SNI` 分流到 `owt-net` 上游。
- TLS 终止点固定在 `Nginx`；`owt-net` 不负责证书管理与 TLS 握手。
- `owt-net` 控制通道仅需提供内网 `WS` 上游，不要求也不保留 `WSS` 服务端兼容。
- `owt-agent` 到 `owt-net` 的公网控制链路必须使用 `WSS`（禁止公网明文 `WS`）。

## 文档组织要求

- 主文档集中在 `owt-ctrl/docs/`。
- 子工程文档仅保留实现细节与构建说明，并回链主文档。
- 文档冲突时，以 `clean-switch-constraints.md` 为准。
- 本轮变更落盘记录见：`change-log-2026-04-10.md`。

## 新增需求（2026-04-10）

- 个人使用场景下，鉴权需更无感，同时保证公网暴露时的安全性。
- `owt-net` 需要支持通过 `WebSocket` 向前端主动推送状态。
- `owt-net` 现有 `HTTP API` 语义与路径保持不变（不新增替代性 HTTP 入口，不破坏现有控制入口）。
- 前端默认页移除“任务与审计”栏位。
- 状态区交互从“刷新探测”改回“状态监控开关”（对应 `MONITORING_SET`）。
- `owt-agent` 不允许长期写盘：移除命令数据库落盘，运行时不再使用本地数据库。
- 控制参数由 `owt-net` 持久化保存，并在 `owt-agent` 注册连接与前端更新参数时主动下发。
- `owt-agent` 运行期仅在内存中持有参数，不写入任何本地参数文件。
- `owt-agent` 不写任何业务文件（含参数与日志文件）；如需访问记录与关键状态持久化，统一保存在 `owt-net`。
- `owt-net` 不允许存储任何 token，也不采用 token 作为核心鉴权方式。
