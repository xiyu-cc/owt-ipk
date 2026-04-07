# owt-ctrl

`owt-ctrl` 是一个用于 OpenWrt 的局域网控制服务底座，目标是作为常驻进程运行并支持开机自启，通过 HTTP 接口对内网设备执行控制动作。

典型场景：

- 向指定设备发送 WOL 魔术包（开机）
- 向指定设备发送 `reboot` / `poweroff` 指令
- 统一由 HTTP API 调用，便于 LuCI、脚本或其他控制端接入

## 项目目标

- 作为 OpenWrt 常驻服务运行（daemon）
- 支持开机自启（procd/init.d）
- 统一 API 开发模型：每个接口继承 `api_base`
- 路由按 `method + path` 精确分发
- 响应统一 JSON，便于前端和自动化脚本处理

## 当前代码状态

已实现：

- Boost.Beast HTTP 服务主链路（多线程 `io_context`）
- WebSocket 升级能力（基础 echo）
- API 调度框架（`api_base` + `factory` + `api_invoke`）
- `OPTIONS` 预检与基础 CORS 头
- INI 配置读取（`server.host/port/threads`）
- 日志（spdlog，滚动文件 + 控制台）
- 首批控制接口：
  - `POST /api/v1/wol/wake`（内置发送魔术包，算法对齐 `wakeonlan` 上游）
  - `POST /api/v1/host/reboot`（通过 `libssh2`）
  - `POST /api/v1/host/poweroff`（通过 `libssh2`）
  - `GET /api/v1/params/get`（读取前端参数）
  - `POST /api/v1/params/set`（保存前端参数）

未实现（待补）：

- OpenWrt 打包细节完善（conffiles、发布流程）
- 认证鉴权与访问控制

## 目录结构

- `include/server/*`：网络层（listener/http_session/websocket_session/controller）
- `include/http_deal/*`：HTTP 分发与响应封装
- `include/api/*`：API 注册入口（`api_holder`）
- `src/core/*`：主程序、配置、日志、工具
- `third_party/*`：第三方依赖与上游快照（Boost/spdlog/nlohmann-json/wakeonlan）

## 请求处理链路

1. `listener` 接收连接
2. `http_session` 解析 HTTP 请求
3. `http_request_handler` 按 Method 进入 `api_invoke<method, Body>`
4. `api_invoke` 从 `factory<string, api_base<...>>` 根据路径取实例
5. 命中接口后执行 `operator()(req)` 返回响应

路径匹配基于 `utils::url_path(req.target())`，即仅使用 `?` 之前的路径部分。

## API 开发规范（核心）

每个业务接口都需要：

1. 继承 `http_deal::api_base<method::xxx, http::vector_body<uint8_t>>`
2. 实现 `operator()(http::request<Body>& req)`
3. 在 `api_holder` 中注册到 `factory`

示例（以 `POST /api/v1/wol/wake` 为例）：

```cpp
class wol_wake_api final
    : public http_deal::api_base<http_deal::method::post,
                                 http_deal::http::vector_body<uint8_t>> {
public:
  http_deal::http::message_generator operator()(
      http_deal::http::request<http_deal::http::vector_body<uint8_t>>& req) override;
};
```

注册示例：

```cpp
using post_factory = http_deal::factory<
    std::string,
    http_deal::api_base<http_deal::method::post, http_deal::http::vector_body<uint8_t>>>;

post_factory::install(http_deal::inplace_hold<wol_wake_api>{}, "/api/v1/wol/wake");
```

## 首批接口设计（建议）

1. `POST /api/v1/wol/wake`
请求体建议：
`{"mac":"AA:BB:CC:DD:EE:FF","broadcast":"192.168.1.255","port":9}`
行为：发送魔术包，返回发送结果。

2. `POST /api/v1/host/reboot`
请求体建议：
`{"host":"192.168.1.10","port":22,"user":"root","password":"***","timeout_ms":5000}`
行为：通过受控通道触发目标设备重启。

3. `POST /api/v1/host/poweroff`
请求体建议与 `reboot` 类似，行为改为关机。

建议统一响应格式：

```json
{
  "ok": true,
  "code": 0,
  "message": "success",
  "data": {}
}
```

## 配置

当前支持：

```ini
[server]
host = 0.0.0.0
port = 9527
threads = 4
```

建议后续扩展：

```ini
[auth]
enable = 1
token = your-token

[access]
allow_lan_only = 1
allowlist = 192.168.1.0/24,10.0.0.0/24
```

## 构建与运行

构建：

```bash
cmake --preset gcc-debug
cmake --build --preset gcc-debug
```

可执行文件：

- `build/gcc-debug/owt-ctrl`

启动：

```bash
./build/gcc-debug/owt-ctrl
```

## Vue 前端（跨设备访问）

已提供前端工程目录：`frontend/`（Vue3 + Vite）。

开发启动：

```bash
cd frontend
npm install
npm run dev
```

生产构建：

```bash
npm run build
```

构建产物在 `frontend/dist`，可部署到任意静态站点或反向代理。

## OpenWrt 自启（部署建议）

落地时建议：

- 可执行文件放置到 `/usr/sbin/owt-ctrl`
- 配置放置到 `/etc/owt-ctrl/config.ini`
- 前端参数放置到 `/etc/owt-ctrl/params.ini`
- 使用 `/etc/init.d/owt-ctrl` + procd 管理守护进程

`/etc/init.d/owt-ctrl` 示例骨架：

```sh
#!/bin/sh /etc/rc.common
USE_PROCD=1
START=95
STOP=10

start_service() {
  procd_open_instance
  procd_set_param command /usr/sbin/owt-ctrl /etc/owt-ctrl/config.ini
  procd_set_param respawn
  procd_set_param stdout 1
  procd_set_param stderr 1
  procd_close_instance
}
```

当前程序已支持通过启动参数指定配置文件路径（默认仍为 `config.ini`）。

启用：

```bash
/etc/init.d/owt-ctrl enable
/etc/init.d/owt-ctrl start
```

## 安全建议

- 默认仅监听 LAN 地址，不直接暴露 WAN
- 对控制接口增加 token 或签名认证
- 对关键动作（reboot/poweroff）增加来源白名单与审计日志
- 业务执行层采用最小权限，避免直接以高危命令裸执行

## 下一步实现顺序（建议）

1. 补齐 `src/api/*` 业务接口实现（先 WOL，再 reboot/poweroff）
2. 在 `api_holder` 完成路由注册
3. 增加认证中间层（header token）
4. 增加 OpenWrt init + package Makefile
5. 增加接口级单元测试与基本压测

## IPK 打包（已提供骨架）

仓库内已提供包目录：

- `application/owt-ctrl/Makefile`
- `application/owt-ctrl/files/owt-ctrl.init`
- `application/owt-ctrl/files/owt-ctrl.config`
- `application/owt-ctrl/files/config.ini`

在 OpenWrt SDK / Buildroot 中将该目录作为 feed 包后，可使用：

```bash
make package/owt-ctrl/compile V=s
```

生成包名示例：`owt-ctrl_0.1.0-1_<arch>.ipk`。

当前包依赖（按你的策略）：

- `libssh2`：SSH 通道使用高性能 C 库实现
- WOL 逻辑：已内置实现，不依赖外部 `wakeonlan` 命令
