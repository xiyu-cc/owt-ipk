# owt-agent Runtime

## 处理流程

1. 启动时读取 `/etc/owt-agent/config.ini` 初始值。
2. 按配置建立 WSS 控制通道。
3. 向 `owt-net` 发送 `REGISTER`。
4. `owt-net` 在注册后主动下发 `PARAMS_SET`，覆盖运行态参数。
5. 周期发送 `HEARTBEAT`。
6. 收到 `COMMAND_PUSH` 后立即回 `COMMAND_ACK`。
7. 执行命令并回传 `COMMAND_RESULT`。

## 本地数据策略

- 命令状态不落盘，不使用本地数据库。
- 参数仅保存在进程内存；参数源由 `owt-net` 持久化并主动下发。
- `owt-agent` 不写参数文件与日志文件（日志仅输出 stdout/stderr）。

## 控制通道约束

- 仅支持 `wss://`。
- TLS 证书校验强制启用（CA 信任链 + 主机名校验）。
- 默认控制通道 endpoint：`wss://owt.wzhex.com/ws/control`。
- 未显式指定端口时默认 `443`。
- 未显式指定路径时默认 `/ws/control`。

## 信号与退出

- 捕获 `SIGINT/SIGTERM`。
- 停止控制通道与监控线程并退出。
