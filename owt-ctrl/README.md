# owt-ctrl

`owt-ctrl` 是运行在 VPS 或中心节点上的控制面服务，负责：

- 提供 HTTP API（任务下发、任务查询、本地运维接口）
- 接收并管理 `owt-agent` 长连接（WSS / gRPC）
- 维护命令状态机与审计事件（SQLite）

仓库已按工程拆分：

- `owt-ctrl/`：控制面工程（本目录）
- `owt-agent/`：执行面工程（网关侧）

## 目录结构

- `include/server/*`：HTTP/WSS/gRPC server 入口
- `include/api/*`：HTTP API 定义与注册
- `include/control/*`：控制通道协议模型与编解码
- `include/service/*`：命令存储、会话路由、执行能力封装
- `src/core/*`：主程序、配置、日志、服务实现
- `src/api/*`：REST API 实现
- `proto/*`：控制通道 proto
- `docs/*`：架构与协议文档

## 配置

仅支持控制面配置：

```ini
[server]
host = 0.0.0.0
port = 9527
threads = 4
enable_grpc = true
grpc_endpoint = 0.0.0.0:50051
```

## 构建

```bash
cmake --preset gcc-debug
cmake --build --preset gcc-debug
```

生成：`build/gcc-debug/owt-ctrl`

## 运行

```bash
./build/gcc-debug/owt-ctrl ./config.ini
```

## OpenWrt 打包目录

- `application/owt-ctrl/`

默认部署文件：

- `/usr/sbin/owt-ctrl`
- `/etc/init.d/owt-ctrl`
- `/etc/config/owt-ctrl`
- `/etc/owt-ctrl/config.ini`
- `/etc/owt-ctrl/params.ini`
