# 需求文档

## 项目拆分要求

- 统一顶层工程名为 `owt-ctrl`。
- `owt-ctrl` 下包含两个子工程：`owt-agent` 与 `owt-net`。
- `owt-agent` 负责网关执行面，作为 OpenWrt 的 `ipk` 包。
- `owt-net` 负责公网控制面，作为 Ubuntu 服务运行。

## 已确认关键句

`owt-agent是openwrt的ipk包，owt-net是ubuntu服务。`

## 文档组织要求

- 主文档集中在 `owt-ctrl/docs/`。
- 子工程文档仅保留实现细节与构建说明，并回链主文档。
