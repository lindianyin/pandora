# 捕鱼游戏 — 技术规格说明书（SPEC）

| 项目 | 内容 |
|------|------|
| 文档版本 | V1.0 |
| 创建日期 | 2026-10-09 |
| 文档状态 | 可开发（须按 **TDD** 落地） |
| 依据文档 | [捕鱼游戏-需求文档.md](./捕鱼游戏-需求文档.md) V1.1.1 |
| 关联主 SPEC | [棋牌游戏服务端-SPEC.md](./棋牌游戏服务端-SPEC.md)（帧格式、鉴权、钱包、`IRoomGame`/`GameRegistry`） |
| 适用范围 | `pandora-server` 捕鱼模块、`proto/game_fish.proto`、`game-web` 渔场与 `/fish-lab` |

> **需求冲突**：玩法语义以需求文档为准；本 SPEC 落实为状态机、公式、协议、表结构、模块接口与 **测试先行** 约束。主仓硬约束（单体、IOCP、钱包幂等、MySQL 权威）不变。  
> **明确不做**：音频；客户端自报捕获发奖；房间时长税（抽水仅开火）。

---

## 1. 规格总览

### 1.1 定位

| 项 | 值 |
|----|----|
| `game_id` | **5000**（变体预留 5001–5099） |
| 短名 | `fish`（`LogRound` / ClientTrace） |
| 模块名 | `FishClassic` |
| 人数 | **1–4**（`min_players` 默认 1，正式金币场允许单人） |
| 代码落点 | `server/src/game/fish/` |
| 适配器 | `FishRoomGame`（`IRoomGame`） |
| 协议 | `proto/game_fish.proto` |
| msg_id 段 | **500000 – 500099**（`msg = 5000 * 100 + slot`） |
| 玩法错误码 | **5000000 – 5000099**（`err = 5000 * 1000 + slot`） |

### 1.2 与主框架的关系

```text
Match / RoomManager
        │  game_id=5000
        ▼
   GameRegistry → FishRoomGame → FishTable（每桌一实例）
        │
        ├─ SessionHub 推送 5000xx
        ├─ WalletService.Adjust（开火扣费+抽水 / 捕获入账；ledger 幂等）
        └─ Admin.RecordRound（可选离场汇总）/ Activity 钩子
```

- 入座、匹配、互踢、钱包复用平台；**禁止**再往 `Room` 加捕鱼平行字段。
- 鱼群、子弹、命中、发奖 **仅服务端权威**；客户端上报开火意图与瞄准参数。

### 1.3 交付切片（TDD 对齐）

| 切片 | 范围 | 先写测试 | 验收 |
|------|------|----------|------|
| F0 | 公式：cost/rake/reward、概率捕获、血量扣血 | `fish_math_test` | 用例表全绿 |
| F1 | `FishTable`：刷鱼、移动、开火、命中、离场 | `fish_table_test` | 无钱包亦可测完一局逻辑 |
| F2 | `FishRoomGame` + proto + 钱包幂等 | `fish_room` 冒烟 + 单测夹具 | 真服开炮账变正确 |
| F3 | 重连快照、DB 鱼种/波次、Admin 热更 | 集成/冒烟 | lab 多端同桌 |
| F4 | 锁定、BOSS 波次、活动钩子 | 增量单测 | P1 清单 |

**强制**：未先提交失败测试（或同提交中测试先于实现意图清晰）不得合入玩法逻辑；见 §12。

---

## 2. TDD 工作流（强制）

### 2.1 节奏

```text
1. 从 §11 选一条用例 → 写失败测试（Red）
2. 最小实现使测试通过（Green）
3. 重构命名/去重，保持全绿（Refactor）
4. 下一条用例；禁止一次堆无测试的大实现
```

### 2.2 测试目标与命令

| 目标 | 路径 | 说明 |
|------|------|------|
| `fish_math_test` | `server/tests/fish_math_test.cpp` | 纯函数：扣费、抽水、概率、伤害、奖励上限 |
| `fish_table_test` | `server/tests/fish_table_test.cpp` | `FishTable` 内存桌：刷鱼/开火/命中/捕获（可注入 RNG） |
| `fish_config_test` | `server/tests/fish_config_test.cpp`（可与 math 合并） | DB 行 → 内存配置解析与校验 |
| 前端 | `game-web` vitest | 帧编解码、炮倍 UI 状态（无音频） |
| 冒烟 | `server/scripts/fish_smoke.mjs` | 登录×N → 匹配 template → 开火 → 见捕获或扣费 |

```powershell
cmake --build server\build --config Release --target fish_math_test
cmake --build server\build --config Release --target fish_table_test
server\build\Release\fish_math_test.exe
server\build\Release\fish_table_test.exe
cd game-web; npm test
# 服务已启动后：
node server\scripts\fish_smoke.mjs
```

### 2.3 可测性设计（必须遵守）

| 点 | 要求 |
|----|------|
| RNG | `FishTable` 注入 `std::function<uint32_t()>` 或固定种子；单测可强制命中/不中 |
| 时间 | tick 用显式 `Tick(dt_ms)` / `Tick(now)`，禁止单测依赖真实 sleep |
| 钱包 | Table 层只产出 `FireCostPlan` / `CatchRewardPlan`；`FishRoomGame` 调 `WalletService` |
| 配置 | 单测用内存 `FishConfig` 构造，不强制连 MySQL |
| 日志 | 关键路径 `LogRound(..., "fish", ...)`，断言不依赖日志字符串 |

### 2.4 Definition of Done（每条玩法 PR）

1. 新增/变更行为有对应用例 ID（§11）。  
2. `fish_math_test` + `fish_table_test`（相关）全绿。  
3. 无新的无测试分支（概率/血量/抽水/拒绝开火）。  
4. 协议字段变更同步 `proto` + SPEC 消息表。

---

## 3. 坐标系与实体

### 3.1 场景

| 项 | 值 |
|----|----|
| 逻辑坐标系 | 原点左下；宽 `W=1920`，高 `H=1080`（逻辑单位，与像素可 1:1） |
| 座位 | 0..3；炮口锚点固定（SPEC 常量数组 `kSeatCannonPos[4]`） |
| Tick | 默认 `dt=50ms`（20 Hz），`room` 配置可改 |

### 3.2 鱼实例 `FishInst`

| 字段 | 类型 | 说明 |
|------|------|------|
| `fish_id` | int64 | 桌内自增 |
| `type_id` | int32 | 鱼种 |
| `x,y` | float | 中心 |
| `vx,vy` | float | 速度 |
| `radius` | float | 碰撞圆 |
| `hp` / `hp_max` | int | 仅 `kind=hp`；`odds` 鱼 hp 无意义 |
| `alive` | bool | |
| `born_ms` / `ttl_ms` | int64 | 超时离场 |

### 3.3 子弹 `Bullet`

| 字段 | 类型 | 说明 |
|------|------|------|
| `bullet_id` | int64 | 桌内自增 |
| `seat` / `uid` | int / int64 | 归属 |
| `mult` | int | 开火时炮倍 |
| `x,y,vx,vy` | float | |
| `radius` | float | 默认小圆 |
| `lock_fish_id` | int64 | 0=无锁定 |
| `alive` | bool | |

---

## 4. 数值公式（权威）

以下符号：`B`=底分 `base_score`，`M`=炮倍 `cannon_mult`，`S`=鱼分值 `score`，`rake_bp`=抽水基点。

### 4.1 开火扣费与抽水

```text
cost       = M * B                          // 玩家本发总扣费
rake       = floor(cost * rake_bp / 10000)  // 平台抽水
play_cost  = cost - rake                    // 计入 RTP 分子的「玩法消耗」侧（记账可拆两条或一条+备注）
```

- 余额 `< cost` → 拒开火（不生成子弹）。  
- 幂等键：`fish:fire:{round_id}:{uid}:{client_seq}`。  
- 抽水与消耗须可审计（ledger `remark` 或子类型；SPEC 实现选一种并单测）。

### 4.2 捕获奖励

```text
raw    = S * B
reward = min(raw, max_catch_reward)         // max_catch_reward 来自配置，默认如 1_000_000
```

幂等键：`fish:catch:{round_id}:{fish_id}:{uid}`（同一鱼只能发一次奖）。

### 4.3 概率鱼（`kind=odds`）

```text
p = clamp(M / S, p_min, p_max)             // 建议 p_min=0.01, p_max=0.95，可配
命中后：Bernoulli(p) → 捕获或子弹销毁（鱼继续）
```

单测：固定 `M,S` 与注入 RNG 序列，断言捕获次数期望落在区间或精确次数。

### 4.4 血量鱼（`kind=hp`）

```text
dmg = max(1, M)                            // 首发：伤害=炮倍；可配系数 dmg = M * dmg_factor
hp  = hp - dmg
hp <= 0 → 捕获；否则广播受伤（剩余 hp）
```

### 4.5 碰撞

圆-圆：`(dx*dx+dy*dy) <= (r_bullet + r_fish)^2`。  
每 tick：先移鱼与子弹，再两两检测；一发子弹默认命中后销毁（穿透 P1）。

---

## 5. 配置（MySQL 权威）

### 5.1 表 `fish_type`（示意 DDL）

```sql
CREATE TABLE IF NOT EXISTS `fish_type` (
  `type_id` INT NOT NULL PRIMARY KEY,
  `name` VARCHAR(64) NOT NULL,
  `score` INT NOT NULL,
  `kind` ENUM('odds','hp') NOT NULL,
  `hp` INT NOT NULL DEFAULT 0,
  `weight` INT NOT NULL DEFAULT 1,
  `radius` INT NOT NULL DEFAULT 40,
  `special` VARCHAR(32) NOT NULL DEFAULT 'none',
  `enabled` TINYINT NOT NULL DEFAULT 1,
  `updated_at` DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3) ON UPDATE CURRENT_TIMESTAMP(3)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
```

### 5.2 表 `fish_wave`

| 列 | 说明 |
|----|------|
| `id` | PK |
| `name` | 波次名 |
| `duration_ms` | 持续 |
| `spawn_interval_ms` | 刷鱼间隔 |
| `max_alive` | 同屏上限（全局另有硬顶 80） |
| `boss_type_id` | 0=无 |
| `weight` / `enabled` | |

进程：`FishConfigStore::Reload(mysql)`；Admin 热更后调用 Reload。

### 5.3 场次模板扩展

`room_template.game_id=5000`，种子示例：`(id=4, 5000, '捕鱼初级场', ...)`。

扩展 JSON：

| 键 | 默认 | 说明 |
|----|------|------|
| `min_players` | 1 | 正式场允许单人 |
| `cannon_mults` | `[1,2,5,10,20,50,100]` | |
| `fire_rate_hz` | 8 | 每座位每秒最大开火 |
| `p_min` / `p_max` | 0.01 / 0.95 | |
| `max_catch_reward` | 1000000 | |
| `bullet_speed` | 1200 | 逻辑单位/秒 |
| `disconnect_kick_ms` | 120000 | 断线踢出 |

`rake_bp` 用模板列。

---

## 6. 状态机

```text
WaitPlayers → Playing → Closed
```

| 状态 | 行为 |
|------|------|
| WaitPlayers | 入座；人数 ≥ `min_players` 后立刻或短倒计时进入 Playing |
| Playing | `Tick`：刷鱼、移鱼、移弹、碰撞、结算；接受开火/换炮/离开 |
| Closed | 空桌或显式销毁；`IRoomGame` 结束 |

断线：座位 `online=false`，**拒绝开火**；超时踢出。重连：下发快照，不重置鱼群（同 `round_id`）。

---

## 7. 模块接口

### 7.1 目录

```text
server/src/game/fish/
  math.hpp|cpp          # cost/rake/p/dmg/reward
  config.hpp|cpp        # FishConfig + ReloadFromDb
  table.hpp|cpp         # FishTable 场景权威
  fish_room_game.hpp|cpp
server/tests/
  fish_math_test.cpp
  fish_table_test.cpp
```

### 7.2 `FishTable`（示意）

```cpp
struct FireRequest {
  int seat;
  int64_t uid;
  int mult;
  float aim_x, aim_y;   // or angle
  int64_t lock_fish_id; // 0=none
  int64_t client_seq;
};

struct OutEvent {
  // spawn_fish | fish_dead | fire | hit | catch | seat_update | snapshot ...
};

class FishTable {
 public:
  explicit FishTable(FishConfig cfg, std::function<uint32_t()> rng);
  void Start();
  void Tick(int dt_ms);
  bool SetCannonMult(int seat, int mult);
  // Returns false if rejected; on success emits FireCostPlan via sink/out
  bool TryFire(const FireRequest& req, /*out*/ FireCostPlan* cost);
  void OnLeave(int seat);
  void SetSink(std::function<void(const OutEvent&)> sink);
};
```

`FishRoomGame`：实现 `IRoomGame`；`Handle` 解 proto；`Tick` 调 `FishTable::Tick`；把 `FireCostPlan`/`CatchRewardPlan` 转为 `Wallet.Adjust`。

注册：

```text
GameRegistry.Register(GameId::kFish=5000, "fish", default_seats=4, factory → FishRoomGame)
```

（`DefaultSeats` 为上限 4；开局人数由匹配 `players`/`min_players` 决定。）

---

## 8. 协议（msg_id = 500000 + slot）

| slot | msg_id | 方向 | message | 说明 |
|------|--------|------|---------|------|
| 1 | 500001 | S→C | `S2C_FishGameStart` | round_id、room、座位、炮倍列表、场景种子/配置快照 |
| 2 | 500002 | S→C | `S2C_FishSeatUpdate` | 座位在线、炮倍、昵称 |
| 3 | 500003 | S→C | `S2C_FishSpawn` | 鱼出生批量 |
| 4 | 500004 | S→C | `S2C_FishDespawn` | 离场/死亡（无奖或有奖见 Catch） |
| 5 | 500005 | S→C | `S2C_FishSync` | 可选降频位置同步（或依赖客户端插值+出生速度） |
| 6 | 500006 | C→S | `C2S_FishFire` | mult、aim、lock、seq |
| 7 | 500007 | S→C | `S2C_FishFireBroadcast` | 谁开火、子弹参数、扣费后余额 |
| 8 | 500008 | S→C | `S2C_FishHit` | 子弹命中、受伤 hp（血量鱼） |
| 9 | 500009 | S→C | `S2C_FishCatch` | 捕获、reward、余额 |
| 10 | 500010 | C→S | `C2S_FishSetMult` | 换炮倍 |
| 11 | 500011 | C→S | `C2S_FishLeave` | 离开 |
| 12 | 500012 | S→C | `S2C_FishKickSeat` | 超时踢出座位 |
| 13 | 500013 | S→C | `S2C_FishWave` | 波次提示 P1 |
| 14 | 500014 | C→S | `C2S_FishLock` | 锁定 P1 |

字段级定义以 `proto/game_fish.proto` 为准；改消息须同改本表。

### 8.1 玩法错误码

| code | 含义 |
|------|------|
| 5000001 | 非法炮倍 |
| 5000002 | 开火过频 |
| 5000003 | 余额不足 |
| 5000004 | 座位无效/已离线 |
| 5000005 | 非法锁定目标 |
| 5000006 | 重复 client_seq |

走 `S2C_Error` + `ErrMessage`。

---

## 9. 对局日志

```text
round=<id> src=server|client game=fish room=<rid> uid=<u> seat=<s> ev=<ev> <detail>
```

建议 `ev`：`start` / `fire` / `catch` / `leave` / `disconnect` / `reconnect`。

---

## 10. 客户端（game-web）

| 项 | 要求 |
|----|------|
| 路由 | `/fish` 正式桌；`/fish-lab` 多端 lab（对齐 phz-lab） |
| 表现 | 色块/简单精灵即可；**无音频** |
| 操作 | 瞄准、开火、换炮、离开；显示余额与炮倍 |
| 顶号 | `KickReason.LoggedInElsewhere` 不自动重连 |
| 测试 | `frame.fish.test.ts` 覆盖 MsgId 与编解码 |

Lab：多浏览器 / 多硬件码游客，同一 `template_id` 入座同桌。

---

## 11. 验收用例（TDD 用例表）

实现时每条对应至少 1 个自动化测试（标 `math` / `table` / `smoke`）。

### 11.1 公式与配置（`fish_math_test`）

| ID | 场景 | 期望 | 层 |
|----|------|------|----|
| T01 | M=10,B=100 → cost=1000 | 相等 | math |
| T02 | cost=1000,rake_bp=500 → rake=50 | `floor(1000*500/10000)` | math |
| T03 | S=20,B=100 → reward=2000 | 相等 | math |
| T04 | raw>max → clamp | `reward==max_catch_reward` | math |
| T05 | M=5,S=100 → p=0.05 | clamp 前后 | math |
| T06 | M=200,S=10 → p=p_max | 不超过 p_max | math |
| T07 | hp 鱼 dmg=M | hp 递减正确 | math |

### 11.2 桌逻辑（`fish_table_test`）

| ID | 场景 | 期望 | 层 |
|----|------|------|----|
| T10 | Start 后 Tick 刷鱼 | alive 数增加且 ≤ max | table |
| T11 | 鱼出界/超时 | despawn 无 Catch | table |
| T12 | TryFire 余额由外层保证；Table 接受合法 fire | 生成子弹 + OutEvent | table |
| T13 | 非法 mult | TryFire false | table |
| T14 | 超频 fire | 第二次拒绝 | table |
| T15 | 子弹命中 odds + RNG 必中 | Catch 一次；鱼死 | table |
| T16 | odds + RNG 必不中 | 子弹死；鱼仍活 | table |
| T17 | hp 鱼多次命中 | 受伤事件；最后一击 Catch | table |
| T18 | 同一 fish_id 不二次 Catch | 幂等/已死 | table |
| T19 | Leave seat | 该座子弹可继续结算归属；不可再 Fire | table |
| T20 | 离线 seat Fire | 拒绝 | table |

### 11.3 房间 / 冒烟

| ID | 场景 | 期望 | 层 |
|----|------|------|----|
| T30 | 单人匹配 template 捕鱼 | 进入 Playing | smoke |
| T31 | 开火 ledger 扣 cost | 余额减少；可区分抽水 | smoke |
| T32 | 捕获加币 | 余额增加 | smoke |
| T33 | 断线再重连 | 快照座位与鱼数量合理 | smoke |
| T34 | fish-lab 双端同桌 | 互见开火广播 | smoke/手工 |

---

## 12. 非目标（本 SPEC 首发不做）

- 音频  
- 特殊鱼（炸弹/闪电等）完整实现（接口可预留 `special`）  
- 客户端物理引擎级碰撞  
- 独立时长税、排行榜冲榜  
- `game_id≠5000` 的捕鱼变体  

---

## 13. 文档与实现顺序（TDD）

```text
1. 本 SPEC 定稿（本文）
2. 主 SPEC / AGENTS 登记 game_id=5000、msg 段、错误段
3. 写 fish_math_test（T01–T07）→ 实现 math.hpp 至绿
4. 写 fish_table_test（T10–T20）→ 实现 FishTable 至绿
5. proto/game_fish.proto + FishRoomGame + Registry 注册
6. schema：fish_type / fish_wave + room_template 种子
7. game-web 渔场页 + /fish-lab + frame 测试
8. fish_smoke.mjs；Admin 鱼种热更
```

改公式或命中规则时：**先改 §4 / §11 用例，再改测试，最后改代码**。

---

## 14. 修订记录

| 版本 | 日期 | 说明 |
|------|------|------|
| V1.0 | 2026-10-09 | 初稿：对齐需求 V1.1.1；强制 TDD 与用例表 |

**关联**：需求 [捕鱼游戏-需求文档.md](./捕鱼游戏-需求文档.md)；规则说明可另补 `docs/捕鱼游戏规则.md`（玩家可读摘要，数值以本 SPEC §4 为准）。
