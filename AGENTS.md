# Pandora — Agent 指南

棋牌游戏单体服务端（C++）+ Vue 网页客户端 + 运营后台。当前里程碑：**M6**。

需求冲突时以 `docs/棋牌游戏服务端-需求文档.md`（SRS）为准，技术实现以 `docs/棋牌游戏服务端-SPEC.md` 为准。

## 硬约束（不可偏离）

- **单体单进程**：模块间同步调用；无微服务、无消息队列拆分。
- **单机房单实例**：目标 CCU ≥ 20000；HTTP/WSS 走 IOCP，禁止按连接 detach 线程。
- **协议**：长连接帧 `uint32 LE len | uint32 LE msg_id | protobuf`；Admin / 部分短连接用 HTTPS+JSON。
- **货币**：金币 + 钻石；支付为支付宝 APP（当前沙箱）；无房卡、无冲榜。
- **首发玩法**：斗地主经典简单规则；匹配/对局自研。
- **存储**：MySQL 单主库（权威数据）；Redis 仅缓存/会话/开关，**可重建**。

## 仓库地图

```text
docs/           # SRS / SPEC / M1–M6 计划
proto/          # 唯一协议契约（.proto）
server/         # C++ pandora-server（MSVC / CMake）
  src/net/      # HTTP、WSS、帧、SessionHub、IOCP
  src/auth/ lobby/ match/ room/ game/
  src/wallet/ pay/ activity/ admin/ social/
  src/store/    # MysqlClient、RedisClient、MemoryStore
  conf/         # server.json / server.tls.json
  sql/          # schema.sql
  scripts/      # *_smoke.mjs、ccu_load.mjs
game-web/       # Vue3 游戏客户端（联调/演示）
admin-web/      # Vue3 + Element Plus 运营后台
docker-compose.yml  # MySQL 5.7 + Redis 6.2
```

命名空间：`pandora`。日志宏：`PLOG_INFO` / `PLOG_WARN` / `PLOG_ERROR`（见 `server/src/common/log.hpp`）。

## 依赖与本地环境

| 组件 | 说明 |
|------|------|
| 构建 | CMake + Visual Studio 2022 x64，C++17+ |
| 包管理 | vcpkg：boost-asio/beast、nlohmann-json、protobuf、libmysql、hiredis、spdlog、openssl |
| MySQL | `127.0.0.1:3306`，用户/库 `pandora` / `pandora` |
| Redis | `redis://127.0.0.1:6379/0`，无密码 |
| 默认端口 | HTTP `8080`，WS `8081` |

```powershell
docker compose up -d
cd server
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
cd build\Release
.\pandora-server.exe
```

冒烟（需服务已启动）：

```powershell
node server\scripts\m2_smoke.mjs
node server\scripts\m3_smoke.mjs
node server\scripts\admin_smoke.mjs
node server\scripts\activity_smoke.mjs
node server\scripts\ccu_load.mjs --target 200
```

`GET /health` 应返回 `mysql:true, redis:true`。

Admin 默认超管：`admin` / `admin123`（`admin-web` 开发端口 `5174`）。

## 数据落点（改存储前必读）

### MySQL（权威）

用户、资料、账变 `ledger`（`idempotent_key` 唯一）、场次模板、对局 `game_round`、活动定义/进度/领取/库存、支付订单、管理员与审计等。以 `server/sql/schema.sql` 为准。

### Redis（可重建，仅 STRING）

| Key | 用途 | TTL |
|-----|------|-----|
| `sess:{token}` | 玩家会话 → uid | 24h |
| `player:{uid}` | 资料 JSON 缓存 | 1h |
| `user:open:1:{open_id}` | 游客 open_id → uid | 1h |
| `admin:session:{token}` | 后台会话 | 24h |
| `ops:maintain` | 维护开关 `0/1` | 无 |
| `user:ban:{uid}` | 封禁快查 `0/1` | 无 |
| `ops:announce:last` | 最近公告副本 | 24h |
| `act:prog:{aid}:{uid}` | 活动进度 | 24h |
| `act:claim:{aid}:{uid}:{reward_key}` | 领奖标记 | 无 |
| `act:stock:{aid}` | 礼包库存 | 无 |

客户端 API 仅有 `Set/Get/Del/Decr/Ping`。文档中规划的 `online:`、`match:q:`、`room:`、限流等**尚未实现**，新增时需同步 SPEC。

钱包/支付幂等键（如 `pay:`、`act:`、`admin:`）写入 **MySQL `ledger`**，不是 Redis。

### MySQL / Redis 客户端

- 均使用连接池（`pool_size`，见 `server/conf/server.json`），支持失败自动重连并重试一次。
- MySQL：自建池 + **`ExecBind`/`QueryBind` 预处理参数绑定**（热路径优先）；遗留 `Exec`/`Query` 仅用于无参 SQL。
- Redis：自建连接池 + **hiredis**；命令失败重连该槽并重试一次。
- `LastError()` 为 thread_local，按调用线程返回。
- 业务侧勿用可用性门禁跳过读写：直接调用 `Exec`/`Query`/`Set`/`Get` 等，依据返回值与 `LastError()` 处理失败；客户端内部已有重连与单次重试。健康检查与仪表盘状态用 `Ping()`（如 `GET /health`、admin dashboard、`main` 启动日志）。
- **并发模型**：HTTP 与 WSS 分属独立 `io_context`（`worker.biz_threads` / `net.iocp_workers`）；账变/局记录/活动进度经 `AsyncWorker` 异步落库；`SessionHub`/`MemoryStore`/`RoomManager` 按 uid 或 room_id 分片锁。

## 编码约定

- **最小改动**：只改任务相关代码；不顺手大重构、不扩写无关文档。
- **协议变更**：先改 `proto/`，再同步 `server` 与 `game-web`；勿手写与 `.proto` 不一致的字段。
- **金钱与幂等**：余额变更必须走 `WalletService` + `ledger.idempotent_key`；禁止绕过账变直接改余额。
- **Redis**：当缓存用；断连或不可用时业务应可降级到 MySQL/内存（现有路径已按此设计）。
- **网络**：新连接逻辑挂在现有 IOCP/Beast 路径上；不要引入「一连接一线程」。
- **前端**：`game-web` / `admin-web` 为 Vue 3 + Vite；Admin UI 沿用 Element Plus，勿另起一套组件体系。
- **配置**：默认看 `server/conf/server.json`；TLS 见 `server.tls.json` 与 `force_tls`。
- **日志**：用现有 `PLOG_*`，不要引入新日志框架。

## 未交付 / 勿假装已完成

- 完整 Schannel TLS（当前有配置与部分 TLS 路径，非完整生产方案）
- 真实支付宝验签（沙箱占位）
- 冲榜活动
- 文档中未落地的 Redis 键（在线、匹配队列、房间元数据、限流等）

## 文档索引

| 文档 | 用途 |
|------|------|
| `docs/棋牌游戏服务端-需求文档.md` | 需求（SRS） |
| `docs/棋牌游戏服务端-SPEC.md` | 技术规格、表结构、协议、模块接口 |
| `docs/棋牌游戏服务端-M1计划.md` … `M6计划.md` | 里程碑范围与验收 |
| `README.md` | 本地启动与冒烟入口 |

改行为或契约时：优先对照当前里程碑计划与 SPEC 对应章节，再改代码与冒烟脚本。
