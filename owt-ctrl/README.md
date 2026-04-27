# owt-ctrl

`owt-ctrl` 是远程控制系统工程，包含控制服务、网关执行代理和协议定义，用于实现公网入口下的内网设备控制能力。

## 项目需求

### 1. 项目目的

- 为个人站点提供“远程控制内网设备”能力，支持开机、重启、关机与运行参数管理。
- 面向两类使用者：
  - 站点操作者：通过浏览器进行控制与状态查看。
  - 网关设备维护者：在网关侧部署并维护执行代理。
- 在不暴露内网设备入站端口的前提下完成远程控制，并提供可追溯的命令与操作记录。

### 2. 范围

#### 2.1 包含范围

- 远程控制：开机、重启、关机、状态采集开关、参数读取、参数保存。
- 设备在线状态与采集状态的实时推送。
- 命令全生命周期查询（提交、状态变化、结果查询）。
- 按设备维度的控制参数持久化。
- 访问控制与操作审计。
- 网关设备与控制服务的长连接控制通道。

#### 2.2 不包含范围

- 局域网设备的入站端口暴露与直连方案。
- 多租户账号体系与复杂 RBAC 权限模型。
- 浏览器直接连接网关设备。
- 控制服务本机执行设备动作（设备动作仅由网关执行）。
- 旧版本协议/旧行为兼容与双栈灰度（当前阶段）。

### 3. 核心对象

- 控制服务：负责命令编排、会话管理、状态机、查询、审计、参数持久化。
- 网关执行代理：负责设备动作执行、命令回执、状态上报。
- 目标设备：被控制的内网主机或网络设备。
- 命令：可追踪的控制请求，包含唯一标识、类型、超时/过期与参数。
- 设备稳定标识：用于路由与持久化的唯一键（MAC 语义）。

### 4. 功能需求

- 设备接入与在线管理：
  - 网关执行代理主动连接控制服务并注册。
  - 支持断线检测与离线回报。
- 统一控制命令提交：
  - 提供统一命令入口，支持超时/重试参数。
  - 首批命令能力：开机、重启、关机、状态采集开关、参数读取、参数保存。
- 命令查询与追踪：
  - 支持单命令与命令列表查询。
  - 支持设备/状态/类型过滤与分页。
- 状态推送与前端消费：
  - 以实时推送为主，不依赖前端轮询。
  - UI 通过 `/ws/v5/ui` 单通道承载 `action/result/event/error` 语义。
  - 支持全量状态事件与单设备观察事件，切换观察目标需立即生效且无旧数据残留。
- 参数管理：
  - 支持按设备读取和保存参数。
  - 参数在控制服务持久化并同步给执行代理，代理侧运行内存生效。
- 审计与访问行为记录：
  - 记录操作者、来源、操作类型、目标资源、结果摘要。
  - 提供可检索、可分页能力。
- 前端交互：
  - 分离展示“目标状态”与“执行结果”。
  - 执行结果可读化，避免直接暴露原始 JSON。
  - 状态采集提供单一开关语义（开/关/未知）。

### 5. 关键业务规则

- 角色边界：
  - 控制服务不直接执行设备动作。
  - 设备动作仅由网关执行代理完成。
- 命令状态机：
  - 生命周期：`CREATED -> DISPATCHED -> ACKED -> RUNNING -> 终态`。
  - 同一命令仅首个终态生效，重复终态回报保留审计记录但不覆盖。
- 标识与路由：
  - 稳定标识用于路由、参数存取与持久化。
  - 展示标识仅用于界面与审计可读性。
- 心跳与采集：
  - 代理持续发送心跳，且不受状态采集开关影响。
  - 仅状态采集开启时，心跳携带完整状态采样。
  - 心跳周期与采样周期独立配置。
- 控制结果回传：
  - `command.submit` 使用异步受理语义（accepted + result-meta）。
  - 终态通过 `command.event` 推送获取。
- 协议契约（v5）：
  - UI WS 路由：`/ws/v5/ui`，Agent WS 路由：`/ws/v5/agent`。
  - UI action：`session.subscribe`、`agent.list`、`params.get`、`params.update`、`command.submit`。
  - UI event：`agent.snapshot`、`agent.update`、`command.event`。
  - 信封字段：`v/kind/name/id/ts_ms/payload/target`，其中 `v = "v5"`，`kind` 为 `action/result/event/error`。
  - Agent action：`agent.register`、`agent.heartbeat`、`command.ack`、`command.result`。
  - Server->Agent event/error：`agent.registered`、`command.dispatch`、`server.error`。

### 6. 边界与异常处理

- 非法输入（类型错误、关键字段缺失、越界值）必须拒绝并返回可读错误。
- 空数据场景（无设备、无参数、无查询结果）返回明确空结果语义。
- 重复提交/重复回包按幂等规则处理，不破坏既有终态。
- 外部依赖失败（设备离线、网络中断、认证异常）返回可诊断错误并保留审计线索。
- 执行超时进入超时终态，请求等待超时返回当前状态并支持后续查询。
- 协议不匹配必须显式拒绝并返回可识别错误码。

### 7. 非功能需求与约束

#### 7.1 性能与稳定性

- 系统应支持并发控制请求下的稳定运行，并具备可配置的请求限流能力。
- 设备连接应具备断线重连能力。
- 状态推送应满足长连接持续运行需求。

#### 7.2 安全

- 公网访问必须经过统一身份认证入口。
- 认证应叠加站点允许名单，不在允许名单中的账号不得访问。
- 设备公网链路必须使用加密通道。
- 当前阶段不以 token 持久化与 token 核心鉴权作为主机制。

#### 7.3 审计

- 系统必须记录关键操作的操作者、时间、对象、动作与结果摘要。
- 审计与命令记录应支持检索与分页查询。

#### 7.4 兼容性与部署环境

- 系统应适配“公网控制服务 + 内网网关执行代理”的分离部署模式。
- 认证入口与控制服务可分层部署，控制服务可仅处理内网转发流量。
- 网关侧应满足可持续运行与自动拉起需求。

#### 7.5 数据生命周期

- 控制参数在控制服务侧持久化；执行代理仅保留运行态内存副本。
- 执行代理不应长期写入业务文件（参数、命令状态、业务日志）。
- 当前个人单租户阶段允许控制服务持有设备明文密码以保证可用性。

### 8. 待确认事项

- 审计日志、命令记录、设备状态数据的最短留存期与清理策略。
- 命令重试策略上限是否需要产品级统一硬限制。
- 多站点/多设备规模目标与容量指标（设备数、并发命令量、推送频率上限）。
- 明文设备密码策略退出条件与替代里程碑。
- 是否将 IPv6 纳入正式支持范围与验收项。

## 目录结构

- `owt-net/`：控制服务与前端相关代码（含 `deploy/deb` 打包脚本）。
- `owt-agent/`：网关执行代理与 OpenWrt `ipk` 打包定义。
- `owt-protocol/`：协议头文件与契约定义。
- `build-release.sh`：统一发布构建入口（同时产出 `deb` 与 `ipk`）。
- `owt-out/`：构建输出目录（默认自动创建在仓库上级目录）。

## 快速构建（发布产物）

在仓库根目录执行：

```bash
./build-release.sh
```

脚本会执行：

1. 构建 `owt-net` 的 `deb` 包。
2. 基于 OpenWrt SDK 构建 `owt-agent` 的 `ipk` 包。

默认输出目录：

```text
../owt-out
```

若 OpenWrt SDK 不在默认搜索路径，可显式指定：

```bash
OPENWRT_SDK_DIR=/path/to/openwrt-sdk-xxx ./build-release.sh
```

## owt-agent SSH 构建策略

- `owt-agent` 提供两个 CMake 开关：
  - `OWT_AGENT_REQUIRE_LIBSSH2`：开启后，若缺少 `libssh2` 头文件或库，配置阶段直接失败。
  - `OWT_AGENT_ALLOW_SSH_STUB`：开启后，允许在缺少 `libssh2` 时编译 SSH stub（仅用于本地开发）。
- 发布路径已固定强约束：
  - `build-release.sh` 触发的 OpenWrt `ipk` 构建，统一使用 `OWT_AGENT_REQUIRE_LIBSSH2=ON` 且 `OWT_AGENT_ALLOW_SSH_STUB=OFF`。
  - 发布构建缺少 `libssh2` 时必须失败，不允许回退到 stub。
- 本地开发默认行为：
  - 允许 stub（可编译、会有告警），便于无交叉依赖环境下调试其它功能。

## owt-agent 配置

默认配置文件位于：

```text
owt-agent/application/owt-agent/files/config.ini
```

安装到设备后路径为：

```text
/etc/owt-agent/config.ini
```

关键配置项包括 `agent_id`、`agent_mac`、`wss_endpoint`、`heartbeat_interval_ms`、`status_collect_interval_ms`。

## 独立认证中心部署约定（auth.wzhex.com）

- `owt-net` 站点（`owt.wzhex.com`）仅保留内部鉴权子请求 `location = /oauth2/auth`（`internal`），用于向本机 `oauth2-proxy`（`127.0.0.1:4180`）校验会话。
- 未登录访问 `owt.wzhex.com` 时，nginx 通过 `error_page 401` 跳转到 `https://auth.wzhex.com/oauth2/start?rd=$scheme://$host$request_uri`。
- `owt.wzhex.com` 不再公开提供 `/oauth2/*` 路径（无兼容入口）；Google OIDC 登录入口与 `/oauth2/callback` 均由 `auth.wzhex.com` 站点承载。
- 认证 cookie 需使用跨子域配置（例如 `cookie_domains = [".wzhex.com"]`），以支持 `auth.wzhex.com` 与 `owt.wzhex.com` 会话共享。
- `owt-net` 的 `deb` 包不负责安装、配置或管理 `oauth2-proxy` 独立服务；运维侧需自行保证认证服务可用。

## owt-agent 进程内无文件写入约束

- 约束范围仅限 `owt-agent` 进程自身：不允许业务日志或运行态数据写入本地文件。
- `stdout/stderr` 输出允许保留；外部组件（如 `procd/logd`）的持久化策略不在该约束范围内。

配套双验收（仅在 `BUILD_TESTING=ON` 时生效）：

1. `owt-agent-no-file-write-static`：静态扫描 `owt-agent/src`、`owt-agent/include`，拦截文件写相关 API 与 spdlog file sink。
2. `owt-agent-no-file-write-runtime`：基于 `strace` 跟踪运行测试二进制，发现文件系统变更类 syscall 即失败。

相关 CMake 选项：

- `OWT_AGENT_ENABLE_NO_FILE_WRITE_CHECKS=ON|OFF`（默认 `ON`）：总开关。
- `OWT_AGENT_ENABLE_NO_FILE_WRITE_RUNTIME_CHECK=ON|OFF`（默认 `ON`）：运行期 `strace` 验收开关。
- 当运行期开关为 `ON` 且 `BUILD_TESTING=ON` 时，若环境缺少 `strace`，配置阶段会直接失败。
