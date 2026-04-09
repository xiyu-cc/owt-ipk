# owt-ctrl

`owt-ctrl` 是顶层工程（monorepo），包含两个子项目：

- `owt-agent`：OpenWrt 侧执行代理，作为 `ipk` 包发布。
- `owt-net`：Ubuntu 侧控制面服务（systemd 守护进程）。

## 仓库结构

- `owt-agent/`：Agent 工程（OpenWrt IPK）
- `owt-net/`：Control Plane 工程（Ubuntu Service）
- `docs/`：主文档（需求、架构、协议）

## 文档导航

- `docs/requirements.md`：需求边界与确认项
- `docs/vps-agent-architecture.md`：总体架构方案
- `docs/phase0-command-protocol.md`：控制通道协议草案

## 快速构建

```bash
# Ubuntu control plane
cd owt-net
cmake --preset gcc-debug
cmake --build --preset gcc-debug

# OpenWrt agent
cd ../owt-agent
cmake --preset gcc-debug
cmake --build --preset gcc-debug
```
