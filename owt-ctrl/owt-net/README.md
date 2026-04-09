# owt-net

`owt-net` 是运行在 Ubuntu 上的控制面服务（Control Plane），负责：

- 提供对外 HTTP API
- 维护与 `owt-agent` 的控制通道（WSS / gRPC）
- 维护命令状态机、事件审计与查询

## 目录结构

- `include/server/*`：HTTP/WSS/gRPC server 入口
- `include/api/*`：HTTP API 定义与注册
- `include/control/*`：控制通道协议模型与编解码
- `include/service/*`：命令存储、会话路由、能力封装
- `src/core/*`：主程序、配置、日志、服务实现
- `src/api/*`：REST API 实现
- `proto/*`：控制通道 proto

## 配置

默认配置：`config.ini`

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

生成：`build/gcc-debug/owt-net`

## 运行

```bash
./build/gcc-debug/owt-net ./config.ini
```

## Ubuntu 服务化（systemd）

建议配置：

- 二进制：`/usr/local/bin/owt-net`
- 配置目录：`/etc/owt-net/`
- 日志目录：`/var/log/owt-net/`
- systemd 单元模板：`deploy/systemd/owt-net.service`

主文档在顶层：`../docs/`。

备注：`application/owt-ctrl/` 与 `frontend/` 为历史目录，当前部署主线按 Ubuntu 服务方式运行 `owt-net`。
