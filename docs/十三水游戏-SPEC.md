# 十三水游戏 — 技术规格说明书（SPEC）

| 项目 | 内容 |
|------|------|
| 文档版本 | V1.0 |
| 创建日期 | 2026-10-12 |
| 文档状态 | 可开发（**强制 TDD**；未过单测不得合入玩法逻辑） |
| 依据文档 | [十三水游戏-需求文档.md](./十三水游戏-需求文档.md) V1.1（§10 已闭合） |
| 关联主 SPEC | [棋牌游戏服务端-SPEC.md](./棋牌游戏服务端-SPEC.md)（帧格式、鉴权、钱包、`IRoomGame`/`GameRegistry`） |
| 适用范围 | `pandora-server` 十三水模块、`proto/game_sss.proto`、`game-web` `/sss` 与 `/sss-lab` |

> **需求冲突**：玩法语义以需求文档为准；本 SPEC 落实为牌编码、比较键、水数公式、特殊牌型、状态机、协议、模块接口与 **测试先行** 约束。主仓硬约束（单体、IOCP、钱包幂等、MySQL 权威）不变。  
> **明确不做**：旁观；大小王/五同；马牌；朋友场房卡；庄闲下注桌；客户端自报牌型/水数/输赢。  
> **与比鸡隔离**：禁止 `#include` / 复用 `biji/hand` 规则常量；目录、协议、错误码、单测、lab 均独立。

---

## 1. 规格总览

### 1.1 定位

| 项 | 值 |
|----|----|
| `game_id` | **7000**（变体预留 7001–7099） |
| 短名 | `sss`（`LogRound` / ClientTrace） |
| 模块名 | `SssClassic`（十三张三墩 · 无王） |
| 人数 | **2–4**（模板 `players`；最大 4） |
| 代码落点 | `server/src/game/sss/` |
| 适配器 | `SssRoomGame`（`IRoomGame`） |
| 规则内核 | `SssTable` + `sss/hand.hpp` + `sss/settle.hpp` |
| 协议 | `proto/game_sss.proto` |
| msg_id 段 | **700000 – 700099**（`msg = 7000 * 100 + slot`） |
| 玩法错误码 | **7000000 – 7000099**（`err = 7000 * 1000 + slot`） |

### 1.2 与主框架的关系

```text
Match / RoomManager
        │  game_id=7000
        ▼
   GameRegistry → SssRoomGame → SssTable（每桌一实例）
        │
        ├─ SessionHub 推送 7000xx
        ├─ WalletService.Adjust（终局结算 + 赢家抽水；ledger 幂等）
        └─ Admin.RecordRound / Activity 钩子（可选）
```

- 入座、匹配、准备、互踢、倒计时框架复用平台；**禁止**再往 `Room` 加十三水平行字段。
- 发牌、牌型、倒水校验、水数、打枪、特殊牌型、金币 delta **仅服务端权威**；客户端上报墩牌/报到与确认。
- 对局轨迹：`game=sss`，客户端经 `C2S_ClientTrace`（9001）上报。

### 1.3 交付切片（TDD 对齐 · 门禁）

| 切片 | 范围 | 先写测试（Red） | 再实现至绿 | 出门条件 |
|------|------|-----------------|------------|----------|
| **S0** | `CardId`、`EvalHead`/`EvalFive`、`Compare*`、`IsLegalArrange` | `sss_hand_test` T01–T12 | `card.hpp` + `hand.*` | T01–T12 全绿；无 RoomGame |
| **S1** | `AutoArrange`、墩水数、打枪/全垒打、特殊牌型、抽水 | T13–T14、T20–T35 | `hand` 代摆 + `settle.*` + `special.*` | T13–T35 全绿；仍无网络 |
| **S2** | `SssTable` 状态机 | `sss_table_test` T40–T48 | `table.*` + `config.hpp` | T40–T48 全绿；无钱包/无 proto |
| **S3** | proto、`SssRoomGame`、Registry、模板种子 | `frame.sss.test.ts`；`sss_smoke` | `game_sss.proto` + RoomGame | T50–T53 冒烟绿；账变幂等 |
| **S4** | 重连、lab、Admin | T48 加深 + T54 | Snapshot / `/sss-lab` | lab 多端同桌手工勾选 |

**硬门禁（违反即打回）**

1. **禁止**先写完整 `SssRoomGame` / 前端桌面再补单测。  
2. **禁止**在 S0/S1 未绿时合入 S2+ 玩法逻辑。  
3. 每条用例 ID（§14）必须在测试函数名或注释中出现（如 `TestT01_StraightFlush`）。  
4. 改 §4/§8 公式或比较序：**先改 §14 用例 → 改测试（允许先红）→ 再改生产代码**。  
5. 细则见 **§2**。

---

## 2. TDD 工作流（强制 · 落地规范）

> 对齐比鸡 / 捕鱼：轻量 `Expect` 宏、用例函数 `TestTxx`、CMake 独立 `add_executable`、**不依赖真服**完成规则层。

### 2.1 Red → Green → Refactor

```text
1. 打开 §14，选下一条未实现用例 ID（按 S0→S1→S2→S3 顺序，禁止跳号堆功能）
2. Red：在对应 *_test.cpp 增加 TestTxx，断言最终行为；编译运行应失败
3. Green：只写使该用例通过的最小生产代码
4. Refactor：提取公共比较/结算，保持全绿
5. 提交粒度建议：单切片或数条用例一组；PR 描述列出覆盖的 Txx
```

**反模式（禁止）**

| 反模式 | 正确做法 |
|--------|----------|
| 先实现整桌再写测试 | 先 `hand`/`settle` 纯函数测通 |
| 单测里 `Sleep` 等倒计时 | `OnArrangeDeadline()` / 注入 `now_ms` |
| Table 内直接调 MySQL/Wallet | Table 产出 `SettlePlan`，RoomGame 再 Adjust |
| 用日志字符串做断言 | 断言返回值 / `SettlePlan` 字段 |
| 复用 `biji::*` 规则 | 独立实现；可抄编码风格不可链库 |
| 一次 PR 塞满 S0–S3 | 按切片出门；S0 可单独合入 |

### 2.2 测试二进制与 CMake

| 目标 | 源文件 | 链接生产代码（建议） | 覆盖用例 |
|------|--------|----------------------|----------|
| `sss_hand_test` | `server/tests/sss_hand_test.cpp` | `hand.cpp` `special.cpp` `settle.cpp` | T01–T14, T20–T35 |
| `sss_table_test` | `server/tests/sss_table_test.cpp` | 上表 + `table.cpp` | T40–T48 |
| vitest | `game-web/src/**/frame.sss.test.ts` | — | MsgId / 编解码 |
| 冒烟 | `server/scripts/sss_smoke.mjs` | 真服 | T50–T54 |

```cmake
add_executable(sss_hand_test
  tests/sss_hand_test.cpp
  src/game/sss/hand.cpp
  src/game/sss/special.cpp
  src/game/sss/settle.cpp
)
target_include_directories(sss_hand_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_executable(sss_table_test
  tests/sss_table_test.cpp
  src/game/sss/hand.cpp
  src/game/sss/special.cpp
  src/game/sss/settle.cpp
  src/game/sss/table.cpp
)
target_include_directories(sss_table_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
```

本地命令：

```powershell
cmake --build server\build --config Release --target sss_hand_test
cmake --build server\build --config Release --target sss_table_test
server\build\Release\sss_hand_test.exe
server\build\Release\sss_table_test.exe
cd game-web; npm test
# 仅 S3+ 且服务已启动：
node server\scripts\sss_smoke.mjs
```

### 2.3 测试夹具约定

```cpp
// server/tests/sss_hand_test.cpp
static int fails = 0;
void Expect(bool cond, const char* msg);

// Card helpers (must match SPEC §3)
constexpr int32_t C(int suit, int rank) { return suit * 13 + rank; } // suit 0..3, rank 0=2 .. 12=A
```

| 约定 | 要求 |
|------|------|
| 用例命名 | `void TestT01_...()` 或注释含 `T01` |
| 牌面构造 | 只用 `C(suit,rank)` / `CardId` |
| 比较断言 | `CompareFive` / `CompareHead` / `CmpOrder` 返回值，可另断言 `type` |
| 结算断言 | 检查 `water[]` / `delta[]` / `rake` / `net`；`sum(net)+sum(rake)==0` |
| Table RNG | 固定种子；T40 断言发牌无重叠、每人 13 张 |
| 时间 | `table.OnArrangeDeadline()` 显式触发；禁止 `sleep` |

### 2.4 可测性设计

| 点 | 要求 |
|----|------|
| 纯函数层 | `Eval*` / `Compare*` / `IsLegalArrange` / `AutoArrange` / `DetectSpecial` / `ComputeSettle` **无 I/O** |
| RNG | `SssTable(cfg, rng)` 注入 |
| 时间 | `OnArrangeDeadline`；deadline 存绝对 ms |
| 钱包 | Table → `SettlePlan`；仅 `SssRoomGame` 调 `WalletService` |
| 配置 | `SssConfig` 内存构造；单测不连 MySQL |
| 日志 | `LogRound(..., "sss", ...)`；**断言不读日志文件** |

### 2.5 Definition of Done（玩法 PR）

| # | 检查项 |
|---|--------|
| 1 | PR 描述列出本 PR 覆盖的用例 ID（§14） |
| 2 | 涉及切片的 `sss_*_test` 退出码 0 |
| 3 | 无「只改 cpp、不改测试」的规则行为变更 |
| 4 | 新错误码 / msg_id 已写入本 SPEC §11 与 `errors.hpp` |
| 5 | C++ 新增注释为 ASCII |
| 6 | 未往 `Room` 加平行字段；未引用 `biji` 规则实现 |
| 7 | S3+：幂等键 `sss:settle:{round}:{uid}`；重复 Settle 不双扣 |

---

## 3. 牌编码

### 3.1 CardId（协议 `int32`，范围 0–51）

与平台扑克玩法一致（一副去王 52 张）：

```text
suit  ∈ {0,1,2,3}   // 0=♦, 1=♣, 2=♥, 3=♠
rank  ∈ {0..12}     // 0=2, 1=3, …, 8=T(10), 9=J, 10=Q, 11=K, 12=A
CardId = suit * 13 + rank          // 0..51
```

```text
RankValue(c) := rank(c)            // A=12 最大（顺子特例除外）
SuitValue(c) := suit(c)
IsRed(c)     := suit ∈ {0, 2}
IsBlack(c)   := suit ∈ {1, 3}
```

**普通比牌不使用花色打破平局**（需求 §10 #2）。花色仅用于：同花/同花顺判定、特殊牌型「凑一色 / 至尊清龙」、客户端展示。

### 3.2 显示（客户端）

| rank | 标签 |
|------|------|
| 0..7 | 2..9 |
| 8 | 10 |
| 9..12 | J,Q,K,A |

协议只传 `CardId`。

---

## 4. 牌型与比较键

### 4.1 五张牌型等级（中墩 / 尾墩，大 → 小）

| level | 枚举名 | 条件 |
|------|--------|------|
| 8 | `STRAIGHT_FLUSH` | 同花且五张成顺 |
| 7 | `FOUR` | 四条 + 一张（铁支） |
| 6 | `FULL_HOUSE` | 三条 + 一对（葫芦） |
| 5 | `FLUSH` | 同花非顺 |
| 4 | `STRAIGHT` | 成顺非同花 |
| 3 | `THREE` | 三条 + 两散 |
| 2 | `TWO_PAIR` | 两对 + 一散 |
| 1 | `PAIR` | 一对 + 三散 |
| 0 | `HIGH` | 乌龙 |

### 4.2 头墩牌型等级（三张）

头墩 **不计同花、不计顺子**。

| level | 枚举名 | 条件 |
|------|--------|------|
| 3 | `THREE` | 三张同 rank |
| 1 | `PAIR` | 恰好一对 |
| 0 | `HIGH` | 其余 |

头墩 level 与五张表中同名 level **数值对齐**（便于 `CmpOrder`）；头墩永不出现 level ∈ {2,4,5,6,7,8}。

### 4.3 顺子定义（五张；头墩特殊牌型用三张顺）

五张自然连续（rank 差值构成长度为 5 的链），**不绕圈**：

- 合法：`A2345`（ranks `12,0,1,2,3`）→ `straight_top = -1`（最小）  
- 合法：`23456`…`TJQKA` → `straight_top = max(natural ranks)`，其中 `TJQKA` → `12`  
- 非法：`JQKA2`、`QKA23` 等

三张顺（仅用于特殊「三同花顺 / 三顺子」头墩）：

- 合法：`A23` → `straight_top = -1`；`234`…`QKA` → 对应 top；非法绕圈同理。

同花顺 = 同花 ∧ 顺子。

### 4.4 五张比较键 `FiveKey`

```text
struct FiveKey {
  int8_t level;           // 0..8
  int8_t p0, p1, p2, p3, p4;  // 类型相关踢脚，见下；未用填 -128
};
```

| level | 比较元组（字典序，先 level） |
|------|------------------------------|
| STRAIGHT_FLUSH / STRAIGHT | `(straight_top)` |
| FOUR | `(quad_rank, kicker)` |
| FULL_HOUSE | `(trip_rank, pair_rank)` |
| FLUSH / HIGH | 五张 rank **降序** `(r0..r4)` |
| THREE | `(trip_rank, kicker_hi, kicker_lo)` |
| TWO_PAIR | `(hi_pair, lo_pair, kicker)` |
| PAIR | `(pair_rank, k0, k1, k2)` 踢脚降序 |

`CompareFive(a,b) → -1/0/1`：按元组字典序；**不比花色**，故允许 0（平局）。

### 4.5 头墩比较键 `HeadKey`

```text
struct HeadKey {
  int8_t level;     // 0,1,3
  int8_t p0, p1, p2;
};
```

| level | 元组 |
|------|------|
| THREE | `(trip_rank)` |
| PAIR | `(pair_rank, kicker)` |
| HIGH | 三张 rank 降序 `(r0,r1,r2)` |

`CompareHead(a,b) → -1/0/1`：不比花色，允许 0。

### 4.6 墩序比较 `CmpOrder`（倒水判定专用）

用于判断 **头 ≤ 中**、**中 ≤ 尾**。中/尾用 `FiveKey`；头用 `HeadKey`，与五张比较时：

```text
CmpOrder_Head_Five(head, five):
  if head.level != five.level: return sign(head.level - five.level)
  // 同 level ∈ {THREE, PAIR, HIGH}：
  //   将 five 的比较元组截断/对齐到与 HeadKey 相同前缀长度后字典序比较
  //   THREE: 只比 trip_rank
  //   PAIR:  比 (pair_rank, kicker)；five 取最大踢脚作为 kicker 与 head.kicker 比
  //   HIGH:  比双方降序 rank 的前 3 项（five 取最大三张）
  // 若前缀全等：视为 head < five（返回 -1），保证「相等牌力时后墩更大」不误判倒水
  //   （同手牌牌张互斥，前缀全等在真牌下不应出现；此为防御）

CmpOrder_Five_Five(a, b) := CompareFive(a, b)
```

```text
IsLegalArrange(head[3], mid[5], tail[5]) :=
  cards 为手牌的置换（多重集合相等）
  && CmpOrder_Head_Five(EvalHead(head), EvalFive(mid)) <= 0
  && CmpOrder_Five_Five(EvalFive(mid), EvalFive(tail)) <= 0
```

特殊报到成功时 **跳过** `IsLegalArrange`（见 §8.4）。

---

## 5. 房间模板配置

`room_template.game_id=7000`。开局随 `S2C_SssGameStart` 下发只读快照。

| 键 | 类型 | 默认 | 说明 |
|----|------|------|------|
| `base_score` | int64 | 模板底分 | 记 `B`；金币 = 水 × B |
| `players` | int | 2..4 | 匹配凑齐人数 |
| `min_gold` / `max_gold` | int64 | 模板 | 入场校验 |
| `rake_bp` | int | 场次 | 赢家抽水基点 |
| `arrange_timeout_s` | int | 60 | 摆牌倒计时（需求默认 45–90，取 60） |
| `enable_special` | bool | true | 特殊牌型报到 |
| `enable_shoot` | bool | true | 打枪 |
| `enable_home_run` | bool | true | 全垒打（依赖打枪） |
| `compare_reveal_ms` | int | 800 | 客户端逐墩展示建议间隔 |

种子示例：

```text
(id=<next>, game_id=7000, name='十三水初级场', players=4, base_score=100, rake_bp=<平台默认>, ...)
```

`DefaultSeatsForGame(7000) = 4`；实际人数 = 模板 `players`。

---

## 6. 桌内状态机

```text
WaitReady
    │ 满 players 且全员 Ready
    ▼
Deal ──── 洗牌、定起发座位、发每人 13 张
    │
    ▼
Arrange ── 提交/改墩/报到/确认；断线仅标托管
    │ 全员 locked 或 deadline
    ▼
Compare ── 算水；广播特殊报到 + 头→中→尾 + 打枪/全垒打
    │
    ▼
Settle ── Wallet.Adjust；推送结算 ──► WaitReady
                ↘ 全离 / 解散 ──► Closed
```

| 状态 | 行为 |
|------|------|
| WaitReady | 入座、准备；结算后清除本局牌面；起发座位保留至下局 Deal |
| Deal | 瞬时完成进 Arrange；私发手牌 |
| Arrange | 接受 `C2S_SssArrange` / 报到；广播他人是否已确认/是否报到（不广播墩内容） |
| Compare | 只读计算结果并下发；不接受改牌 |
| Settle | 账变；`S2C_SssSettle`；回 WaitReady |

### 6.1 起发座位

```text
首局：deal_start = rng() % n
之后：
  winners = argmax seats by last_net_water（上局结算净水，抽水前 gross 水数）
  并列取 seat_id 最小
  deal_start = (winner_seat + 1) % n
发牌：从 deal_start 起座位号递增模 n，一人一张，共 13 轮；余牌不发
```

### 6.2 托管与超时

- Arrange 断线：`trusteeship=true`，**不立即代摆**。  
- `deadline`：对每个未 `locked` 座位 `AutoArrange(hand)` → `locked=true`，`special=None`（**不自动报到**）。  
- 重连：下发 phase、剩余秒、自己的手牌/草稿/是否报到/是否已锁；他人仅确认与报到标志。  
- **不重 Arm** 已走过的 deadline（下发剩余秒）。

### 6.3 离开

- WaitReady：直接离座。  
- 开局中离开：标托管，占座至 Settle；开局后人数固定。

---

## 7. 代摆启发式（权威 · 可单测）

对 13 张手牌，枚举划分：

```text
选 3 张作 head，再选 5 张作 mid，剩余 5 张作 tail
共 C(13,3)*C(10,5) = 180180 种
过滤 IsLegalArrange
在合法集中取字典序最优：
  最大化 FiveKey(tail)，其次 FiveKey(mid)，其次 HeadKey(head)
若合法集为空（不应出现）：按 CardId 升序切 [0..2][3..7][8..12]，再暴力找任一合法（防御）
```

确定性：同手牌同结果；禁止随机代摆；**禁止**代摆时设置特殊报到。

---

## 8. 结算公式（权威）

符号：`B = base_score`，`n = 人数`，座位 `0..n-1`。  
先划分玩家集合：

```text
Special(u) = 若 u.locked && u.report_special && DetectSpecial(u.hand)==u.special_type
             （提交时 server 写入 special_type；见 §8.4）
Ordinary = { u | not Special(u) }
```

### 8.1 单墩水数（普通 vs 普通）

对 Ordinary 中无序对 `(i,j)`，`i<j`，逐墩 `d ∈ {head,mid,tail}`：

```text
cmp = Compare*(i.d, j.d)   // head 用 CompareHead，中尾用 CompareFive
if cmp == 0: water_d = 0
else:
  winner, loser = cmp>0 ? (i,j) : (j,i)
  water_d = DunWater(d, winner_type)   // 见下表；只按胜方牌型取档，不叠加
  pair_water[winner] += water_d
  pair_water[loser]  -= water_d
  if cmp != 0: track win/loss per dun for shoot
```

| 墩 | 胜方牌型 | 水数 |
|----|----------|------|
| head | `THREE` | 3 |
| head | 其它胜 | 1 |
| mid | `STRAIGHT_FLUSH` | 10 |
| mid | `FOUR` | 8 |
| mid | `FULL_HOUSE` | 2 |
| mid | 其它胜 | 1 |
| tail | `STRAIGHT_FLUSH` | 5 |
| tail | `FOUR` | 4 |
| tail | 其它胜 | 1 |

「其它胜」含平局以外的所有胜出；同花顺/铁支等只取上表专档，**不再 +1**。

### 8.2 打枪与全垒打

```text
Shoot(i→j) := enable_shoot
           && i,j ∈ Ordinary
           && i 在 head/mid/tail 三墩对 j 均为 cmp>0（无平局）

若 Shoot(i→j):
  pair_sum = 该对三墩已累计的有符号水数之和（对 i 为正）
  pair_sum *= 2
  写回 i/j 的对局水数（替换该对原先三墩合计）

HomeRun(i) := enable_home_run && n>=3 && enable_shoot
           && ∀ j≠i, j∈Ordinary: Shoot(i→j)
           && Ordinary 中对手数 >= 1

若 HomeRun(i):
  对每个 Ordinary 对手 j：将 i↔j 的对局水数再 *= 2
```

特殊牌型玩家 **不参与** 打枪/全垒打（不作为射击者也不作为被射击者计入 Ordinary 对局）。

### 8.3 倒水通赔（兜底）

比牌前若某座 `locked` 且非特殊路径，但 `!IsLegalArrange`：

```text
该座对每个其他玩家 v：
  水数 = -(HeadMax + MidMax + TailMax) * shoot_mult
  HeadMax=3, MidMax=10, TailMax=5
  shoot_mult = enable_shoot ? 2 : 1
  （视为三墩全负且被打枪；不做全垒打叠加）
该座 special 无效
```

正常路径确认阶段已拒绝倒水，本分支仅防御。

### 8.4 特殊牌型

#### 8.4.1 检测 `DetectSpecial(hand[13]) → SpecialType`

返回手牌能构成的 **最高** 一档；不满足则 `NONE`。优先级与水数：

| 值 | 枚举 | 条件（权威摘要） | 向每人水数 `W` |
|----|------|------------------|---------------|
| 13 | `NATURAL_DRAGON` | 13 张同一 suit，且 rank 集合 = `{0..12}` | 108 |
| 12 | `DRAGON` | rank 集合 = `{0..12}`（花色不限） | 36 |
| 11 | `TWELVE_ROYALS` | 每张 rank ∈ `{9,10,11,12}`（J/Q/K/A） | 24 |
| 10 | `THREE_SF` | **存在**划分使头为三张同花顺、中尾皆五张同花顺 | 20 |
| 9 | `THREE_FOUR` | 恰好 3 个四条 + 1 张（三分天下） | 20 |
| 8 | `ALL_BIG` | 每张 rank ≥ 6（即点数 ≥ 8；rank6=8 … rank12=A） | 10 |
| 7 | `ALL_SMALL` | 每张 rank ∈ `{0..6}`（2–8）；**不含 A** | 10 |
| 6 | `ONE_COLOR` | 全红或全黑 | 10 |
| 5 | `FOUR_THREE` | 恰好 4 个三条 + 1 张 | 6 |
| 4 | `FIVE_PAIR_THREE` | 恰好 5 个对子结构与 1 个三条：点数计数中「对数」与「三条」满足 5 pair + 1 trip（13 张） | 5 |
| 3 | `SIX_PAIR` | 恰好 6 对 + 1 单张 | 4 |
| 2 | `THREE_STRAIGHT` | **存在**划分使头三张成顺、中尾皆五张成顺（同花顺算顺） | 3 |
| 1 | `THREE_FLUSH` | **存在**划分使头三张同花、中尾皆同花（同花顺算同花） | 3 |
| 0 | `NONE` | — | — |

说明：

- `THREE_SF` / `THREE_STRAIGHT` / `THREE_FLUSH` 为存在性检测，不必等于玩家普通摆法。  
- 同时满足多档只取最高。  
- `enable_special=false` 时 `DetectSpecial` 对业务恒为 `NONE`（不可报到）。

#### 8.4.2 报到提交

```text
玩家 C2S：report_special=true
server:
  t = DetectSpecial(hand)
  if t==NONE: 拒绝（7000007）
  else: special_type=t; locked 时可同时不要求 IsLegalArrange
       可选附带展示用三墩（不校验序，或校验多重集合）
```

未报到即使 `DetectSpecial!=NONE`，仍走普通摆牌 + `IsLegalArrange`。

#### 8.4.3 特殊路径水数

```text
S = { u | Special(u) }
O = Ordinary

// 特殊 vs 特殊
for each unordered pair (a,b) in S:
  if type(a) > type(b):  // 枚举值更大 = 更强
    water[a] += W(type(a)); water[b] -= W(type(a))
  else if type(a) < type(b):
    water[b] += W(type(b)); water[a] -= W(type(b))
  // equal: 0

// 特殊 vs 普通
for each s in S:
  for each o in O:
    water[s] += W(type(s))
    water[o] -= W(type(s))

// 普通 vs 普通：§8.1–8.2
```

### 8.5 汇总与抽水

```text
gross_water[uid] = 该座全部有符号水数之和
gross_gold[uid]  = gross_water[uid] * B
rake[uid] = gross_gold[uid] > 0
          ? floor(gross_gold[uid] * rake_bp / 10000) : 0
net[uid]  = gross_gold[uid] - rake[uid]
```

幂等键：`sss:settle:{round_id}:{uid}`。  
`sum(net)+sum(rake)==0`（抽水归平台）。

### 8.6 SettlePlan（Table → RoomGame）

```text
struct PairShoot { int8_t from_seat; int8_t to_seat; };  // from 打枪 to
struct SeatSettle {
  int64_t uid;
  int32_t gross_water;
  int64_t gross_gold;
  int64_t rake;
  int64_t net;
  SpecialType special;          // NONE if ordinary
  bool home_run;
  // 展示：三墩牌、type、对每对手每墩 water、shoot 列表
};
```

---

## 9. 桌面数据模型（内存）

```text
SssTable
  room_id, round_id, template_id, cfg
  n: 2..4
  deal_start: 0..n-1
  last_water_winner: int   // -1 = 尚无上局
  phase: WaitReady|Deal|Arrange|Compare|Settle
  arrange_deadline_ms
  rng
  seats[4]:
    uid, online, ready, trusteeship
    hand[13]
    draft_head[3], draft_mid[5], draft_tail[5]
    report_special: bool
    special_type: SpecialType
    locked: bool
    final_* 同上
  compare_result / settle_plan   // 重连缓存
```

---

## 10. 模块接口

### 10.1 目录

```text
server/src/game/sss/
  card.hpp
  hand.hpp|cpp          # EvalHead/Five、Compare*、IsLegalArrange、AutoArrange
  special.hpp|cpp       # DetectSpecial
  settle.hpp|cpp        # 水数、打枪、全垒打、抽水 → SettlePlan
  config.hpp
  table.hpp|cpp
  sss_room_game.hpp|cpp
server/tests/
  sss_hand_test.cpp
  sss_table_test.cpp
proto/game_sss.proto
```

### 10.2 `SssTable`（示意）

```cpp
class SssTable {
 public:
  explicit SssTable(SssConfig cfg, std::function<uint32_t()> rng);
  void OnReady(int seat, bool ready);
  bool TryStartDeal();
  // report_special: if true, ignore ordinary order; server fills special_type
  bool SetArrange(int seat,
                  const int32_t head[3], const int32_t mid[5], const int32_t tail[5],
                  bool report_special, bool confirm);
  void OnDisconnect(int seat);
  void OnReconnect(int seat);
  void OnArrangeDeadline();
  const SettlePlan& Plan() const;
};
```

注册：

```text
GameRegistry.Register(GameId::kSss=7000, "sss", default_seats=4, factory → SssRoomGame)
```

---

## 11. 协议（msg_id = 700000 + slot）

| slot | msg_id | 方向 | message | 说明 |
|------|--------|------|---------|------|
| 1 | 700001 | S→C | `S2C_SssGameStart` | round_id、n、deal_start、自己的 13 张、timeout_s、cfg 快照 |
| 2 | 700002 | S→C | `S2C_SssArrangeState` | 各座 locked / trusteeship / reported；剩余秒 |
| 3 | 700003 | C→S | `C2S_SssArrange` | head×3、mid×5、tail×5；`report_special`；`confirm` |
| 4 | 700004 | S→C | `S2C_SssArrangeAck` | ok / 错误细节（亦可走通用 Error） |
| 5 | 700005 | S→C | `S2C_SssCompare` | 特殊报到列表；三墩每座牌面与 type；两两每墩 water；shoot / home_run |
| 6 | 700006 | S→C | `S2C_SssSettle` | 每人 gross_water/gold、rake、net、余额、special |
| 7 | 700007 | S→C | `S2C_SssSnapshot` | 重连：phase、手牌/墩草稿、reported、compare/settle 缓存、剩余秒 |
| 8 | 700008 | C→S | `C2S_SssReady` | 可选；优先复用房间 Ready |
| 9 | 700009 | C→S | `C2S_SssLeave` | 可选；优先复用房间离开 |

字段级定义以 `proto/game_sss.proto` 为准；改消息须同改本表。

### 11.1 玩法错误码

| code | 含义 |
|------|------|
| 7000001 | 非法摆牌（非本人手牌 / 张数不对） |
| 7000002 | 倒水（头≤中≤尾 不满足） |
| 7000003 | 已确认不可再改 |
| 7000004 | 非 Arrange 阶段 |
| 7000005 | 座位无效 |
| 7000006 | 未开局 / 阶段拒绝 |
| 7000007 | 无效特殊报到（手牌不构成任何特殊牌型，或 `enable_special=false`） |

走 `S2C_Error` + `ErrMessage`。

---

## 12. 对局日志

```text
round=<id> src=server|client game=sss room=<rid> uid=<u> seat=<s> ev=<ev> <detail>
```

建议 `ev`：`deal` / `arrange` / `confirm` / `special` / `trust` / `auto_arrange` / `compare` / `shoot` / `home_run` / `settle` / `disconnect` / `reconnect`。

---

## 13. 客户端（game-web）

| 项 | 要求 |
|----|------|
| 路由 | `/sss` 正式桌；`/sss-lab` 多硬件码游客同桌（`d0…`） |
| 摆牌 | 头 3 / 中 5 / 尾 5、牌型提示、倒水禁用确认、报到入口 |
| 比牌 | 先特殊报到，再按 `S2C_SssCompare` 逐墩亮牌；打枪/全垒打占位动效 |
| 结算 | 水数、打枪、特殊、抽水、净胜负 |
| 顶号 | `KickReason.LoggedInElsewhere` 不自动重连 |
| 音频 | 无强制 |
| 测试 | `frame.sss.test.ts` 覆盖 MsgId 与编解码 |

---

## 14. 验收用例（TDD 用例表）

> 一条 ID ≥ 一个自动化测试。牌例用 `C(suit,rank)`。

### 14.1 牌型与比较（`sss_hand_test` · S0）

| ID | 场景 | 期望 | 层 |
|----|------|------|----|
| T01 | 五张：同花顺 > 铁支 > 葫芦 > 同花 > 顺子 > 三条 > 两对 > 对子 > 乌龙（最小牌例链） | `CompareFive` 全序 | hand |
| T02 | `{♠T,♠J,♠Q,♠K,♠A}` vs `{♥9,♥T,♥J,♥Q,♥K}` | 前者 SF 更大 | hand |
| T03 | 顺子 A2345 < 23456 < TJQKA | `straight_top` 序 | hand |
| T04 | `{♠J,♠Q,♠K,♠A,♦2}` | 非顺 / 非 SF | hand |
| T05 | 两同型同点不同花五张乌龙（构造相同 rank 多重集） | `CompareFive==0` | hand |
| T06 | 头墩：三条 > 对子 > 乌龙 | `CompareHead` | hand |
| T07 | 头墩三张同花连续仍为 HIGH 或 PAIR/THREE（不计顺花） | 非五张 SF 逻辑 | hand |
| T08 | 合法：弱头对 < 中顺 < 尾葫芦（固定 CardId） | `IsLegalArrange` true | hand |
| T09 | 将 T08 头尾对调导致倒水 | false | hand |
| T10 | 头三条 vs 中两对：`CmpOrder` 头 > 中 | 倒水 | hand |
| T11 | 头对子 vs 中对子，头对 rank 更小 | 合法 | hand |
| T12 | 13 张多重集合不匹配 | `IsLegalArrange` false | hand |

### 14.2 代摆与特殊（`sss_hand_test` · S1 前半）

| ID | 场景 | 期望 | 层 |
|----|------|------|----|
| T13 | 固定 13 张 `AutoArrange` 两次 | 位集相等且合法；`special` 不置位 | hand |
| T14 | 同上：tail ≥ 某手写合法弱尾 | `CompareFive≥0` | hand |
| T15 | 一条龙手牌 `DetectSpecial` | `DRAGON`，W=36 | special |
| T16 | 同花一条龙 | `NATURAL_DRAGON` 优先于 `DRAGON` | special |
| T17 | 全小含 A | 不得判 `ALL_SMALL` | special |
| T18 | 全小仅 2–8 | `ALL_SMALL` | special |
| T19 | 六对半 | `SIX_PAIR` | special |

### 14.3 结算（`sss_hand_test` · S1 后半）

| ID | 场景 | 期望 | 层 |
|----|------|------|----|
| T20 | 2 人，仅头墩 seat0 胜普通，`B=100` | water +1/-1；gold ±100 | settle |
| T21 | 头墩冲三胜 | water ±3 | settle |
| T22 | 中墩同花顺胜 | water ±10 | settle |
| T23 | 尾墩铁支胜 | water ±4 | settle |
| T24 | 三墩全胜（无平），`enable_shoot` | 对局水数合计 ×2 | settle |
| T25 | 4 人 seat0 对其余三人皆打枪 | `home_run`；对每人水数在打枪后再 ×2 | settle |
| T26 | 2 人局即使三墩全胜 | 无 `home_run` | settle |
| T27 | 特殊一条龙 vs 普通 | 龙 +36 水/人；不产生 shoot | settle |
| T28 | 两特殊：清龙 vs 一条龙 | 清龙向龙收 108 | settle |
| T29 | 两特殊同型 | 互为 0 | settle |
| T30 | 1 特殊 + 2 普通：普通之间仍比墩；特殊各向两普通收费 | 水数拆分正确 | settle |
| T31 | `ApplyRake(gross=1000, bp=500)` | rake=50, net=950 | settle |
| T32 | `gross≤0` | rake=0 | settle |
| T33 | 整局 `sum(net)+sum(rake)==0` | 守恒 | settle |
| T34 | `enable_shoot=false` | 三墩全胜不翻倍 | settle |
| T35 | `enable_special=false` 时报到 | 拒绝 / Detect 业务 NONE | settle |

### 14.4 桌逻辑（`sss_table_test` · S2）

| ID | 场景 | 期望 | 层 |
|----|------|------|----|
| T40 | n=4，固定 rng，Ready→Deal | 每人 13 张；52 张互异；phase=Arrange | table |
| T41 | 合法三墩 confirm | locked | table |
| T42 | 倒水 confirm | 拒绝；未 lock | table |
| T43 | 已 lock 再改 | 拒绝 7000003 | table |
| T44 | 有效特殊报到 confirm | locked；`special!=NONE`；不要求墩序 | table |
| T45 | 无效报到 | 拒绝 7000007 | table |
| T46 | `OnArrangeDeadline` | 全员 lock；合法普通摆；无自动报到 | table |
| T47 | Arrange 中 Disconnect | trusteeship；未 lock 直至 Deadline | table |
| T48 | 上局净水胜者 seat=2；下局 Deal | `deal_start==(2+1)%n` | table |
| T49 | `BuildSnapshot` | phase、hand、draft、reported、remain_s、locked | table |

### 14.5 协议 / 冒烟（S3–S4）

| ID | 场景 | 期望 | 层 |
|----|------|------|----|
| T50 | vitest：slot→msg_id 与关键编解码 | 与 proto 一致 | front |
| T51 | 4 游客匹配 `game_id=7000` | GameStart；13 张 | smoke |
| T52 | 全员确认 → Settle | 余额变化；ledger `sss:settle:*` | smoke |
| T53 | 无人确认至超时 | 仍 Compare/Settle；无特殊 | smoke |
| T54 | `/sss-lab` 多 `d0` 同桌 | 互见 Compare | smoke/手工 |

### 14.6 切片出门检查表

| 切片 | 必绿 | 仍禁止出现 |
|------|------|------------|
| S0 | T01–T12 | `sss_room_game.cpp` 大段逻辑 |
| S1 | T13–T19, T20–T35 | 网络、Wallet |
| S2 | T40–T49 | proto 依赖进 table 单测 |
| S3 | T50–T53 | 无幂等直接改余额 |
| S4 | T54 + 重连手工 | — |

---

## 15. 非目标（本 SPEC 首发不做）

- 旁观  
- 大小王、五同、马牌  
- 朋友场房卡、冲榜、庄闲下注桌  
- 录像产品化  
- `game_id≠7000` 变体  
- 复用比鸡规则库  
- 无单测情况下先联调页面  

---

## 16. 文档与实现顺序（强制 TDD 流水线）

```text
Phase 0  文档
  ├─ 本 SPEC（已定稿）
  ├─ （可选）十三水游戏规则.md —— 数值引用 §4/§8，不替代用例表
  └─ AGENTS / 主 SPEC / game_ids.hpp / errors.hpp 登记 7000（可与 S0 同 PR）

Phase 1  S0 Red→Green
  ├─ CMake 增加 sss_hand_test
  ├─ T01–T12（红）→ hand 至绿

Phase 2  S1 Red→Green
  ├─ T13–T35（红）→ AutoArrange + special + settle 至绿

Phase 3  S2 Red→Green
  ├─ sss_table_test T40–T49 → SssTable 至绿（无 Wallet）

Phase 4  S3
  ├─ proto/game_sss.proto + proto:gen + T50
  ├─ SssRoomGame + Registry + lobby 种子
  └─ sss_smoke.mjs T51–T53

Phase 5  S4
  └─ Snapshot、/sss + /sss-lab、T54
```

**变更纪律**：

```text
§4 / §8 正文 → §14 用例行 → *_test.cpp → 生产代码
```

---

## 17. 修订记录

| 版本 | 日期 | 说明 |
|------|------|------|
| V1.0 | 2026-10-12 | 初稿：对齐需求 V1.1；强制 TDD；锁定编码、CmpOrder、水数/打枪/全垒打、特殊牌型、协议 7000xx |

**关联**：

- 需求：[十三水游戏-需求文档.md](./十三水游戏-需求文档.md)
- 规则说明：`docs/十三水游戏规则.md`（待写；数值以本 SPEC §4/§8 为准）
- 相近玩法（勿混）：[比鸡游戏-SPEC.md](./比鸡游戏-SPEC.md)
