# Pandora（M5）

依据 [docs/棋牌游戏服务端-SPEC.md](docs/棋牌游戏服务端-SPEC.md)、[M2](docs/棋牌游戏服务端-M2计划.md)、[M2b](docs/棋牌游戏服务端-M2b计划.md)、[M3](docs/棋牌游戏服务端-M3计划.md)、[M4](docs/棋牌游戏服务端-M4计划.md)、[M5](docs/棋牌游戏服务端-M5计划.md)。

## 范围

- **M2 / M2b / M3**：对局、game-web、钱包、沙箱支付、断线重连
- **M4**：MySQL/Redis 客户端、Admin API、admin-web、场次/档位热更、审计、封禁/维护
- **M5**：活动引擎（签到/任务/礼包）、`S2C_ActivityUpdate` 3001、game-web `/activity`、admin 活动 CRUD

**未包含（M6+）**：冲榜活动、真实支付宝验签、报表导出。

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
```

`GET /health` 应返回 `mysql:true, redis:true`。

## 运营后台

```powershell
cd admin-web
npm install
npm run dev
```

- 地址：`http://127.0.0.1:5174`
- 默认超管：`admin` / `admin123`
- 环境变量：`VITE_ADMIN_API=http://127.0.0.1:8080`

可完成：看板、玩家踢/封/补发、账变、对局、场次热更、充值档位、订单、公告、维护开关、审计、**活动 CRUD**。

## game-web

```powershell
cd game-web
npm install
npm run dev
```

大厅可进入 **活动中心**（`/activity`）：列表、进度、领奖；WSS `ActivityUpdate(3001)` 刷新。

## 验收清单

1. Docker MySQL/Redis healthy；`/health` 双 true  
2. `admin_smoke` / `activity_smoke` 通过  
3. admin-web 活动可创建/编辑/下架；审计有记录  
4. game-web `/activity` 可签到/领奖；任务随结算推进  
5. M2/M3 冒烟回归  
