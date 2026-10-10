# Pandora — Agent 指南

棋牌游戏单体服务端（C++）+ Vue 网页客户端 + 运营后台。平台里程碑：**M6**（IOCP、报表、压测）。玩法：斗地主经典简单规则，以及杭州麻将（白板固定财神）。

需求冲突时以 `docs/棋牌游戏服务端-需求文档.md`（SRS）为准，技术实现以 `docs/棋牌游戏服务端-SPEC.md` 为准。杭州麻将以 `docs/杭州麻将-SPEC.md` 为准，规则说明见 `docs/杭州麻将规则.md`。跑胡子以 `docs/跑胡子-SPEC.md` 为准，规则说明见 `docs/跑胡子规则.md`。比鸡以 `docs/比鸡游戏-SPEC.md` 为准，需求见 `docs/比鸡游戏-需求文档.md`。

## 硬约束（不可偏离）

- **单体单进程**：模块间同步调用；无微服务、无消息队列拆分。
- **单机房单实例**：目标 CCU ≥ 20000；HTTP/WSS 走 IOCP，禁止按连接 detach 线程。
- **协议**：长连接帧 `uint32 LE len | uint32 LE msg_id | protobuf`；Admin / 部分短连接用 HTTPS+JSON。
- **货币**：金币 + 钻石；支付为支付宝 APP（当前沙箱）；无房卡、无冲榜。
- **首发玩法**：斗地主经典简单规则；匹配/对局自研。
- **存储**：MySQL 单主库（权威数据）；Redis 仅缓存/会话/开关，**可重建**。

## 仓库地图

```text
docs/                 # SRS / SPEC / 杭州麻将 / 跑胡子 / M1–M6 计划
proto/                # 唯一协议契约（.proto）
server/               # C++ pandora-server（MSVC / CMake）
  src/net/            # HTTP、WSS、帧、SessionHub、IOCP
  src/auth/ lobby/ match/ room/ game/
  src/game/           # GameRegistry + IRoomGame；game_id 2000/3000/4000/5000/6000
  src/game/ddz|hzmj|phz|fish|biji/  # 规则 Table + *_room_game 适配器
  src/wallet/ pay/ activity/ admin/ social/ bag/
  src/store/          # MysqlClient、RedisClient、MemoryStore
  conf/               # server.json / server.tls.json
  sql/                # schema.sql
  scripts/            # build.ps1、*_smoke.mjs、ccu_load.mjs
  tests/              # hzmj_*_test、phz_*_test、fish_*_test、biji_*_test、ddz_cards_test
game-web/             # Vue3；/hzmj-lab /ddz-lab /phz-lab /fish-lab /biji-lab
admin-web/            # Vue3 + Element Plus 运营后台（端口 5174）
docker-compose.yml    # MySQL 5.7 + Redis 6.2
```

玩法扩展：`msg_id = game_id * 100 + slot`（`game_id` 四位，1000–9999）；新玩法实现 `IRoomGame` 并在 `RegisterBuiltinGames` 注册，勿再往 `Room` 加平行字段。错误码与文案见 `errors.hpp`（`ErrMessage` / `SendError`）。

命名空间：`pandora`。日志宏：`PLOG_INFO` / `PLOG_WARN` / `PLOG_ERROR`。对局轨迹用 `LogRound`（`server/src/common/log.hpp`），行格式：

```text
round=<局号> src=server|client game=hzmj|ddz|phz|fish|biji room=<房间> uid=<玩家> seat=<座位> ev=<事件> <detail>
```

文件在进程工作目录 `logs/pandora_YYYY-MM-DD.log`（从 `server/` 启动即为 `server/logs/`）。客户端经 `C2S_ClientTrace`（9001）上报，服务端用 `RoomOf(uid)` 填房间号。座位类事件的 `uid` 取该座位玩家。

## 依赖与本地环境

| 组件 | 说明 |
|------|------|
| 构建 | CMake + Visual Studio 2022 x64，C++17+ |
| 包管理 | vcpkg：boost-asio/beast、nlohmann-json、protobuf、libmysql、redis-plus-plus、spdlog、openssl |
| MySQL | `127.0.0.1:3306`，用户/库 `pandora` / `pandora` |
| Redis | `redis://127.0.0.1:6379/0`，无密码 |
| 默认端口 | HTTP `8080`，WS `8081`；game-web `5173`；admin-web `5174` |

```powershell
docker compose up -d
powershell -ExecutionPolicy Bypass -File server/scripts/build.ps1
cd server\build\Release
.\pandora-server.exe
```

`pandora-server.exe` 正在运行时链接会 LNK1104。`build.ps1` 会先结束该进程；手动 `cmake --build` 时需先停掉进程。PowerShell 不用 `&&`，用 `;`。

冒烟（需服务已启动）：

```powershell
node server\scripts\m2_smoke.mjs
node server\scripts\m3_smoke.mjs
node server\scripts\admin_smoke.mjs
node server\scripts\activity_smoke.mjs
node server\scripts\hzmj_smoke.mjs
node server\scripts\phz_smoke.mjs
node server\scripts\fish_smoke.mjs
node server\scripts\biji_smoke.mjs
node server\scripts\ccu_load.mjs --target 200
```

规则测试（不必启动服务）：

```powershell
cmake --build server\build --config Release --target hzmj_table_test
cmake --build server\build --config Release --target hzmj_rules_test
cmake --build server\build --config Release --target phz_table_test
cmake --build server\build --config Release --target phz_rules_test
cmake --build server\build --config Release --target fish_math_test
cmake --build server\build --config Release --target fish_table_test
cmake --build server\build --config Release --target biji_hand_test
cmake --build server\build --config Release --target biji_table_test
server\build\Release\hzmj_table_test.exe
server\build\Release\phz_table_test.exe
server\build\Release\fish_math_test.exe
server\build\Release\fish_table_test.exe
server\build\Release\biji_hand_test.exe
server\build\Release\biji_table_test.exe
cd game-web
npm test
```

`GET /health` 应返回 `mysql:true, redis:true`。

Admin 默认超管：`admin` / `admin123`。

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

钱包/支付幂等键（如 `pay:`、`act:`、`admin:`、`hzmj:`）写入 **MySQL `ledger`**，不是 Redis。

### MySQL / Redis 客户端

- 均使用连接池（`pool_size`，见 `server/conf/server.json`），支持失败自动重连并重试一次。
- MySQL：自建池 + **`ExecBind`/`QueryBind` 预处理参数绑定**（热路径优先）；遗留 `Exec`/`Query` 仅用于无参 SQL。
- Redis：`redis-plus-plus` `ConnectionPool`；命令失败在同池重试一次（库在下次 fetch 时重连坏连接）；仅 `redis_==nullptr` 时单飞建池。
- `LastError()` 为 thread_local，按调用线程返回。
- 业务侧勿用可用性门禁跳过读写：直接调用 `Exec`/`Query`/`Set`/`Get` 等，依据返回值与 `LastError()` 处理失败；客户端内部已有重连与单次重试。健康检查与仪表盘状态用 `Ping()`（如 `GET /health`、admin dashboard、`main` 启动日志）。
- **并发模型**：HTTP 与 WSS 分属独立 `io_context`（`worker.biz_threads` / `net.iocp_workers`）；账变/局记录/活动进度经 `AsyncWorker` 异步落库；`SessionHub`/`MemoryStore`/`RoomManager` 按 uid 或 room_id 分片锁。

## 编码约定

- **最小改动**：只改任务相关代码；不顺手大重构、不扩写无关文档。
- **协议变更**：先改 `proto/`，再同步 `server` 与 `game-web`；勿手写与 `.proto` 不一致的字段。`game-web` 用 ts-proto 生成：`cd game-web; npm run proto:gen`（输出 `src/gen/`），`frame.ts` 仅保留帧封装与薄包装。
- **金钱与幂等**：余额变更必须走 `WalletService` + `ledger.idempotent_key`；禁止绕过账变直接改余额。
- **Redis**：当缓存用；断连或不可用时业务应可降级到 MySQL/内存（现有路径已按此设计）。
- **网络**：新连接逻辑挂在现有 IOCP/Beast 路径上；不要引入「一连接一线程」。
- **前端**：`game-web` / `admin-web` 为 Vue 3 + Vite；Admin UI 沿用 Element Plus，勿另起一套组件体系。含中文的 `.vue` 用脚本按 UTF-8 写入；直接替换容易把中文写成 `?`。
- **C++ 注释**：新增注释用 ASCII。源文件在代码页 936 下，中文 `//` 会触发 C4819 并吃掉下一行。
- **配置**：默认看 `server/conf/server.json`；TLS 见 `server.tls.json` 与 `force_tls`。实验室行牌超时见 `server.tls.json` 的 `play_timeout_s`。
- **日志**：用现有 `PLOG_*` / `LogRound`，不要引入新日志框架。
- **不要提交**：`game-web/tsconfig.tsbuildinfo`、`server/build/`、`logs/`。

## 杭州麻将（改牌桌前）

细节见 `docs/杭州麻将-SPEC.md` §7。实现时保持：

- 自摸只在摸牌之后（庄家起手 14 张、杠后补牌算摸牌）。`S2C_HzmjTurn.can_zimo` 为假时客户端不显示自摸；吃、碰后即使牌型已成型也须先出牌。
- 出牌阶段断线只标记托管，等到原倒计时结束再代打。鸣牌窗内断线立刻代过。
- 重连下发剩余秒，不重新 `ArmHzmjDeadline`，斗地主重连不调用 `BroadcastTurn`。
- 非目标：花牌、定缺、买码、一炮多响、翻财神、抽水以外的旁路结算。

## 未交付 / 勿假装已完成

- 完整 Schannel TLS（当前有配置与部分 TLS 路径，非完整生产方案）
- 真实支付宝验签（沙箱占位）
- 冲榜活动
- 杭州麻将独立重连消息 `6011`（当前复用 6001 / 6007 / 6005 / 6002）
- 文档中未落地的 Redis 键（在线、匹配队列、房间元数据、限流等）

## 文档索引

| 文档 | 用途 |
|------|------|
| `docs/棋牌游戏服务端-需求文档.md` | 需求（SRS） |
| `docs/棋牌游戏服务端-SPEC.md` | 技术规格、表结构、协议、模块接口、对局日志 |
| `docs/杭州麻将-SPEC.md` | 杭州麻将协议、托管、重连、计分 |
| `docs/杭州麻将规则.md` | 玩法说明（白板财神、爆头、三牢点炮） |
| `docs/跑胡子-SPEC.md` | 跑胡子协议、状态机、偎跑提、囤番结算 |
| `docs/跑胡子规则.md` | 跑胡子玩法说明（湖南经典默认档） |
| `docs/捕鱼游戏-SPEC.md` | 捕鱼协议、公式、TDD |
| `docs/比鸡游戏-需求文档.md` | 比鸡需求（SRS） |
| `docs/比鸡游戏-SPEC.md` | 比鸡协议、牌型、结算、TDD |
| `docs/棋牌游戏服务端-M1计划.md` … `M6计划.md` | 里程碑范围与验收 |
| `README.md` | 本地启动与冒烟入口 |

改行为或契约时：先改对应 SPEC（玩法规则同时改 `docs/杭州麻将规则.md` / `docs/跑胡子规则.md`），再改 `proto/`、服务端、`game-web` 与冒烟脚本。
