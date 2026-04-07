# owt-ctrl Vue Frontend

面向局域网远程控制的 Vue3 前端，调用 `owt-ctrl` HTTP API：

- `GET /api/v1/params/get`
- `POST /api/v1/params/set`
- `POST /api/v1/wol/wake`
- `POST /api/v1/host/reboot`
- `POST /api/v1/host/poweroff`
- `GET /api/v1/host/probe`

## 开发

```bash
cd frontend
npm install
npm run dev
```

默认监听：

- `0.0.0.0:5173`（局域网设备可访问）

## 打包

```bash
npm run build
```

产物目录：

- `frontend/dist`

## 说明

- 前端固定请求地址：`http://openwrt.lan:9527`。
- 页面加载时自动读取服务端参数，修改后通过保存接口写回 `params.ini`。
- 页面开启后每秒调用一次探针接口，展示目标设备在线状态与 CPU/MEM/上下行速率。
- 当前请求为明文 HTTP，公网场景建议加 HTTPS 反向代理与鉴权。
