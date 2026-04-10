# Build And Release

本文档统一说明 `owt-net` 与 `owt-agent` 的构建方式、产物路径和发布脚本约束。

## 总体规则

- 交付产物统一输出到：`owt-ipk/../owt-out/`
- 不再按 `ipk/`、`deb/`、`web/` 子目录分流
- `owt-agent` 仅支持 OpenWrt SDK package 编译，不支持本地 CMake/Make 编译

## owt-net（Ubuntu）

本地调试构建：

```bash
cd owt-ctrl/owt-net
cmake --preset gcc-debug
cmake --build --preset gcc-debug
```

Deb 打包（会自动构建前端并打入 deb）：

```bash
cd owt-ctrl/owt-net
./deploy/deb/build-deb.sh gcc-release
```

默认产物位置：`owt-ipk/../owt-out/*.deb`

`owt-net.deb` 现在会在打包时自动拉取最新 `oauth2-proxy`（按构建机架构），并将以下文件一并打入包内：

- `/usr/local/bin/oauth2-proxy`
- `/etc/owt-net/oauth2-proxy.cfg`（conffile，需替换真实 `client_id/client_secret/cookie_secret`）
- `/lib/systemd/system/oauth2-proxy.service`

安装 `owt-net.deb` 后会自动 `enable/start` `owt-net` 和 `oauth2-proxy`。
如需固定版本构建，可在打包前设置：

```bash
export OAUTH2_PROXY_VERSION=v7.15.1
./deploy/deb/build-deb.sh gcc-release
```

## owt-agent（OpenWrt SDK）

`owt-agent` 是 OpenWrt `ipk` 包，必须通过 SDK package 流程编译。

设置 SDK 路径：

```bash
export OPENWRT_SDK_DIR=/path/to/openwrt-sdk-24.10.5-...
```

方式 A：作为 SDK 本地包

```bash
ln -snf /path/to/owt-ipk/owt-ctrl/owt-agent/application/owt-agent \
  "$OPENWRT_SDK_DIR/package/owt-agent"
make -C "$OPENWRT_SDK_DIR" package/owt-agent/compile V=s
```

方式 B：作为 feed 包（参考 fancontrol/qmodem）

```bash
./scripts/feeds update owt-ctrl
./scripts/feeds install -f -p owt-ctrl owt-agent
make package/feeds/owt-ctrl/owt-agent/compile V=s
```

默认产物位置：`owt-ipk/../owt-out/owt-agent*.ipk`

## 一键发布

```bash
cd owt-ctrl
./build-release.sh
```

脚本行为：

- 先打 `owt-net` deb（含前端）
- 再打 `owt-agent` ipk
- 最终将 `*.deb` 与 `owt-agent*.ipk` 统一放入 `owt-out/`

## Google 鉴权部署校对（owt.wzhex.com）

- `Nginx` 站点配置 `server_name` 必须是 `owt.wzhex.com`（见 `owt-net/deploy/nginx/owt-net.conf`）。
- `owt-agent` 默认上游 endpoint 必须是 `wss://owt.wzhex.com/ws/control`（见 `owt-agent/application/owt-agent/files/config.ini`）。
- `oauth2-proxy` 配置模板见 `owt-net/deploy/nginx/oauth2-proxy-google.example.cfg`。
- Google Cloud Console OAuth Client 白名单必须包含：
  - Authorized JavaScript origins: `https://owt.wzhex.com`
  - Authorized redirect URIs: `https://owt.wzhex.com/oauth2/callback`

### 路径鉴权矩阵（必须保持）

- `/`、`/api/*`、`/ws/status`：必须走 `oauth2-proxy` 登录鉴权。
- `/ws/control`：用于 `owt-agent` 设备通道，不走网页登录鉴权。

说明：`/ws/control` 若被 OAuth 登录链路拦截，会出现 agent 重连并离线（常见 `302`）；若后端二进制仍要求统一身份头，会出现 `401`。

为兼容旧版 `owt-net` 二进制，`/ws/control` 建议保留固定头透传：

```nginx
proxy_set_header   X-Forwarded-User owt-agent;
proxy_set_header   X-Forwarded-Email owt-agent@local;
```

## 逻辑冲突与修正

1. 冲突：此前 `build-release.sh` 同时维护 `WEB_OUT`，而 deb 打包流程已内建前端构建。  
   修正：移除 `WEB_OUT` 与独立 web 构建步骤，前端只走 deb 流程。

2. 冲突：此前产物分散在 `owt-out/ipk` 与 `owt-out/deb`，与“统一产物目录”目标冲突。  
   修正：`build-release.sh` 和 `build-deb.sh` 统一输出到 `owt-out/`。

3. 冲突：子工程内文档与根文档并存，容易造成路径与流程描述不一致。  
   修正：文档统一收口到 `owt-ctrl/docs/`，子工程不再保留 README/docs。

## 常见故障排查（鉴权相关）

1. 现象：`/ws/control` 持续 `302`  
   原因：`/ws/control` 仍被 `oauth2-proxy` 重定向登录。  
   处理：从 `location = /ws/control` 中移除 `auth_request`。

2. 现象：`/ws/control` 持续 `401`  
   原因：`owt-net` 后端仍在统一校验 Google 身份头。  
   处理：
   - 升级到包含 `ws_path` 分流校验的新版本；
   - 或在 nginx `/ws/control` 临时补固定 `X-Forwarded-User/Email` 头。

3. 现象：前端有 agent 但离线  
   检查项：
   - `/ws/control` 是否 `101`；
   - `owt-agent` 是否连接到 `wss://owt.wzhex.com/ws/control`；
   - `owt-net` 日志是否持续出现 register/heartbeat。
