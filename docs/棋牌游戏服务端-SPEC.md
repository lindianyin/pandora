# 棋牌游戏服务端 — 技术规格说明书（SPEC）

| 项目 | 内容 |
|------|------|
| 文档版本 | V1.4 |
| 创建日期 | 2026-09-28 |
| 文档状态 | 修订（基于 SRS V1.17；增补 Admin 道具选择联动） |
| 依据文档 | [棋牌游戏服务端-需求文档.md](./棋牌游戏服务端-需求文档.md) |
| 适用范围 | 游戏服（C++/MSVC 单体）、**简单版网页客户端（Vue）**、运营后台（Vue）、协议与数据契约 |

本文档将已闭合需求落实为可开发的技术规格：目录结构、协议帧、消息 ID、表结构、配置默认值、状态机与模块接口、**网页客户端页面与联调要求**。需求冲突时以 **SRS V1.17** 为准，并回写修订本 SPEC。

---

## 1. 规格总览

### 1.1 交付物

| 交付物 | 技术选型 | 说明 |
|--------|----------|------|
| `pandora-server` | C++17+ / MSVC v143 | 单体游戏服可执行文件 |
| `game-web` | Vue 3 + Vite | **简单版网页游戏客户端**（联调/演示） |
| `admin-web` | Vue 3 + Vite | 运营后台前端 |
| `proto/` | proto3 | 服务端 + 网页客户端共用契约 |
| 配置 | JSON/YAML + 环境变量 | 含支付宝、TLS、DB 等 |

### 1.2 硬约束（不可偏离）

| ID | 约束 |
|----|------|
| S-01 | 单体单进程；模块间同步调用；无微服务、无 Kafka |
| S-02 | 单机房单实例；目标 CCU ≥ 20000 |
| S-03 | 游戏内长连接：WSS / TCP+TLS；帧：`uint32 LE len \| uint32 LE msg_id \| protobuf` |
| S-04 | HTTPS 短连接可用 JSON；Admin 仅 HTTPS+JSON |
| S-05 | 货币：金币 + 钻石；支付：支付宝 APP；无房卡、无冲榜活动；**道具入背包（非货币）** |
| S-06 | 首发玩法：斗地主经典简单规则；匹配/对局自研 |
| S-07 | MySQL 单主库，不读写分离；Redis 可重建 |
| S-08 | 交付简单版网页客户端：WSS+Protobuf，与服务端同一契约；三开浏览器可打完一局 |
| S-09 | 道具发放/消耗唯一入口为背包模块；类型：`qty` / `qty_ttl` / `ttl` |

### 1.3 建议仓库布局

```text
pandora/
├── docs/
│   ├── 棋牌游戏服务端-需求文档.md
│   └── 棋牌游戏服务端-SPEC.md
├── proto/                      # .proto 唯一契约源
│   ├── common.proto
│   ├── lobby.proto
│   ├── game_ddz.proto
│   ├── activity.proto
│   ├── social.proto            # 战绩推送/邮件/好友通知
│   └── bag.proto               # 背包变更推送（M8）
├── server/                     # C++ 游戏服
│   ├── CMakeLists.txt / pandora.sln
│   ├── src/
│   │   ├── main.cpp
│   │   ├── net/                # HTTP/WS/TCP+TLS、帧编解码
│   │   ├── auth/
│   │   ├── lobby/
│   │   ├── match/
│   │   ├── game/               # 对局框架 + ddz/
│   │   ├── wallet/
│   │   ├── bag/                # 背包与道具（FR-BAG）
│   │   ├── activity/
│   │   ├── social/
│   │   ├── admin/              # Admin REST
│   │   ├── pay/                # 支付宝
│   │   ├── common/             # 配置、日志、线程池、错误码
│   │   └── generated/          # protoc 生成（可 gitignore）
│   ├── conf/
│   └── tests/
├── game-web/                   # Vue 简单版网页游戏客户端
│   ├── package.json
│   └── src/
│       ├── views/              # login / lobby / table / activity / bag / record / friends / mail / rank
│       ├── net/                # WSS + 帧编解码
│       └── proto/              # 生成或引用 ../../proto
└── admin-web/                  # Vue 运营后台
    ├── package.json
    └── src/
```

---

## 2. 进程与线程模型

### 2.1 进程

- 一个 `pandora-server` 进程监听：
  - `https_port`：玩家 REST + 支付宝回调 + `/health`
  - `admin_https_port`：Admin API（可同端口不同 path 前缀，**推荐独立端口**便于防火墙）
  - `wss_port`：WebSocket TLS
  - `tcp_tls_port`：原生 TCP+TLS

### 2.2 线程建议

| 线程/池 | 职责 |
|---------|------|
| 主/网络 IO 线程（1..N） | accept、读写、帧解析、投递业务 |
| 业务 Worker 池 | 大厅/匹配/钱包/背包/活动/Admin 请求处理 |
| 对局 Tick 线程（1 或分片） | 房间定时器、倒计时、托管；**不跑慢 SQL** |
| 异步 IO 池 | 邮件、归档、统计、**过期道具清理**等非关键路径 |

原则：对局广播与状态变更在 Tick/业务线程内同步完成；禁止在 Tick 内阻塞等待远端。

### 2.3 优雅停机

1. 设置 `maintain=true`，拒绝新登录/匹配  
2. 等待在局结束（超时强制结算/散桌策略可配）  
3. 排空异步队列关键任务  
4. 关闭监听并退出  

---

## 3. 模块接口规格

### 3.1 模块依赖（进程内）

```text
net → auth → lobby/match/game/wallet/bag/activity/social/admin/pay
game → wallet（结算）
activity → wallet（货币发奖）+ bag（道具发奖）
social.Mail.Claim → wallet + bag（按 attach_json）
pay → wallet（充值到账）[+ bag 赠送道具，P1]
admin → 各模块配置与查询 API（含 bag grant/revoke）
```

### 3.2 玩法插件接口（C++ 概念）

```cpp
struct IGameLogic {
  virtual void OnPlayerEnter(SeatId) = 0;
  virtual void OnPlayerReady(SeatId, bool ready) = 0;
  virtual void OnGameStart() = 0;
  virtual Result OnPlayerAction(SeatId, const Action&) = 0; // Validate→Apply
  virtual void OnTimeout(SeatId) = 0;
  virtual void OnReconnect(SeatId, Viewer&) = 0;
  virtual SettleResult OnSettle() = 0;
  virtual void OnDestroy() = 0;
};
```

首发实现：`DdzClassicSimple`（`game_id = 1`）。

### 3.3 钱包接口（概念）

```text
Adjust(uid, currency, delta, biz_type, idempotent_key, remark) -> Result
  - 同事务更新余额 + 写 ledger
  - UNIQUE(idempotent_key) 防重
```

`currency`: `1=金币`, `2=钻石`  
`biz_type`: `game_settle | pay_recharge | exchange | activity_reward | gm_adjust | mail_reward | ...`

### 3.4 背包接口（概念）

```text
BagService
  ReloadDefs()                              // 从 MySQL 加载 item_define
  ListBag(uid, include_expired=false) -> items
  Grant(uid, item_id, quantity, expire_policy, idempotent_key, biz_type, ref_id) -> Result
  Consume(uid, item_id, quantity, prefer_expire_at?, idempotent_key, biz_type, ref_id) -> Result
  GetQuantity(uid, item_id) -> usable_qty   // 未过期可用量
```

`expire_policy`：
- `none`：数量道具，`expire_at = 9999-12-31 23:59:59.999`（哨兵不过期）
- `duration_sec=N`：自发放起 +N 秒
- `absolute=DATETIME`：显式到期

堆叠规则：
- `qty`：同 `uid+item_id`（`expire_at` 为不过期哨兵）一行合并 `quantity`
- `qty_ttl`：同 `uid+item_id+expire_at` 合并数量；不同到期时刻新开行；**不续期**
- `ttl`：同 `item_id` **仅一行**、`quantity=1`；再次发放按时长**续期**：`expire_at = max(现 expire, NOW) + duration×份数`；已过期则从 NOW 起加

幂等：`item_ledger.idempotent_key` UNIQUE。过期：**惰性**（List/Consume 判定）+ 可选异步归档。

奖励统一结构（活动/邮件/GM/充值赠送共用）：

```json
{
  "currency": 1,
  "amount": 500,
  "items": [{ "item_id": 1001, "quantity": 2, "expire_sec": 86400 }]
}
```

- 有 `currency`+`amount` → `wallet.Adjust`
- 有 `items[]` → 逐条 `bag.Grant`（子幂等键：`{parent_idem}:{item_id}:{i}`）

### 3.5 社交模块接口（概念）

```text
SocialService
  Record.OnRoundSettled(round)          // 结算后更新战绩汇总（可异步）
  Friend.List / Request / Accept / Reject / Remove
  Mail.Send(system|admin) / List / Read / Claim / Delete
  Rank.OnGoldChanged(uid, gold)         // 钱包变更后刷新 ZSET（可异步）
  Rank.SnapshotDaily / SnapshotWeekly   // 定时落库
```

依赖：`Record`/`Rank` 读 `game_round` / `player_profile`；`Mail.Claim` → `wallet.Adjust` + **`bag.Grant`（附件含道具时）**；推送经 `SessionHub`。

---

## 4. 网络与协议规格

### 4.1 二进制帧（WSS / TCP+TLS）

| 字段 | 类型 | 字节序 | 说明 |
|------|------|--------|------|
| length | uint32 | **小端** | `= 4 + len(body)`，即 `msg_id` 4 字节 + body 长度 |
| msg_id | uint32 | **小端** | 消息类型 |
| body | bytes | — | Protobuf 序列化载荷 |

约束：

- 单帧 `length` 上限默认 `1 MiB`（可配），超限断连  
- 粘包：按 length 切帧；半包缓存  
- WSS：每帧可对应一个 WebSocket Binary Message（推荐一整业务帧一 WS message）  
- TCP：连续字节流按帧解析  

### 4.2 连接与鉴权

1. HTTPS `POST /api/v1/auth/login` → `access_token` + `uid`  
2. 建立 WSS/TCP+TLS 后首包必须 `C2S_Auth`（msg_id 见下表），携带 token  
3. 鉴权失败关闭连接；成功绑定 `conn_id ↔ uid`  
4. 心跳：客户端每 `heartbeat_interval_s`（默认 15）发 `C2S_Heartbeat`；服务端超时 `heartbeat_timeout_s`（默认 45）踢线并触发托管  

### 4.3 msg_id 分段

| 范围 | 用途 |
|------|------|
| 1 – 999 | 系统/公共（鉴权、心跳、踢人、错误、维护） |
| 1000 – 1999 | 大厅/房间/匹配 |
| 2000 – 2999 | 斗地主对局 |
| 3000 – 3999 | 活动推送 |
| 4000 – 4999 | 社交（邮件通知、好友状态等，P1） |
| 5000 – 5999 | 背包推送（FR-BAG） |
| 6000 – 6999 | 杭州麻将对局（详见 `docs/杭州麻将-SPEC.md`） |
| 9000 – 9999 | 调试/保留。`9001` `C2S_ClientTrace`：客户端对局轨迹，服务端写入日志，行首 `round=<局号>` |

#### 4.3.1 公共消息

| msg_id | 方向 | proto message | 说明 |
|--------|------|---------------|------|
| 1 | C→S | `C2S_Auth` | token 鉴权 |
| 2 | S→C | `S2C_AuthResult` | 成功/失败码 |
| 3 | C→S | `C2S_Heartbeat` | 心跳 |
| 4 | S→C | `S2C_HeartbeatAck` | 可选 |
| 5 | S→C | `S2C_Kick` | 踢下线 |
| 6 | S→C | `S2C_Maintain` | 维护通知 |
| 7 | S→C | `S2C_Error` | 通用错误 |

#### 4.3.2 大厅/房间/匹配

| msg_id | 方向 | proto message | 说明 |
|--------|------|---------------|------|
| 1001 | C→S | `C2S_GetLobby` | 拉场次列表 |
| 1002 | S→C | `S2C_LobbyInfo` | 场次信息 |
| 1010 | C→S | `C2S_QuickMatch` | 快速匹配 |
| 1011 | C→S | `C2S_CancelMatch` | 取消匹配 |
| 1012 | S→C | `S2C_MatchStatus` | 匹配中/成功/超时 |
| 1020 | C→S | `C2S_JoinRoom` | 加入房间 |
| 1021 | C→S | `C2S_LeaveRoom` | 离开 |
| 1022 | C→S | `C2S_Ready` | 准备/取消 |
| 1023 | S→C | `S2C_RoomState` | 房间快照 |
| 1030 | C→S | `C2S_Chat` | 房间聊天（P1） |

#### 4.3.3 斗地主

| msg_id | 方向 | proto message | 说明 |
|--------|------|---------------|------|
| 2001 | S→C | `S2C_DdzGameStart` | 发牌/身份（仅己方手牌）；字段 `round_id` 供客户端显示局号 |
| 2002 | S→C | `S2C_DdzTurn` | 轮到谁、阶段 |
| 2003 | C→S | `C2S_DdzBid` | 叫分 0/1/2/3 |
| 2004 | S→C | `S2C_DdzBidBroadcast` | 叫分广播 |
| 2005 | C→S | `C2S_DdzPlay` | 出牌或过 |
| 2006 | S→C | `S2C_DdzPlayBroadcast` | 出牌广播 |
| 2007 | S→C | `S2C_DdzSettle` | 结算 |
| 2008 | S→C | `S2C_DdzReconnect` | 重连快照 |

#### 4.3.4 活动

| msg_id | 方向 | proto message | 说明 |
|--------|------|---------------|------|
| 3001 | S→C | `S2C_ActivityUpdate` | 进度/可领变更 |

#### 4.3.5 社交（P1，邮件/好友推送）

| msg_id | 方向 | proto message | 说明 |
|--------|------|---------------|------|
| 4001 | S→C | `S2C_MailNotify` | 新邮件到达（摘要） |
| 4002 | S→C | `S2C_FriendNotify` | 好友申请/同意/上线（可配） |

#### 4.3.6 背包（FR-BAG，P0 推送为 P1）

| msg_id | 方向 | proto message | 说明 |
|--------|------|---------------|------|
| 5001 | S→C | `S2C_BagUpdate` | 背包变更（发放/消耗/过期摘要） |

#### 4.3.7 对局轨迹（调试）

| msg_id | 方向 | proto message | 说明 |
|--------|------|---------------|------|
| 9001 | C→S | `C2S_ClientTrace` | 客户端对局轨迹。字段：`round_id`、`seat_id`、`event`、`detail`、`game`（`hzmj` 或 `ddz`）。不带房间号，服务端用 `RoomOf(uid)` 填入日志 |

> `.proto` 字段定义落在 `proto/`；本 SPEC 锁定 msg_id 与消息名。变更字段遵守 proto3 兼容规则。日志行格式见 §15.1。

### 4.4 防重放（长连接）

- 每连接维护递增 `client_seq`（可选，P1）  
- 或关键写操作带 `nonce`（短连接支付/领奖必须幂等键）  

---

## 5. HTTPS API 规格

### 5.1 通用约定

- Content-Type: `application/json; charset=utf-8`  
- 玩家鉴权：`Authorization: Bearer <access_token>`  
- Admin 鉴权：独立 Bearer（admin token）  
- 统一响应：

```json
{ "code": 0, "message": "ok", "data": {}, "trace_id": "..." }
```

- 分页：`page`（从 1）、`page_size`（默认 20，最大 100）→ `total`

### 5.2 错误码（节选）

| code | 含义 |
|------|------|
| 0 | 成功 |
| 1001 | 未登录/Token 无效 |
| 1002 | 无权限 |
| 1003 | 参数错误 |
| 1004 | 资源不存在 |
| 2001 | 金币不足 |
| 2002 | 重复操作（幂等命中） |
| 2003 | 活动不可领 |
| 2101 | 好友申请非法 / 已是好友 / 人数达上限 |
| 2102 | 邮件不存在或已领取 |
| 2103 | 排行榜类型/周期非法 |
| 2201 | 道具不存在或已下架 |
| 2202 | 道具数量不足 |
| 2203 | 道具已过期 |
| 2204 | 道具类型/参数不匹配（如 qty 传了非法 expire） |
| 3001 | 支付下单失败 |
| 3002 | 订单状态非法 |
| 5000 | 维护中 |
| 5001 | 账号封禁 |
| 9999 | 内部错误 |

### 5.3 玩家 API

| Method | Path | 说明 | 鉴权 |
|--------|------|------|------|
| POST | `/api/v1/auth/login` | 登录（guest/phone…） | 否 |
| POST | `/api/v1/auth/refresh` | 刷新 token | 是 |
| GET | `/api/v1/player/profile` | 资料+余额 | 是 |
| GET | `/api/v1/record/summary` | 个人战绩汇总（FR-SOC-01） | 是 |
| GET | `/api/v1/record/recent` | 近期对局列表（FR-SOC-01） | 是 |
| GET | `/api/v1/friend/list` | 好友列表（FR-SOC-02） | 是 |
| POST | `/api/v1/friend/request` | 发起好友申请 | 是 |
| POST | `/api/v1/friend/accept` | 同意申请 | 是 |
| POST | `/api/v1/friend/reject` | 拒绝申请 | 是 |
| POST | `/api/v1/friend/remove` | 删除好友 | 是 |
| GET | `/api/v1/mail/list` | 邮件列表（FR-SOC-03） | 是 |
| POST | `/api/v1/mail/{id}/read` | 标记已读 | 是 |
| POST | `/api/v1/mail/{id}/claim` | 领取附件（幂等） | 是 |
| POST | `/api/v1/mail/{id}/delete` | 删除邮件 | 是 |
| GET | `/api/v1/rank/{period}` | 排行榜 `daily`/`weekly`（FR-SOC-04） | 是 |
| GET | `/api/v1/activity/list` | 活动列表 | 是 |
| GET | `/api/v1/activity/{id}/progress` | 进度 | 是 |
| POST | `/api/v1/activity/{id}/claim` | 领奖（body: `reward_key`） | 是 |
| GET | `/api/v1/pay/products` | 充值档位 | 是 |
| POST | `/api/v1/pay/alipay/create` | 创建 APP 支付订单 | 是 |
| POST | `/api/v1/pay/alipay/notify` | 支付宝异步通知 | 验签 |
| POST | `/api/v1/wallet/exchange` | 钻石→金币 | 是 |
| GET | `/api/v1/bag/list` | 背包列表（FR-BAG；`include_expired=0|1`） | 是 |
| POST | `/api/v1/bag/use` | 消耗/使用道具（body: `item_id,quantity,idempotent_key`；无玩法用途时可仅供演示扣减） | 是 |
| GET | `/health` | 健康检查 | 否 |

#### 登录请求示例

```json
{ "type": "guest", "device_id": "xxx", "channel": "official" }
```

#### 创建支付订单响应 data

```json
{
  "order_id": "P202609230001",
  "product_id": 1,
  "amount_fen": 600,
  "alipay_order_str": "..." 
}
```

客户端用 `alipay_order_str` 调起支付宝 APP SDK。

### 5.4 Admin API

前缀 `/admin/v1`。角色：`super` / `ops` / `cs`。

| Method | Path | 说明 | 最低角色 |
|--------|------|------|----------|
| POST | `/admin/v1/auth/login` | 登录 | — |
| GET | `/admin/v1/dashboard` | 看板 | cs |
| GET | `/admin/v1/players` | 查询玩家 | cs |
| POST | `/admin/v1/players/{uid}/kick` | 踢下线 | ops |
| POST | `/admin/v1/players/{uid}/ban` | 封禁 | ops |
| POST | `/admin/v1/players/{uid}/unban` | 解封 | ops |
| POST | `/admin/v1/wallet/adjust` | 补发/扣币（需 `idempotent_key`） | ops |
| GET | `/admin/v1/wallet/ledgers` | 账变查询 | cs |
| GET | `/admin/v1/rounds` | 对局查询 | cs |
| GET/PUT | `/admin/v1/rooms/templates` | 场次配置 | ops |
| CRUD | `/admin/v1/activities` | 活动（奖励可含道具） | ops |
| POST | `/admin/v1/activities/{id}/enable` | 活动上架/下架 | ops |
| POST | `/admin/v1/announce` | 公告 | ops |
| POST | `/admin/v1/mail/send` | 系统邮件（附件可含金币/钻石/道具） | ops |
| GET | `/admin/v1/mail` | 邮件发送记录查询 | cs |
| GET | `/admin/v1/rank/snapshot` | 排行快照查询 | cs |
| POST | `/admin/v1/ops/maintain` | 维护开关 | super |
| CRUD | `/admin/v1/pay/products` | 充值档位（可含赠送道具） | ops |
| POST | `/admin/v1/pay/products/{id}/enable` | 充值档位上架/下架 | ops |
| GET | `/admin/v1/pay/orders` | 订单 | cs |
| CRUD | `/admin/v1/items` | 道具定义（qty/qty_ttl/ttl） | ops |
| POST | `/admin/v1/items/{id}/enable` | 道具定义上架/下架 | ops |
| GET | `/admin/v1/players/{uid}/bag` | 玩家背包 | cs |
| GET | `/admin/v1/item/ledgers` | 道具流水（`uid`/`item_id`） | cs |
| POST | `/admin/v1/bag/grant` | 人工发放道具（`idempotent_key`） | ops |
| POST | `/admin/v1/bag/revoke` | 人工回收/扣减道具 | ops |
| GET | `/admin/v1/audit` | 审计 | super |

高危写操作：前端二次确认即可，**无双人审批**；服务端写 `admin_audit`。

---

## 6. 数据规格

### 6.1 MySQL 表（核心）

> 引擎 InnoDB，字符集 `utf8mb4`。下列为逻辑规格，实现时可微调索引名。

#### `user`

| 列 | 类型 | 说明 |
|----|------|------|
| uid | BIGINT PK | 用户 ID |
| account_type | TINYINT | 1 guest 2 phone … |
| open_id | VARCHAR(128) | 外部/设备标识，UNIQUE(account_type, open_id) |
| phone | VARCHAR(32) NOT NULL DEFAULT '' | 空串=未绑定 |
| status | TINYINT | 0 正常 1 封禁 |
| created_at / updated_at | DATETIME(3) | |

#### `player_profile`

| 列 | 类型 | 说明 |
|----|------|------|
| uid | BIGINT PK | |
| nickname | VARCHAR(64) | |
| avatar | VARCHAR(256) | |
| gold | BIGINT | 金币，≥0 |
| diamond | BIGINT | 钻石，≥0 |
| level | INT | 默认 1 |

#### `ledger`

| 列 | 类型 | 说明 |
|----|------|------|
| id | BIGINT PK AI | |
| uid | BIGINT | IDX |
| currency | TINYINT | 1 金 2 钻 |
| delta | BIGINT | 可负 |
| balance_after | BIGINT | |
| biz_type | VARCHAR(32) | |
| idempotent_key | VARCHAR(64) | **UNIQUE** |
| ref_id | VARCHAR(64) | round_id/order_id… |
| created_at | DATETIME(3) | IDX |

#### `room_template`

| 列 | 类型 | 说明 |
|----|------|------|
| id | INT PK | |
| game_id | INT | 1=斗地主 |
| name | VARCHAR(64) | 如「初级场」 |
| base_score | INT | 底分，默认 100 |
| rake_bp | INT | 抽水万分比，默认 500=5% |
| min_gold / max_gold | BIGINT | 准入；max=0 表示无上限 |
| enabled | TINYINT | |

#### `game_round`

| 列 | 类型 | 说明 |
|----|------|------|
| round_id | BIGINT PK | 雪花/序列 |
| room_id | BIGINT | |
| game_id | INT | |
| template_id | INT | |
| players_json | JSON | uid/seat/结果 |
| base_score | INT | |
| multiplier | INT | |
| started_at / ended_at | DATETIME(3) NOT NULL | 结算写入时双端同设 |

> `players_json` 元素约定（NOT NULL 对象数组）：
> `[{ "uid":1, "seat_id":0, "delta":100, "is_landlord":true }, ...]`  
> 战绩列表优先扫本表；汇总表 `player_stats` 做加速。

#### `player_stats`（FR-SOC-01）

| 列 | 类型 | 说明 |
|----|------|------|
| uid | BIGINT PK | |
| total_rounds | INT NOT NULL DEFAULT 0 | 总局数 |
| win_rounds | INT NOT NULL DEFAULT 0 | 胜局 |
| lose_rounds | INT NOT NULL DEFAULT 0 | 负局 |
| landlord_rounds | INT NOT NULL DEFAULT 0 | 地主局数 |
| gold_win_sum | BIGINT NOT NULL DEFAULT 0 | 累计赢金（仅正 delta 之和） |
| gold_lose_sum | BIGINT NOT NULL DEFAULT 0 | 累计输金（|负 delta| 之和） |
| updated_at | DATETIME(3) NOT NULL | |

结算成功后：`INSERT ... ON DUPLICATE KEY UPDATE` 递增；失败不落。

#### `friend_request`（FR-SOC-02）

| 列 | 类型 | 说明 |
|----|------|------|
| id | BIGINT PK AI | |
| from_uid | BIGINT NOT NULL | |
| to_uid | BIGINT NOT NULL | |
| status | TINYINT NOT NULL DEFAULT 0 | 0 待处理 1 已同意 2 已拒绝 3 已过期 |
| created_at | DATETIME(3) NOT NULL | |
| updated_at | DATETIME(3) NOT NULL | |
| UNIQUE | `(from_uid, to_uid)` where status=0 | 实现可用普通 UNIQUE(from,to) + 业务校验 |

#### `friendship`（FR-SOC-02）

| 列 | 类型 | 说明 |
|----|------|------|
| uid_low | BIGINT NOT NULL | min(uid_a, uid_b) |
| uid_high | BIGINT NOT NULL | max(uid_a, uid_b) |
| created_at | DATETIME(3) NOT NULL | |
| PRIMARY KEY | `(uid_low, uid_high)` | 无向边去重 |

约束：单用户好友上限默认 **100**（配置 `social.friend_max`）；禁止加自己。

#### `mail`（FR-SOC-03）

| 列 | 类型 | 说明 |
|----|------|------|
| id | BIGINT PK AI | |
| to_uid | BIGINT NOT NULL | `0`=全服模板展开时写具体 uid；发送任务另表可选 |
| title | VARCHAR(128) NOT NULL | |
| body | VARCHAR(1024) NOT NULL DEFAULT '' | |
| attach_json | JSON NOT NULL | 空 `{}`；货币例 `{"currency":1,"amount":500}`；道具例 `{"items":[{"item_id":1001,"quantity":1,"expire_sec":86400}]}`；可同时含货币与道具 |
| status | TINYINT NOT NULL DEFAULT 0 | 0 未读 1 已读 2 已领附件 3 已删 |
| expire_at | DATETIME(3) NOT NULL | 默认创建+30 天 |
| created_at | DATETIME(3) NOT NULL | |
| KEY | `(to_uid, status, id)` | |

领取附件幂等键：`mail:{mail_id}:{uid}` → 货币走 `wallet.Adjust`（`biz_type=mail_reward`）；道具走 `bag.Grant`（子键 `mail:{mail_id}:{uid}:item:{item_id}`）。

#### `mail_send_log`（Admin）

| 列 | 类型 | 说明 |
|----|------|------|
| id | BIGINT PK AI | |
| admin_id | INT NOT NULL | |
| scope | VARCHAR(16) NOT NULL | `uids` / `all` |
| target_json | JSON NOT NULL | uid 列表或 `{}` |
| title | VARCHAR(128) NOT NULL | |
| body | VARCHAR(1024) NOT NULL DEFAULT '' | |
| attach_json | JSON NOT NULL | |
| created_at | DATETIME(3) NOT NULL | |

#### `rank_snapshot`（FR-SOC-04）

| 列 | 类型 | 说明 |
|----|------|------|
| id | BIGINT PK AI | |
| period | VARCHAR(16) NOT NULL | `daily` / `weekly` |
| period_key | VARCHAR(16) NOT NULL | `20260928` / `2026W39` |
| uid | BIGINT NOT NULL | |
| score | BIGINT NOT NULL | 排行分（默认=当前金币快照或周期内净赢金，见 §10.4） |
| rank_no | INT NOT NULL | 名次 |
| created_at | DATETIME(3) NOT NULL | |
| UNIQUE | `(period, period_key, uid)` | |

#### `activity_define` / `activity_progress` / `activity_claim`

- `activity_define`：type(`sign`/`task`/`gift`)、规则 JSON（**奖励可含 `items[]`**）、时间窗（`start_at`/`end_at` NOT NULL；哨兵 `1970-01-01` / `9999-12-31` 表示无窗）、enabled  
- `activity_progress`：PK(`activity_id`,`uid`)，progress JSON，updated_at  
- `activity_claim`：UNIQUE(`activity_id`,`uid`,`reward_key`)

#### `item_define`（FR-BAG）

| 列 | 类型 | 说明 |
|----|------|------|
| id | INT PK AI | `item_id` |
| name | VARCHAR(64) NOT NULL | |
| icon | VARCHAR(256) NOT NULL DEFAULT '' | |
| kind | VARCHAR(16) NOT NULL | `qty` / `qty_ttl` / `ttl` |
| stackable | TINYINT NOT NULL DEFAULT 1 | 是否允许堆叠 |
| default_expire_sec | INT NOT NULL DEFAULT 0 | 默认时效秒；`qty` 固定 0；`ttl`/`qty_ttl`>0 时发放可用 |
| tag | VARCHAR(32) NOT NULL DEFAULT '' | 业务标签，如 `avatar_frame` / `ticket` |
| enabled | TINYINT NOT NULL DEFAULT 1 | 上下架 |
| created_at / updated_at | DATETIME(3) NOT NULL | |

#### `bag_item`（FR-BAG）

| 列 | 类型 | 说明 |
|----|------|------|
| id | BIGINT PK AI | 持有行 ID |
| uid | BIGINT NOT NULL | |
| item_id | INT NOT NULL | |
| quantity | BIGINT NOT NULL | ≥0；耗尽可删行或留 0（实现选删行） |
| expire_at | DATETIME(3) NOT NULL | `qty` 用哨兵 `9999-12-31 23:59:59.999` |
| updated_at | DATETIME(3) NOT NULL | |
| UNIQUE | `(uid, item_id, expire_at)` | 堆叠键 |
| KEY | `(uid, item_id)` | |

#### `item_ledger`（FR-BAG）

| 列 | 类型 | 说明 |
|----|------|------|
| id | BIGINT PK AI | |
| uid | BIGINT NOT NULL | |
| item_id | INT NOT NULL | |
| delta | BIGINT NOT NULL | 可负 |
| quantity_after | BIGINT NOT NULL | 该堆叠行操作后数量 |
| expire_at | DATETIME(3) NOT NULL | 本行对应到期 |
| biz_type | VARCHAR(32) NOT NULL | `activity_reward`/`mail_reward`/`gm_grant`/`gm_revoke`/`bag_use`/`pay_gift`… |
| idempotent_key | VARCHAR(64) NOT NULL | UNIQUE |
| ref_id | VARCHAR(64) NOT NULL DEFAULT '' | |
| created_at | DATETIME(3) NOT NULL | |
| KEY | `(uid, created_at)` | |

#### `pay_product`

| 列 | 类型 | 说明 |
|----|------|------|
| id | INT PK | |
| amount_fen | INT | 分 |
| diamond | INT | |
| gift_diamond | INT | 默认 0 |
| gift_items_json | JSON NOT NULL | 默认 `[]`；例 `[{"item_id":1001,"quantity":1,"expire_sec":0}]`（P1） |
| sort | INT | |
| enabled | TINYINT | |

#### `pay_order`

| 列 | 类型 | 说明 |
|----|------|------|
| order_id | VARCHAR(32) PK | |
| uid | BIGINT | IDX |
| product_id | INT | |
| amount_fen | INT | |
| status | TINYINT | 0 待支付 1 成功 2 失败 3 关闭 |
| alipay_trade_no | VARCHAR(64) NOT NULL DEFAULT '' | |
| idempotent_paid | TINYINT | 到账标记 |
| created_at | DATETIME(3) NOT NULL | |
| paid_at | DATETIME(3) NOT NULL | 哨兵 `1970-01-01`=未支付 |

状态机：`0 → 1`（回调成功且验签）或 `0 → 2/3`；**仅 0→1 触发加钻一次**。

#### `admin_user` / `admin_role` / `admin_audit`

标准 RBAC + 审计（who/when/action/target/before/after/ip）。`before_json`/`after_json` 为 JSON NOT NULL（空用 `{}`）。

> 约定：表结构**不使用可空列**；“无值”用空串或上述哨兵时间/空 JSON 表示。迁移见 `server/sql/migrate_drop_nulls.sql`。
### 6.2 Redis Key

| Key | 类型 | TTL | 说明 |
|-----|------|-----|------|
| `sess:{token}` | HASH/STRING | access 过期 | uid、设备 |
| `online:{uid}` | STRING | 心跳刷新 | conn 元数据 |
| `match:q:{template_id}` | LIST/ZSET | — | 匹配队列 |
| `room:{room_id}` | HASH | 房间生命周期 | 元数据（可选） |
| `act:prog:{aid}:{uid}` | STRING/HASH | 可较长 | 进度缓存 |
| `act:stock:{aid}` | STRING | — | 库存原子减 |
| `cfg:room_templates` | STRING | 主动失效 | 场次缓存 |
| `rl:{uid}:{action}` | STRING | 窗口 | 限流 |
| `friend:{uid}` | SET | 可重建 | 好友 uid 集合缓存（可选） |
| `mail:unread:{uid}` | STRING | 24h | 未读数缓存（可选） |
| `rank:gold:daily:{yyyymmdd}` | ZSET | 48h | 日榜：member=uid，score=见 §10.4 |
| `rank:gold:weekly:{yyyy}W{ww}` | ZSET | 16d | 周榜 |
| `rank:me:{period}:{uid}` | STRING | 同榜 TTL | 个人名次缓存（可选） |
| `bag:ver:{uid}` | STRING | 1h | 背包版本号（可选，推送/缓存失效） |

原则：余额以 MySQL 为准；Redis 余额缓存若使用，必须以 DB 事务成功后再写。排行 ZSET **可重建**（从 `player_profile.gold` 或 `rank_snapshot` 回灌）。背包以 MySQL `bag_item` 为准，Redis 仅可选缓存。

---

## 7. 斗地主简单规则规格

### 7.1 常量默认（可被 `room_template`/后台覆盖）

| 项 | 默认 |
|----|------|
| 人数 | 3 |
| 牌副 | 1 副 54 张 |
| 底分 | 100 |
| 抽水 | 5%（`rake_bp=500`） |
| 叫分倒计时 | 15s |
| 行牌倒计时 | 20s |
| 叫分选项 | 0 不叫 / 1 / 2 / 3；最高者成为地主；皆不叫则重新发牌（最多重发 N 次，默认 3，其后强制随机地主叫 1） |

### 7.2 状态机

```text
WaitReady → Deal → Bid → Play → Settle → (WaitReady | Destroy)
```

| 状态 | 行为 |
|------|------|
| WaitReady | 满员且全员 Ready → Deal |
| Deal | 每人 17 张，剩 3 张底牌；进入 Bid |
| Bid | 逆时针叫分；确定地主后亮底牌归地主；倍数初值=叫分 |
| Play | 地主先出；校验牌型；炸弹/王炸翻倍；出完进入 Settle |
| Settle | 计算输赢金币、抽水、写 ledger、推送 `S2C_DdzSettle`、通知活动与战绩汇总 |

### 7.3 牌型（首发）

单张、对子、三张、三带一、三带二、单顺（≥5）、双顺（≥3 对）、三顺/飞机（及带翅膀的简单实现）、四带二（可选开关，默认开）、炸弹、王炸。

校验：**仅服务端**判定；客户端展示用。

### 7.4 结算公式（简单）

```text
base = base_score
mult = bid_score * (2 ^ bomb_count) * (spring ? 2 : 1)   // spring 可选，默认启用
stake = base * mult

地主赢：每个农民支付 stake；地主得 2*stake
地主输：地主支付每个农民 stake

抽水：从赢家所得中扣 rake（向下取整），入系统账户或记流水 biz_type=rake
```

幂等键：`settle:{round_id}:{uid}`。

### 7.5 断线与托管

- 断线：标记托管。斗地主在 Tick 上可立即代打（叫分代 0、行牌代最小牌或过），到点同样代打  
- 重连：下发 `S2C_DdzReconnect`（己方手牌 + 公共信息，不发他人手牌）。`timeout_s` 为距原截止点的剩余秒（毫秒向上取整），**不**重新广播完整倒计时、不拨满截止点  
- 主动取消托管：重连即清除该座位托管标记  
- 杭州麻将的断线、鸣牌窗代过与剩余倒计时见 `docs/杭州麻将-SPEC.md` §7.2  

---

## 8. 钱包、支付、兑换

### 8.1 账变

- 所有余额变更走 `Adjust`  
- `gold/diamond` 不允许扣成负；不足返回 `2001`  

### 8.2 钻石兑换金币

- 默认：`1 diamond → 1000 gold`（配置键 `exchange.diamond_to_gold`）  
- API：`POST /api/v1/wallet/exchange` `{ "diamond": n }`  
- 幂等键：`ex:{uid}:{client_order_id}`（客户端生成）或服务端按请求去重窗口  

### 8.3 支付宝 APP 支付

1. 客户端拉档位 → 选 `product_id` → `create`  
2. 服务端写 `pay_order(status=0)`，调用支付宝 SDK/API 生成 `orderStr`  
3. 客户端调起 APP 支付  
4. 支付宝 `notify` → 验签 → 校验金额 → `status=1` → `Adjust(+diamond)`（幂等：`pay:{order_id}`）→ 若档位配置 `gift_items_json` 非空则 `bag.Grant`（幂等：`pay:{order_id}:item:{id}`，P1）  
5. 商户参数全部配置化：`app_id`、私钥、公钥、`gateway`、`notify_url`、`sandbox` 开关  

---

## 9. 活动规格

### 9.1 类型

| type | 说明 | 进度触发 |
|------|------|----------|
| `sign` | 每日签到 | 登录/签到领取 |
| `task` | 对局任务（如完成 N 局） | 结算成功实时 +N |
| `gift` | 限时礼包（可库存） | 购买/领取 |

不含冲榜。

奖励 JSON 示例（`rules_json` / 领奖结果）：

```json
{
  "reward_key": "daily",
  "reward": {
    "currency": 1,
    "amount": 500,
    "items": [{ "item_id": 1001, "quantity": 1, "expire_sec": 0 }]
  }
}
```

`expire_sec=0` 且道具为 `qty`：不过期；为 `ttl`/`qty_ttl`：用 `item_define.default_expire_sec`。

### 9.2 实时进度

```text
Settle 成功 → Activity.OnGameSettled(uid, template_id, ...) 同步更新
           → 若进度变化 → S2C_ActivityUpdate
```

领奖：校验 → 库存 CAS → `wallet.Adjust`（若有货币）→ **`bag.Grant`（若有道具）** → 写 `activity_claim`。

幂等键：`act:{aid}:{uid}:{reward_key}`（道具子键追加 `:item:{item_id}`）。

---

## 10. 社交与战绩规格（FR-SOC）

> 对应 SRS §3.8。实现落点：`server/src/social/`、`proto/social.proto`、玩家 REST 见 §5.3。  
> 列约定仍遵守「无可空列」。

### 10.1 FR-SOC-01 个人战绩 / 近期对局（P0）

**数据流**

```text
Ddz Settle → Admin.RecordRound(game_round)
           → Social.Record.OnRoundSettled → 更新 player_stats（AsyncWorker）
```

**查询**

| API | 行为 |
|-----|------|
| `GET /api/v1/record/summary` | 读 `player_stats`；无行则返回全 0 |
| `GET /api/v1/record/recent?page=&page_size=` | `game_round` 中 `players_json` 含本 uid，按 `ended_at DESC` |

**summary data 示例**

```json
{
  "total_rounds": 12,
  "win_rounds": 7,
  "lose_rounds": 5,
  "win_rate_bp": 5833,
  "landlord_rounds": 4,
  "gold_win_sum": 120000,
  "gold_lose_sum": 80000
}
```

`win_rate_bp`：胜率万分比，`total_rounds=0` 时为 0。

**recent item 示例**

```json
{
  "round_id": 10001,
  "template_id": 1,
  "ended_at": "2026-09-28 12:00:00.000",
  "base_score": 100,
  "multiplier": 2,
  "delta_gold": 190,
  "is_landlord": true,
  "result": "win"
}
```

从 `players_json` 解析本 uid 的 `delta`；`result`=`win|lose|draw`（delta=0 为 draw）。

### 10.2 FR-SOC-02 好友（P1）

**状态机（申请）**

```text
none → (request) pending → accept → friendship
                         → reject → 结束
                         → 超时(默认 7d) → expired
```

**规则**

- 双向好友；存无向边 `(uid_low, uid_high)`  
- 上限 `social.friend_max`（默认 100）  
- 已是好友 / 有 pending / 自己 → `2101`  
- 同意后删 pending，写 `friendship`，可选推送 `S2C_FriendNotify`  

**list data**：`[{ "uid", "nickname", "online": bool }]`；`online` 查 `SessionHub`（或未来 `online:{uid}`）。

### 10.3 FR-SOC-03 邮件 / 系统通知（P1）

**通道**

| 来源 | 说明 |
|------|------|
| Admin `POST /admin/v1/mail/send` | 运营发奖/通知；写 `mail_send_log` + 逐 uid 插 `mail`（全服异步批次） |
| 系统 | 维护结束、大额账变等（可选，首发可仅 Admin） |

**玩家行为**：列表（过滤 `status!=3` 且未过期）→ 已读 → 领附件（货币与/或道具）→ 删除。  
**推送**：入库后若在线，发 `S2C_MailNotify{ mail_id, title, has_attach }`。  
**过期**：`expire_at` 后不可领；异步清理或查询时过滤。  
**附件**：`attach_json` 按 §3.4 奖励结构解析；领取时分别调钱包与背包。

### 10.4 FR-SOC-04 排行榜日/周（P1）

**计分（首发默认）**

- **日榜 / 周榜 score = 玩家当前金币**（`player_profile.gold`）  
- 变更路径：`Wallet.Adjust` 成功且 currency=金币 → 异步 `ZADD` 两个 ZSET  
- 备选（配置 `rank.score_mode`）：`net_win`=周期内结算净赢金（需额外累加器，二期）

**Redis**

- `rank:gold:daily:{yyyymmdd}`、`rank:gold:weekly:{yyyy}W{ww}`（ISO 周）  
- `ZREVRANGE 0 99 WITHSCORES` 取 Top100  
- 个人名次：`ZREVRANK` +1  

**落库**

- 每日 00:05、每周一 00:10（可配）将 TopN（默认 100）写入 `rank_snapshot`  
- 旧 ZSET 按 TTL 过期；快照保留 ≥ 90 天（运维归档）

**API** `GET /api/v1/rank/{period}?limit=50`

```json
{
  "period": "daily",
  "period_key": "20260928",
  "list": [{ "rank": 1, "uid": 2, "nickname": "A", "score": 50000 }],
  "me": { "rank": 12, "score": 12000 }
}
```

未上榜 `me.rank=0`。

### 10.5 配置键

```json
"social": {
  "friend_max": 100,
  "friend_request_ttl_days": 7,
  "mail_expire_days": 30,
  "mail_broadcast_batch": 500,
  "rank_top_n": 100,
  "rank_score_mode": "gold"
}
```

### 10.6 验收（对照 SRS）

| ID | 验收 |
|----|------|
| FR-SOC-01 | 打完一局后 summary/recent 可见本局；重复结算不双计 |
| FR-SOC-02 | 双端互加好友、列表、删除；超上限失败 |
| FR-SOC-03 | Admin 发带金币/道具附件邮件 → 玩家领取到账且幂等 |
| FR-SOC-04 | 金币变化后日/周榜顺序正确；跨日后新 key；快照表有行 |

---

## 11. 背包与道具规格（FR-BAG）

> 对应 SRS §3.7。实现落点：`server/src/bag/`、`proto/bag.proto`、表见 §6.1、API 见 §5.3/§5.4。

### 11.1 道具类型语义

| kind | 发放 | 堆叠 UNIQUE | 消耗校验 |
|------|------|-------------|----------|
| `qty` | `quantity+=N`，`expire_at=哨兵不过期` | `(uid,item_id,expire_at)` | 仅数量 |
| `qty_ttl` | `quantity+=N`，写 `expire_at`（policy 或 default_expire_sec） | 同上 | 未过期 + 数量 |
| `ttl` | 同 `item_id` 仅一行、`quantity=1`；未过期则 `expire_at += duration*N`（从 max(现 expire, NOW) 起加）；已过期/无则从 NOW 起加 | 同 item_id 合并为一行 | 未过期即视为持有（buff） |

哨兵不过期：`9999-12-31 23:59:59.999`。

### 11.2 流程

```text
业务(活动/邮件/GM/支付赠送)
  → Bag.Grant(idempotent_key)
  → UPSERT bag_item + INSERT item_ledger
  → 可选 S2C_BagUpdate

Consume / Admin.revoke
  → 选未过期行扣减（优先最早到期，FIFO）
  → 不足 → 2202/2203
```

### 11.3 列表 API data 示例

`GET /api/v1/bag/list`

```json
{
  "items": [
    {
      "item_id": 1001,
      "name": "改名卡",
      "kind": "qty",
      "quantity": 3,
      "expire_at": "9999-12-31 23:59:59",
      "tag": "rename"
    },
    {
      "item_id": 2001,
      "name": "周卡体验",
      "kind": "ttl",
      "quantity": 1,
      "expire_at": "2026-10-05 12:00:00",
      "tag": "pass"
    }
  ]
}
```

时间字段对外为可读字符串（与 Admin 约定一致）。

### 11.4 配置键

```json
"bag": {
  "lazy_expire": true,
  "cleanup_interval_s": 3600,
  "max_rows_per_uid": 500
}
```

> `ttl` 固定为「单行续期」语义（见 §11.1），不再提供按 expire 新开行的堆叠模式。

### 11.5 验收（对照 SRS）

| ID | 验收 |
|----|------|
| FR-BAG-06/07/08 | 三类道具各发放一条路径；qty 不过期；ttl/qty_ttl 到期后不可 Consume |
| FR-BAG-04/05/10 | 重复幂等键不双加；流水可追溯 |
| FR-BAG-11 | 活动领奖、邮件附件、Admin grant 至少各打通一类道具 |
| FR-BAG-13 | Admin 可 CRUD 道具定义、上下架、查背包/流水；`item_id` 表单/筛选用 ItemSelect（§14.2.1） |
| FR-WEB-06a | game-web `/bag` 可见持有与过期时间 |

---

## 12. 匹配规格

- 队列键：`match:q:{template_id}`  
- 入队条件：金币 ∈ [min,max]，未封禁，未在房间  
- 凑满 3 人 → 创建 `room` → 分配座位 → `S2C_MatchStatus(success)` + `S2C_RoomState`  
- 超时（默认 30s）出队并通知  
- 单实例内存队列即可；进程重启清空匹配中状态并通知重试  

---

## 13. 配置规格

### 13.1 文件示例（`conf/server.json` 逻辑字段）

```json
{
  "net": {
    "https_port": 443,
    "admin_https_port": 8443,
    "wss_port": 4430,
    "tcp_tls_port": 4431,
    "tls_cert": "certs/server.crt",
    "tls_key": "certs/server.key",
    "max_frame_bytes": 1048576,
    "heartbeat_interval_s": 15,
    "heartbeat_timeout_s": 45
  },
  "mysql": { "dsn": "...", "pool_size": 50 },
  "redis": { "uri": "redis://127.0.0.1:6379/0", "pool_size": 50 },
  "game": {
    "base_score": 100,
    "rake_bp": 500,
    "bid_timeout_s": 15,
    "play_timeout_s": 20,
    "match_timeout_s": 30
  },
  "exchange": { "diamond_to_gold": 1000 },
  "alipay": {
    "sandbox": true,
    "app_id": "REPLACE",
    "merchant_private_key": "REPLACE",
    "alipay_public_key": "REPLACE",
    "gateway": "https://openapi.alipay.com/gateway.do",
    "notify_url": "https://api.example.com/api/v1/pay/alipay/notify"
  },
  "worker": { "biz_threads": 8, "async_threads": 4 },
  "social": {
    "friend_max": 100,
    "friend_request_ttl_days": 7,
    "mail_expire_days": 30,
    "mail_broadcast_batch": 500,
    "rank_top_n": 100,
    "rank_score_mode": "gold"
  },
  "bag": {
    "lazy_expire": true,
    "cleanup_interval_s": 3600,
    "max_rows_per_uid": 500
  }
}
```

密钥禁止入库；用环境变量覆盖敏感字段。

### 13.2 热更

后台改 `room_template` / 活动 / 道具定义 / 公告 / 邮件广播 → 进程内刷新缓存 → 对在线连接广播（维护/公告/活动刷新/邮件通知/背包定义刷新）。

---

## 14. 运营后台（Vue）规格

### 14.1 技术

- Vue 3 + Vue Router + Pinia（建议）  
- UI：Element Plus / Naive UI 任选  
- 构建静态资源；Nginx HTTPS；**域名与证书由运维提供**  

### 14.2 页面

| 路由 | 页面 | 角色 |
|------|------|------|
| /login | 登录 | — |
| /dashboard | 看板 | cs+ |
| /players | 玩家（含背包入口） | cs+ |
| /ledgers | 账变 | cs+ |
| /rounds | 对局 | cs+ |
| /templates | 场次 | ops+ |
| /activities | 活动（奖励可配道具） | ops+ |
| /items | 道具定义 CRUD/上下架；人工发放/背包/流水 | ops+ |
| /pay/products | 充值档位（可赠送道具） | ops+ |
| /pay/orders | 订单 | cs+ |
| /announce | 公告 | ops+ |
| /mail | 系统邮件发送与记录（附件可含道具） | ops+ |
| /rank | 排行快照查询 | cs+ |
| /ops | 维护/白名单 | super |
| /audit | 审计 | super |
| /reports | 报表 CSV 下载 | ops+ |

### 14.2.1 道具选择联动（FR-ADMIN-25）

后台凡需填写 `item_id` 的交互**不得**使用自由数字输入，统一组件 `admin-web/src/components/ItemSelect.vue`：

| 页面 | 场景 |
|------|------|
| `/activities` | 活动奖励 `items[]` |
| `/mail` | 邮件附件道具行 |
| `/pay/products` | 档位赠送道具行 |
| `/items` | 人工发放；道具流水筛选（可清空=不限；可含已下架） |

- 选项来源：`GET /admin/v1/items`；展示 `ID 名称 (kind)`；可过滤搜索。  
- 默认仅已上架；流水筛选等传 `includeDisabled`。  
- 保存前过滤 `item_id<=0` 的空行；未选有效道具则提示，不提交自由 ID。  

---

## 14.3 简单版网页游戏客户端（game-web）

### 目标

联调与演示用 **简洁 UI**，非商业美术；协议与原生客户端一致（WSS + `uint32 LE` 帧 + Protobuf）。

### 技术

- Vue 3 + Vite + TypeScript（建议）  
- protobufjs / ts-proto 等由 `proto/` 生成  
- 环境变量：`VITE_API_BASE`、`VITE_WSS_URL`  

### 页面

| 路由 | 说明 |
|------|------|
| `/login` | 游客登录 |
| `/lobby` | 场次列表、金币/钻石、快速匹配 |
| `/table` | 斗地主桌面：手牌、叫分/出牌/过、倒计时、局号、结算弹层 |
| `/hzmj-table` | 杭州麻将桌面：手牌、鸣牌、倒计时、局号、结算 |
| `/hzmj-lab` | 杭州麻将四联调试 |
| `/activity` | 活动列表与领奖（至少签到或任务一类） |
| `/bag` | 背包列表：数量、过期时间可读（FR-BAG / FR-WEB-06a） |
| `/wallet` | 余额、兑换、充值档位（支付 P1） |
| `/record` | 战绩汇总 + 近期对局（FR-SOC-01，P0） |
| `/friends` | 好友列表/申请（FR-SOC-02，P1） |
| `/mail` | 邮件列表/领取（FR-SOC-03，P1；附件可含道具） |
| `/rank` | 日/周排行榜（FR-SOC-04，P1） |

### 对局桌最小交互

1. 显示 3 个座位（己方在下）  
2. 叫分：按钮 0/1/2/3  
3. 出牌：点选手牌 +「出牌」「过」；非法出牌提示服务端错误  
4. 收到 `S2C_DdzSettle` 展示输赢并回大厅/续局  
5. 断线自动重连并处理 `S2C_DdzReconnect`；倒计时显示快照里的剩余秒，不从满值重计  
6. 对局中把关键步骤以 `C2S_ClientTrace`（9001）上报，桌面显示局号，与服务端日志的 `round=` 对齐  

### 验收

- 同一浏览器 **三开窗口**（或隐身窗口）可凑一桌打完一局  
- 编解码与服务端抓包帧一致  
- P0：战绩页在一局后有数据；背包页可展示持有道具；P1：好友/邮件/排行可演示  

---

## 15. 可观测性

### 15.1 日志字段

常规行：`ts, level, trace_id, uid, room_id, round_id, msg_id, code, err`。

对局轨迹另写一行，便于按局过滤。文件为进程工作目录下 `logs/pandora_YYYY-MM-DD.log`（从 `server/` 启动时即 `server/logs/`），按日切分，保留 7 天。

```text
round=<局号> src=server|client game=hzmj|ddz room=<房间> uid=<玩家> seat=<座位> ev=<事件> <detail>
```

- `src=server` 由服务端直接写。座位类事件的 `uid` 取该座位玩家，流局等无主事件为 `0`。  
- `src=client` 来自 `C2S_ClientTrace`（9001）。`event` 截断 40、`detail` 截断 500，换行改为空格。`room` 由服务端按连接 uid 查房间填入，与 `src=server` 使用同一房间号。  
- 过滤示例：`round=1790750821245`。

### 15.2 指标（最低）

`conn_gauge, match_queue_len, room_active, settle_qps, settle_fail, pay_success, async_queue_len, login_qps, mail_send_qps, rank_zcard, bag_grant_qps, bag_grant_fail`

### 15.3 健康检查

`GET /health` → `{ "status":"ok", "mysql":true, "redis":true, "ccu":1234 }`

---

## 16. 里程碑实现切片（对照 SRS）

| 里程碑 | SPEC 交付要点 |
|--------|----------------|
| M1 | net 帧编解码、TLS、login、session、heartbeat、工程骨架、proto 生成流水线 |
| M2 | lobby/room/match + DdzClassicSimple 状态机与牌型 |
| M2b | **game-web**：登录 + WSS 帧 + 大厅/匹配（可与 M2 并行） |
| M3 | wallet/ledger、exchange、alipay APP、断线托管；**网页对局桌打通** |
| M4 | admin API + admin-web、场次热更、审计 |
| M5 | sign/task/gift 实时进度；**网页活动页** |
| M6 | 单实例 2 万 CCU 压测与调优 |
| M7 | **社交 FR-SOC**：战绩 P0；好友/邮件/排行 P1；`social.proto` + Admin 邮件/排行页 |
| M8 | **背包 FR-BAG**：三类道具、Grant/Consume、活动/邮件/Admin/game-web `/bag`（对应 SRS 里程碑「M7 背包」） |

---

## 17. 验收映射（摘要）

| SRS 验收点 | SPEC 验证方式 |
|------------|----------------|
| Protobuf 帧 u32 LE | 单测编解码 + 抓包；网页与服务端互通 |
| 单实例 2 万 CCU | 压测脚本长连接+心跳 |
| 结算/领奖/支付/道具发放幂等 | 重复请求单测 |
| 斗地主服务端权威 | 篡改出牌包被拒 |
| 无 Kafka/微服务 | 部署拓扑检查 |
| Vue 运营后台 | 用例清单走查（含道具定义/补发；`item_id` 均从定义选择） |
| 简单网页客户端 | 三开窗口打完一局 + 活动领奖 + 背包页 |
| FR-SOC-01～04 | 见 §10.6 |
| FR-BAG | 见 §11.5 |

---

## 18. 变更记录

| 版本 | 日期 | 说明 |
|------|------|------|
| V1.0 | 2026-09-23 | 初版，依据 SRS V1.13 |
| V1.1 | 2026-09-23 | 增加简单版网页游戏客户端（game-web），对齐 SRS V1.14 |
| V1.2 | 2026-09-28 | 新增 §10 社交与战绩（FR-SOC）：表/Redis/API/msg_id/模块接口；里程碑 M7 |
| V1.3 | 2026-09-28 | 新增 §11 背包与道具（FR-BAG）：三类道具、表/API/BagService、活动邮件挂钩；里程碑 M8；对齐 SRS V1.16 |
| V1.4 | 2026-09-29 | §14.2.1 运营后台道具选择联动（`ItemSelect`）；对齐 SRS V1.17 / FR-ADMIN-25 |

---

**关联文档**：需求以 `docs/棋牌游戏服务端-需求文档.md`（**V1.17**）为准；字段级 `.proto` 与 DDL 脚本在实现阶段落入 `proto/` 与 `server/sql/`，并与本 SPEC 的 msg_id / 表名保持一致。
