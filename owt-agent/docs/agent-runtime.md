# owt-agent Runtime

## 处理流程

1. 启动时读取 `config.ini` 并初始化本地存储。
2. 按配置启动 WSS/gRPC 控制通道。
3. 向 `owt-ctrl` 发送 `REGISTER`。
4. 周期发送 `HEARTBEAT`。
5. 收到 `COMMAND_PUSH` 后立即回 `COMMAND_ACK`。
6. 执行命令并回传 `COMMAND_RESULT`。

## 本地持久化

- 命令库：`/etc/owt-agent/owt_agent.db`（不可写时回退 `./owt_agent.db`）
- 参数文件：`/etc/owt-agent/params.ini`（不可写时回退 `./params.ini`）
- 日志文件：`/var/log/owt-agent/owt-agent.log`（不可写时回退 `./logs/owt-agent.log`）

## 信号与退出

- 捕获 `SIGINT/SIGTERM`。
- 停止控制通道、监控线程并安全关闭本地存储。
