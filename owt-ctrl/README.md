# owt-ctrl

`owt-ctrl` 是顶层工程（monorepo），包含两个子项目：

- `owt-agent`：OpenWrt 侧执行代理，作为 `ipk` 包发布。
- `owt-net`：Ubuntu 侧控制面，目标交付为 `owt-net.deb`（安装后以 systemd 守护进程运行）。

## 目录结构

- `owt-agent/`：Agent 工程（OpenWrt IPK）
- `owt-net/`：Control Plane 工程（Ubuntu Deb + Service）
- `docs/`：工程唯一文档目录

## 文档导航

- `docs/requirements.md`：需求边界与确认项
- `docs/clean-switch-constraints.md`：clean switch 硬约束（本轮最高优先级）
- `docs/architecture.md`：控制面实现与组件关系
- `docs/vps-agent-architecture.md`：总体架构方案
- `docs/phase0-command-protocol.md`：控制通道协议草案
- `docs/build-and-release.md`：统一构建、产物路径与发布流程（`owt-net` + `owt-agent`）
- `docs/owt-agent-runtime.md`：`owt-agent` 运行时流程与持久化行为

文档约束：

- `owt-ctrl/docs/` 是唯一文档入口。
- 子工程目录（`owt-agent/`、`owt-net/`）不再放置 README/docs 文档，避免多源描述造成误导。

## 快速构建

```bash
# Ubuntu control plane
cd owt-net
cmake --preset gcc-debug
cmake --build --preset gcc-debug

# OpenWrt agent (SDK package build only)
cd ..
OPENWRT_SDK_DIR=/path/to/openwrt-sdk-24.10.5-...
make -C "$OPENWRT_SDK_DIR" package/owt-agent/compile V=s
```

构建产物统一输出到：`owt-ipk/../owt-out/`
