# 变更落盘记录（2026-04-10）

## 背景

本次变更基于以下新增硬约束：

- `owt-agent` 运行在 OpenWrt/eMMC 环境，不允许长期写盘。
- `owt-net` 作为公网中转站，不允许存储任何 token，不采用 token 作为核心鉴权方式。
- 保持 `owt-net` 现有 `HTTP API` 路径与语义不变。

## 变更范围

- 子工程：`owt-agent`
- 子工程：`owt-net`
- 文档：`docs/*`

## 已落地改动

### 1. `owt-agent` 移除数据库落盘（破坏式）

- 删除命令库接口与实现：
  - `owt-agent/include/service/command_store.h`
  - `owt-agent/src/core/command_store.cpp`
- 删除启动/退出流程中的命令库初始化与关闭调用。
- 运行时命令去重改为进程内内存集合（仅当前进程生命周期有效）。
- OpenWrt 包构建移除 sqlite3 相关编译与链接项。

### 2. 参数存储切换到 `owt-net`（`owt-agent` 无写盘）

- `owt-net` 新增 `agent_params` 持久化，按 `agent_id` 保存控制参数。
- `PARAMS_SET` 由 `owt-net` 先落库，再下发到 `owt-agent`。
- `owt-agent` 注册连接后，`owt-net` 会主动下发最新参数。
- `owt-agent` 参数仅在进程内存持有，不再读写 `params.ini`。
- `owt-agent` 日志输出改为 stdout/stderr，不再写日志文件。

### 3. `owt-net` 鉴权改造（Google OIDC + 反向代理身份）

- 删除 `management_token` 配置项（结构体、解析、默认配置）。
- `Nginx` 接入 `oauth2-proxy`（Google OIDC）并向后端转发身份头。
- `owt-net` 对 HTTP/WS 统一校验 `X-Forwarded-Proto=https` 与身份头（`X-Forwarded-Email/User`）。
- 前端不再依赖 token 输入、保存、透传逻辑（含 `Authorization` 与 `access_token`）。

### 4. 现有功能保持项

- `control/*` HTTP API 路径与语义保持不变。
- 前端状态 `WS` 主动推送机制保持可用。
- 状态区保持“状态监控开关（`MONITORING_SET`）”交互。

## 文档一致性修正

已同步修正文档中与旧策略冲突的条目：

- 移除“management_token 双层防护”表述，改为“边缘无感鉴权（反向代理身份认证）”。
- 移除/替换 `owt-agent` 命令库落盘与参数写盘描述。
- 协议错误码中移除 `AUTH_FAILED`。

涉及文件：

- `docs/requirements.md`
- `docs/clean-switch-constraints.md`
- `docs/vps-agent-architecture.md`
- `docs/phase0-command-protocol.md`
- `docs/owt-agent-runtime.md`

## 验证记录（2026-04-10）

- `owt-net`：
  - `cmake --preset gcc-debug`
  - `cmake --build --preset gcc-debug`
  - `ctest --preset gcc-debug --output-on-failure`
  - 结果：通过
- 前端：
  - `npm run build`
  - 结果：通过
- `owt-agent`：
  - 关键改动文件完成语法级检查（不含本机缺失 OpenSSL 头时的 TLS 相关编译验证）
  - 完整 OpenWrt SDK 打包验证需在 SDK 环境执行

## 影响说明

- 本次为 clean switch，明确不兼容旧的 token 鉴权与 agent 命令库落盘行为。
- 参数持久化转移到 `owt-net`；`owt-agent` 不写任何业务文件。

## 补充改动（2026-04-10 晚）

### 1. 鉴权边界按通道拆分

- `/ws/control` 是 `owt-agent -> owt-net` 控制通道，不依赖浏览器 OAuth cookie。
- `/api/*`、`/ws/status`、`/` 继续走 `oauth2-proxy`（Google 登录态）鉴权。
- 修正目标：避免把设备通道错误纳入“网页登录鉴权”导致 agent 长期离线。

### 2. `/ws/control` 401 根因与修复

- 现场现象：`/ws/control` 从 `302`（被重定向到登录）变为 `401`（后端拒绝）。
- 根因：`owt-net` 旧逻辑对所有 WebSocket 升级统一要求 Google 身份头。
- 修复：
  - `owt-net` 代码按 `ws_path` 分流，`/ws/control` 不再要求 Google 身份头。
  - `Nginx` 对 `/ws/control` 增加固定 `X-Forwarded-User/Email`，兼容旧版后端二进制。

### 3. 生效判据

- `access.log` 中 `/ws/control` 不再是 `302/401`，应出现 `101`。
- 前端 agent 列表状态恢复在线并持续心跳更新。
