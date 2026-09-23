# Pandora（M6）

依据 [docs/棋牌游戏服务端-SPEC.md](docs/棋牌游戏服务端-SPEC.md)、[M1](docs/棋牌游戏服务端-M1计划.md)、[M2](docs/棋牌游戏服务端-M2计划.md)、[M2b](docs/棋牌游戏服务端-M2b计划.md)、[M3](docs/棋牌游戏服务端-M3计划.md)、[M4](docs/棋牌游戏服务端-M4计划.md)、[M5](docs/棋牌游戏服务端-M5计划.md)、[M6](docs/棋牌游戏服务端-M6计划.md)。

## 范围

- **M1**：单体骨架、HTTP/WSS、帧编解码、登录会话、心跳
- **M2 / M2b / M3**：对局、game-web、钱包、沙箱支付、断线重连
- **M4**：MySQL/Redis、Admin API、admin-web、场次/档位热更、审计
- **M5**：活动引擎、game-web `/activity`、admin 活动 CRUD
- **M6**：HTTP/WSS **IOCP**、报表 CSV、`force_tls`、`ccu_load` 压测

**未包含（后续）**：完整 Schannel TLS、真实支付宝验签、冲榜。

## 目录

```text
proto/          # .proto
server/         # C++ pandora-server
game-web/       # Vue 网页客户端
admin-web/      # Vue 运营后台（Element Plus）
docs/           # SRS / SPEC / 里程碑计划
docker-compose.yml
```

## 本地依赖（Docker Desktop）

```powershell
docker compose up -d
docker compose ps
```

| 服务 | 端口 | 账号 |
|------|------|------|
| MySQL `mysql:5.7` | 3306 | `pandora` / `pandora`，库 `pandora` |
| Redis `redis:6.2.18` | 6379 | 无密码 |

## 构建服务端

```powershell
cd server
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
cd build\Release
.\pandora-server.exe
```

冒烟：

```powershell
node server\scripts\m2_smoke.mjs
node server\scripts\m3_smoke.mjs
node server\scripts\admin_smoke.mjs
node server\scripts\activity_smoke.mjs
node server\scripts\ccu_load.mjs --target 200
```

`GET /health` 应返回 `mysql:true, redis:true` 及 `ccu` / `iocp_workers`。

## 运营后台

```powershell
cd admin-web
npm install
npm run dev
```

- 地址：`http://127.0.0.1:5174`
- 默认超管：`admin` / `admin123`
- 报表页可下载账变 / 对局 / 活动领取 CSV

## game-web

```powershell
cd game-web
npm install
npm run dev
```

## 验收清单

1. Docker MySQL/Redis healthy；`/health` 双 true  
2. HTTP/WSS 经 IOCP；无按连接 detach 线程  
3. `admin_smoke` / `activity_smoke` / 低并发 `ccu_load` 通过  
4. Admin 报表 CSV 可下载并有审计  
