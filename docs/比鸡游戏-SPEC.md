# 比鸡游戏 — 技术规格说明书（SPEC）

| 项目 | 内容 |
|------|------|
| 文档版本 | V1.1 |
| 创建日期 | 2026-10-10 |
| 文档状态 | 可开发（**强制 TDD**；未过单测不得合入玩法逻辑） |
| 依据文档 | [比鸡游戏-需求文档.md](./比鸡游戏-需求文档.md) V1.1 |
| 关联主 SPEC | [棋牌游戏服务端-SPEC.md](./棋牌游戏服务端-SPEC.md)（帧格式、鉴权、钱包、`IRoomGame`/`GameRegistry`） |
| 适用范围 | `pandora-server` 比鸡模块、`proto/game_biji.proto`、`game-web` `/biji` 与 `/biji-lab` |

> **需求冲突**：玩法语义以需求文档为准；本 SPEC 落实为牌编码、比较键、状态机、结算公式、协议、模块接口与 **测试先行** 约束。主仓硬约束（单体、IOCP、钱包幂等、MySQL 权威）不变。  
> **明确不做**：旁观；5 人无王；带大小王变体；朋友场房卡；客户端自报牌型/输赢。

---

## 1. 规格总览

### 1.1 定位

| 项 | 值 |
|----|----|
| `game_id` | **6000**（变体预留 6001–6099） |
| 短名 | `biji`（`LogRound` / ClientTrace） |
| 模块名 | `BijiClassic`（九张三墩 · 无王） |
| 人数 | **2–4**（模板 `players`；最大 4） |
| 代码落点 | `server/src/game/biji/` |
| 适配器 | `BijiRoomGame`（`IRoomGame`） |
| 规则内核 | `BijiTable` + `biji/hand.hpp`（牌型/比较/代摆） |
| 协议 | `proto/game_biji.proto` |
| msg_id 段 | **600000 – 600099**（`msg = 6000 * 100 + slot`） |
| 玩法错误码 | **6000000 – 6000099**（`err = 6000 * 1000 + slot`） |

### 1.2 与主框架的关系

```text
Match / RoomManager
        │  game_id=6000
        ▼
   GameRegistry → BijiRoomGame → BijiTable（每桌一实例）
        │
        ├─ SessionHub 推送 6000xx
        ├─ WalletService.Adjust（终局结算 + 赢家抽水；ledger 幂等）
        └─ Admin.RecordRound / Activity 钩子（可选）
```

- 入座、匹配、准备、互踢、倒计时框架复用平台；**禁止**再往 `Room` 加比鸡平行字段。
- 发牌、牌型、倒水校验、比牌、吃喜、金币 delta **仅服务端权威**；客户端上报墩牌与确认。
- 对局轨迹：`game=biji`，客户端经 `C2S_ClientTrace`（9001）上报。

### 1.3 交付切片（TDD 对齐 · 门禁）

| 切片 | 范围 | 先写测试（Red） | 再实现至绿 | 出门条件 |
|------|------|-----------------|------------|----------|
| **B0** | `CardId`、`EvalDun`、`CompareDun`、`IsLegalArrange` | `biji_hand_test` T01–T07 | `card.hpp` + `hand.*` | T01–T07 全绿；无 RoomGame |
| **B1** | `AutoArrange`、墩结算、吃喜、抽水 | 同文件 T08–T09、T20–T27 | `hand` 代摆 + `settle.*` | T08–T27 全绿；仍无网络 |
| **B2** | `BijiTable` 状态机 | `biji_table_test` T30–T37 | `table.*` + `config.hpp` | T30–T37 全绿；无钱包/无 proto |
| **B3** | proto、`BijiRoomGame`、Registry、模板种子 | 先补 `frame.biji.test.ts` MsgId；再 `biji_smoke` 骨架 | `game_biji.proto` + RoomGame | T40–T42 冒烟绿；账变幂等 |
| **B4** | 重连、lab、Admin | T37 加深 + T43 | Snapshot / `/biji-lab` | lab 多端同桌手工勾选 |

**硬门禁（违反即打回）**

1. **禁止**先写完整 `BijiRoomGame` / 前端桌面再补单测。  
2. **禁止**在 B0/B1 未绿时合入 B2+ 玩法逻辑。  
3. 每条用例 ID（§14）必须在测试函数名或注释中出现（如 `TestT01_ThreeAces`）。  
4. 改 §4/§8 公式或比较序：**先改 §14 用例 → 改测试（允许先红）→ 再改生产代码**。  
5. 细则见 **§2**。

---

## 2. TDD 工作流（强制 · 落地规范）

> 对齐捕鱼 `fish_math_test` / `fish_table_test` 实践：轻量 `Expect` 宏、用例函数 `TestTxx`、CMake 独立 `add_executable`、**不依赖真服**完成规则层。

### 2.1 Red → Green → Refactor

```text
1. 打开 §14，选下一条未实现用例 ID（按 B0→B1→B2→B3 顺序，禁止跳号堆功能）
2. Red：在对应 *_test.cpp 增加 TestTxx，断言最终行为；编译运行应失败
3. Green：只写使该用例通过的最小生产代码（可暂时丑陋）
4. Refactor：提取公共比较/结算，保持全绿
5. 提交粒度建议：单切片或数条用例一组；PR 描述列出覆盖的 Txx
```

**反模式（禁止）**

| 反模式 | 正确做法 |
|--------|----------|
| 先实现整桌再写测试 | 先 `hand` 纯函数测通 |
| 单测里 `Sleep` 等倒计时 | `OnArrangeDeadline()` / 注入 `now_ms` |
| Table 内直接调 MySQL/Wallet | Table 产出 `SettlePlan`，RoomGame 再 Adjust |
| 用日志字符串做断言 | 断言返回值 / `SettlePlan` 字段 |
| 一次 PR 塞满 B0–B3 | 按切片出门；B0 可单独合入 |

### 2.2 测试二进制与 CMake

| 目标 | 源文件 | 链接生产代码（建议） | 覆盖用例 |
|------|--------|----------------------|----------|
| `biji_hand_test` | `server/tests/biji_hand_test.cpp` | `hand.cpp` `settle.cpp`（若拆分） | T01–T09, T20–T27 |
| `biji_table_test` | `server/tests/biji_table_test.cpp` | 上表 + `table.cpp` | T30–T37 |
| vitest | `game-web/src/**/frame.biji.test.ts` | — | MsgId / 编解码 |
| 冒烟 | `server/scripts/biji_smoke.mjs` | 真服 | T40–T43 |

CMake 登记（与 `fish_*_test` 同风格，实现时写入 `server/CMakeLists.txt`）：

```cmake
add_executable(biji_hand_test
  tests/biji_hand_test.cpp
  src/game/biji/hand.cpp
  src/game/biji/settle.cpp
)
target_include_directories(biji_hand_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_executable(biji_table_test
  tests/biji_table_test.cpp
  src/game/biji/hand.cpp
  src/game/biji/settle.cpp
  src/game/biji/table.cpp
)
target_include_directories(biji_table_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
```

本地命令：

```powershell
cmake --build server\build --config Release --target biji_hand_test
cmake --build server\build --config Release --target biji_table_test
server\build\Release\biji_hand_test.exe
server\build\Release\biji_table_test.exe
cd game-web; npm test
# 仅 B3+ 且服务已启动：
node server\scripts\biji_smoke.mjs
```

**CI / 合入建议**：`biji_hand_test` + `biji_table_test` 作为玩法 PR 必跑；冒烟可夜间或手工。

### 2.3 测试夹具约定

与 `fish_math_test` 相同风格（ASCII 注释，避免 C4819）：

```cpp
// server/tests/biji_hand_test.cpp
static int fails = 0;
void Expect(bool cond, const char* msg);

// Card helpers (must match SPEC §3)
constexpr int32_t C(int suit, int rank) { return suit * 13 + rank; } // suit 0..3, rank 0=2 .. 12=A
// Examples: C(3,12)=♠A, C(2,11)=♥K, C(0,0)=♦2
```

| 约定 | 要求 |
|------|------|
| 用例命名 | `void TestT01_ThreeAcesBeatsTripK()` 或注释含 `T01` |
| 牌面构造 | 只用 `C(suit,rank)` / `CardId`，禁止魔法十进制无注释 |
| 比较断言 | `CompareDun(a,b) > 0` 等，不依赖字符串牌型名（可另断言 `EvalDun().type`） |
| 结算断言 | 检查 `delta[]` / `rake` / `net` 精确值；`sum(delta)+sum(rake)==0`（抽水归平台时：`sum(net)+sum(rake)==0`） |
| Table RNG | 固定种子或队列 rng；T30 断言发牌无重叠、张数 9 |
| 时间 | `table.OnArrangeDeadline()` 显式触发；禁止 `std::this_thread::sleep` |

### 2.4 可测性设计（生产代码必须遵守）

| 点 | 要求 |
|----|------|
| 纯函数层 | `EvalDun` / `CompareDun` / `IsLegalArrange` / `AutoArrange` / `ComputeSettle` **无 I/O** |
| RNG | `BijiTable(cfg, rng)` 注入；单测可复现洗牌与首局 `deal_start` |
| 时间 | `SetNowMs` 或外部调用 `OnArrangeDeadline`；deadline 存绝对 ms |
| 钱包 | Table → `SettlePlan`；仅 `BijiRoomGame` 调 `WalletService` |
| 配置 | `BijiConfig` 内存构造；单测不连 MySQL |
| 事件 | 可选 `OutEvent` sink 供 table 测断言广播意图；不强制 |
| 日志 | `LogRound(..., "biji", ...)`；**断言不读日志文件** |

### 2.5 切片内推荐实现顺序（逐条 Txx）

**B0（hand）**

```text
T01 → EvalDun(THREE) + CompareDun
T02 → 六种 type 全覆盖最小牌例
T03 → straight_top（A23 / QKA）
T04 → KA2 非顺
T05 → suit_kick
T06 / T07 → IsLegalArrange
```

**B1（arrange + settle）**

```text
T08 → AutoArrange 确定性
T09 → 尾墩偏好（固定手牌 vs 已知合法弱尾对照）
T20–T22 → Mult(place) 与 2/3/4 人
T23–T25 → 吃喜互斥与叠加
T26–T27 → rake
```

**B2（table）**

```text
T30 Deal → T31 Confirm → T32 倒水 → T33 锁后拒绝
→ T34 Deadline 代摆 → T35 断线不即代 → T36 起发轮转 → T37 Snapshot 字段
```

**B3–B4**：proto 生成 → Registry → lobby 种子 → smoke T40–T42 → lab T43。

### 2.6 Definition of Done（玩法 PR）

| # | 检查项 |
|---|--------|
| 1 | PR 描述列出本 PR 覆盖的用例 ID（§14） |
| 2 | `biji_hand_test` / `biji_table_test`（本切片涉及者）退出码 0 |
| 3 | 无「只改 cpp、不改测试」的规则行为变更 |
| 4 | 新错误码 / msg_id 已写入本 SPEC §11 与 `errors.hpp` |
| 5 | C++ 新增注释为 ASCII（AGENTS 硬约束） |
| 6 | 未引入一连接一线程；未往 `Room` 加比鸡平行字段 |
| 7 | B3+：幂等键 `biji:settle:{round}:{uid}`；重复 Settle 不双扣 |

### 2.7 回归策略

- 任意改动 `hand`/`settle`：**必须**全量跑 `biji_hand_test`。  
- 改状态机：**必须**全量跑 `biji_table_test`。  
- 改协议：先改 `proto` + §11 表 + `frame.biji.test.ts`，再改 RoomGame。  
- 规则争议：以需求文档为准，同步改本 SPEC §4/§8 与 §14 用例后，再改代码。

---

## 3. 牌编码

### 3.1 CardId（协议 `int32`，范围 0–51）

一副去掉大小王共 52 张：

```text
suit  ∈ {0,1,2,3}   // 0=♦ 方块, 1=♣ 梅花, 2=♥ 红桃, 3=♠ 黑桃
rank  ∈ {0..12}     // 0=2, 1=3, …, 8=T(10), 9=J, 10=Q, 11=K, 12=A
CardId = suit * 13 + rank          // 0..51
```

**比较序（权威）**

```text
RankValue(c) := rank(c)            // 数值越大越大；A=12 最大（顺子特例除外）
SuitValue(c) := suit(c)            // 黑桃3 > 红桃2 > 梅花1 > 方块0
```

辅助：

```text
IsRed(c)  := suit(c) ∈ {0, 2}     // ♦♥
IsBlack(c):= suit(c) ∈ {1, 3}     // ♣♠
```

### 3.2 显示（客户端）

| rank | 标签 |
|------|------|
| 0..7 | 2..9 |
| 8 | 10 |
| 9..12 | J,Q,K,A |

花色用 Unicode 或资源图；协议只传 `CardId`。

---

## 4. 单墩牌型与比较键

### 4.1 牌型枚举（大 → 小）

| 值 | 名 | 条件 |
|----|----|------|
| 5 | `THREE` | 三张同 rank |
| 4 | `STRAIGHT_FLUSH` | 同花且成顺 |
| 3 | `FLUSH` | 同花非顺 |
| 2 | `STRAIGHT` | 成顺非同花 |
| 1 | `PAIR` | 恰好一对 |
| 0 | `HIGH` | 其余 |

### 4.2 顺子定义

三张 rank **在圆周上不绕圈**的连续：

- 合法：`A23`（ranks 12,0,1）视为最小顺，**比较键高牌记为 3（即 rank 值语义上的「3」作为顶）**——实现用内部 `straight_top`：
  - `A23` → `straight_top = -1`（最小）
  - `234` → `0`（顶为 4 的前驱链：top=rank of highest in natural = 2 对应牌 4 → 用最高自然 rank；统一为：）
  - 标准：`234…QKA`，`straight_top = max(rank)` 对非 A23；**仅 A23：`straight_top = -1`**；**QKA：`straight_top = 12`**。
- 非法：`KA2`、`JQA` 以外的绕圈（`QKA` 合法；`KA2` 非法）。

同花顺 = 同花 ∧ 顺子（含 A23 / QKA）。

### 4.3 比较键 `DunKey`

对三张牌算出：

```text
struct DunKey {
  int8_t type;          // 0..5
  int8_t primary;       // THREE: rank; SF/STRAIGHT: straight_top; PAIR: pair_rank; FLUSH/HIGH: high1
  int8_t secondary;     // PAIR: kicker; FLUSH/HIGH: high2; else 0
  int8_t tertiary;      // FLUSH/HIGH: high3; else 0
  int8_t suit_kick;     // 打破平局：取「用于定型的关键牌」中 SuitValue 最大者；见下
};
```

**同型比较序**（字典序，先比 type）：

| type | primary | secondary | tertiary | suit_kick |
|------|---------|-----------|----------|-----------|
| THREE | 三条 rank | 0 | 0 | 三张中最大 SuitValue |
| STRAIGHT_FLUSH | straight_top | 0 | 0 | 顺子中最大 SuitValue 那张的花色（同 top 比花） |
| FLUSH | 降序 rank[0] | rank[1] | rank[2] | 最大 rank 牌的花色；若同再比次大牌花色…（实现：逐张比完点后，从大到小比各张 SuitValue） |
| STRAIGHT | straight_top | 0 | 0 | 同 SF |
| PAIR | 对子 rank | 单张 rank | 0 | 对子两张中较大 SuitValue；再比单张 SuitValue |
| HIGH | 降序 rank[0..2] | — | — | 从大到小比各张 SuitValue |

`CompareDun(a,b) → -1/0/1`：按字段字典序；因花色参与，**桌内同墩比较结果不得为 0**（52 张唯一牌，三张组合在完整键下可区分；若极端相等则按 `seat` 升序打破，仅作防御）。

### 4.4 倒水判定

```text
IsLegalArrange(head, mid, tail) :=
  CompareDun(head, mid) <= 0 && CompareDun(mid, tail) <= 0
```

即 **头 ≤ 中 ≤ 尾**。

---

## 5. 房间模板配置

`room_template.game_id=6000`。开局随 `S2C_BijiGameStart` 下发只读快照。

| 键 | 类型 | 默认 | 说明 |
|----|------|------|------|
| `base_score` | int64 | 模板底分 | 记 `B` |
| `players` | int | 2..4 | 匹配凑齐人数；非 2–4 拒开局 |
| `min_gold` / `max_gold` | int64 | 模板 | 入场校验 |
| `rake_bp` | int | 场次 | 赢家抽水基点 |
| `arrange_timeout_s` | int | 45 | 摆牌倒计时 |
| `compare_reveal_ms` | int | 800 | 客户端逐墩展示建议间隔（服务端可一次下发全量） |
| `enable_chixi` | bool | true | 吃喜开关 |

种子示例（实现时写入 `schema` / lobby 引导）：

```text
(id=5, game_id=6000, name='比鸡初级场', players=4, base_score=100, rake_bp=<平台默认>, ...)
```

`DefaultSeatsForGame(6000) = 4`（上限）；实际人数 = 模板 `players`。

---

## 6. 桌内状态机

```text
WaitReady
    │ 满 players 且全员 Ready
    ▼
Deal ──── 洗牌、定起发座位、发每人 9 张
    │
    ▼
Arrange ── 提交/改墩/确认；断线仅标托管
    │ 全员 locked 或 deadline
    ▼
Compare ── 算分；广播头→中→尾结果（可一条消息含三墩）
    │
    ▼
Settle ── Wallet.Adjust；推送结算 ──► WaitReady
                ↘ 全离 / 解散 ──► Closed
```

| 状态 | 行为 |
|------|------|
| WaitReady | 入座、准备；结算后清除本局牌面；起发座位保留至下局 Deal |
| Deal | 瞬时完成进 Arrange；私发手牌 |
| Arrange | 接受 `C2S_BijiArrange` / `Confirm`；广播他人是否已确认（不广播墩内容） |
| Compare | 只读计算结果并下发；不接受改牌 |
| Settle | 账变；`S2C_BijiSettle`；回 WaitReady |

### 6.1 起发座位

```text
首局：deal_start = rng() % n
之后：deal_start = (last_tail_winner_seat + 1) % n
发牌顺序：从 deal_start 起逆时针（座位号递增模 n）一人一张，共 9 轮
```

`last_tail_winner_seat`：上局尾墩比较第一名座位。

### 6.2 托管与超时

- Arrange 断线：`trusteeship=true`，**不立即代摆**。  
- `deadline` 到达：对每个未 `locked` 座位执行 `AutoArrange(hand)` → `locked=true`。  
- 重连：下发阶段、剩余秒、自己的手牌/草稿墩/是否已锁；他人仅确认状态。  
- **不重 Arm** 已走过的 deadline（对齐杭麻：下发剩余秒）。

### 6.3 离开

- WaitReady：直接离座（平台既有）。  
- 开局中离开：标托管，局结束再按平台规则处理离座；不中途作废整桌（除非只剩不足 2 人——首发：开局后人数固定，离线仍占座至 Settle）。

---

## 7. 代摆启发式（权威 · 可单测）

对 9 张手牌，枚举所有有序三墩划分：

```text
选 3 张作 head，再选 3 张作 mid，剩余 3 张作 tail
共 C(9,3)*C(6,3)*C(3,3) = 1680 种
过滤 IsLegalArrange
在合法集中取字典序最大键：
  最大化 DunKey(tail)，其次 mid，其次 head
若合法集为空（数学上不应出现）：按 CardId 排序后固定切 [0..2][3..5][6..8] 并若非法则暴力找到任一合法（防御）
```

确定性：同手牌同结果；禁止随机代摆。

---

## 8. 结算公式（权威）

符号：`B = base_score`，`n = 人数`，座位 `0..n-1`。

### 8.1 单墩名次倍数

相对该墩第一名的名次 `place`（2..n）：

```text
Mult(place) := 2 * (place - 1)     // 2→2, 3→4, 4→6
```

2/3 人局自然同比缩放（无 place=4 则无 6 倍档）。

对墩 `d ∈ {head,mid,tail}`：

```text
winner = argmax seats by DunKey(d)
for each loser ≠ winner:
  pay = Mult(place_of(loser)) * B
  delta[loser]  -= pay
  delta[winner] += pay
```

### 8.2 吃喜（`enable_chixi`）

先判定三墩牌型，再判全色。

**三墩档（互斥，取最高）**

| 条件 | `chixi_type` | `mult` |
|------|--------------|--------|
| 三墩均为 THREE | `TRIPLE_THREE` | 12 |
| 三墩均为 STRAIGHT_FLUSH | `TRIPLE_SF` | 10 |
| 三墩均为 FLUSH 或 SF（且非 TRIPLE_SF） | `TRIPLE_FLUSH` | 6 |
| 三墩均为 STRAIGHT 或 SF（且非更高三墩档） | `TRIPLE_STRAIGHT` | 6 |

优先级：`TRIPLE_THREE > TRIPLE_SF > TRIPLE_FLUSH > TRIPLE_STRAIGHT`。  
「三墩同花」：每墩 `type ∈ {FLUSH, STRAIGHT_FLUSH}`。  
「三墩顺子」：每墩 `type ∈ {STRAIGHT, STRAIGHT_FLUSH}`。  
同时可报同花与顺子时：若三墩皆 SF → 只记 `TRIPLE_SF`；若皆同花但非皆 SF → `TRIPLE_FLUSH`；若皆顺但非皆同花 → `TRIPLE_STRAIGHT`。

**全色档（可与三墩档叠加）**

| 条件 | `chixi_type` | `mult` |
|------|--------------|--------|
| 9 张皆黑（♣♠） | `ALL_BLACK` | 4 |
| 9 张皆红（♦♥） | `ALL_RED` | 4 |

对每个吃喜触发者 `u`、每个倍数 `M`：

```text
for each other v:
  delta[v] -= M * B
  delta[u] += M * B
```

多人各自独立结算。

### 8.3 汇总与抽水

```text
gross[uid] = sum(墩结算) + sum(吃喜)
// 仅对 gross > 0 的赢家抽水：
rake[uid]  = floor(gross[uid] * rake_bp / 10000)   // gross<=0 → 0
net[uid]   = gross[uid] - rake[uid]
```

账变：`WalletService.Adjust(uid, net[uid], idempotent_key)`。  
幂等键：`biji:settle:{round_id}:{uid}`。  
抽水可记 ledger remark / 子类型，须可审计。

### 8.4 SettlePlan（Table → RoomGame）

```text
struct SeatSettle {
  int64_t uid;
  int64_t dun_delta[3];     // 头中尾
  int64_t chixi_delta;
  int64_t rake;
  int64_t net;              // 入账
  // 展示：各墩 type、名次、吃喜类型列表
};
```

---

## 9. 桌面数据模型（内存）

```text
BijiTable
  room_id, round_id, template_id, cfg
  n: 2..4
  deal_start: 0..n-1
  last_tail_winner: int   // -1 = 尚未有上局
  phase: WaitReady|Deal|Arrange|Compare|Settle
  arrange_deadline_ms
  deck_seed / rng
  seats[4]:
    uid, online, ready, trusteeship
    hand[9]                 // Deal 后填充；Settle 后清空
    draft_head/mid/tail[3]  // 未确认可改
    locked: bool
    final_head/mid/tail[3]  // locked 后
  compare_result            // 算完缓存供重连
```

---

## 10. 模块接口

### 10.1 目录

```text
server/src/game/biji/
  card.hpp              # CardId 编解码、IsRed
  hand.hpp|cpp          # EvalDun、CompareDun、IsLegalArrange、AutoArrange
  settle.hpp|cpp        # 墩结算、吃喜、抽水 → SettlePlan
  config.hpp
  table.hpp|cpp         # BijiTable
  biji_room_game.hpp|cpp
server/tests/
  biji_hand_test.cpp
  biji_table_test.cpp
proto/game_biji.proto
```

### 10.2 `BijiTable`（示意）

```cpp
class BijiTable {
 public:
  explicit BijiTable(BijiConfig cfg, std::function<uint32_t()> rng);
  void OnReady(int seat, bool ready);
  bool TryStartDeal();  // all ready → Deal→Arrange
  bool SetArrange(int seat, const int32_t head[3], const int32_t mid[3],
                  const int32_t tail[3], bool confirm);
  void OnDisconnect(int seat);
  void OnReconnect(int seat);
  void OnArrangeDeadline();  // auto-arrange unlocked
  // After Compare computed:
  const SettlePlan& Plan() const;
};
```

`BijiRoomGame`：`Handle` 解 proto；Arrange deadline 挂现有定时器；`Plan()` 转 `Wallet.Adjust`；注册：

```text
GameRegistry.Register(GameId::kBiji=6000, "biji", default_seats=4, factory → BijiRoomGame)
```

Lobby：`NormalizeTemplateGameId` / 种子模板 `id=5`（若 1–4 已被 ddz/hzmj/phz/fish 占用）。

---

## 11. 协议（msg_id = 600000 + slot）

| slot | msg_id | 方向 | message | 说明 |
|------|--------|------|---------|------|
| 1 | 600001 | S→C | `S2C_BijiGameStart` | round_id、n、deal_start、自己的 9 张、timeout_s、cfg 快照 |
| 2 | 600002 | S→C | `S2C_BijiArrangeState` | 各座是否 locked / trusteeship；剩余秒 |
| 3 | 600003 | C→S | `C2S_BijiArrange` | head/mid/tail 各 3×CardId；`confirm` bool |
| 4 | 600004 | S→C | `S2C_BijiArrangeAck` | ok / 错误细节（倒水等，亦可走通用 Error） |
| 5 | 600005 | S→C | `S2C_BijiCompare` | 三墩：每座牌面、type、名次、本墩 delta |
| 6 | 600006 | S→C | `S2C_BijiSettle` | 每人 gross/chixi/rake/net、余额、吃喜列表 |
| 7 | 600007 | S→C | `S2C_BijiSnapshot` | 重连：phase、手牌/墩草稿、compare/settle 缓存、剩余秒 |
| 8 | 600008 | C→S | `C2S_BijiReady` | 可选；若复用房间通用 Ready 则可省略 |
| 9 | 600009 | C→S | `C2S_BijiLeave` | 可选；复用房间离开则可省略 |

字段级定义以 `proto/game_biji.proto` 为准；改消息须同改本表。  
准备/离开优先复用房间层通用消息，玩法层只保留对局专用 slot。

### 11.1 玩法错误码

| code | 含义 |
|------|------|
| 6000001 | 非法摆牌（非本人手牌 / 张数不对） |
| 6000002 | 倒水（头≤中≤尾 不满足） |
| 6000003 | 已确认不可再改 |
| 6000004 | 非 Arrange 阶段 |
| 6000005 | 座位无效 |
| 6000006 | 未开局 / 阶段拒绝 |

走 `S2C_Error` + `ErrMessage`。

---

## 12. 对局日志

```text
round=<id> src=server|client game=biji room=<rid> uid=<u> seat=<s> ev=<ev> <detail>
```

建议 `ev`：`deal` / `arrange` / `confirm` / `trust` / `auto_arrange` / `compare` / `chixi` / `settle` / `disconnect` / `reconnect`。

---

## 13. 客户端（game-web）

| 项 | 要求 |
|----|------|
| 路由 | `/biji` 正式桌；`/biji-lab` 多硬件码游客同桌（对齐 hzmj-lab，`d0…`） |
| 摆牌 | 三墩放置区、实时牌型提示、倒水禁用确认 |
| 比牌 | 按 `S2C_BijiCompare` 逐墩亮牌与飘分 |
| 结算 | 展示墩分、吃喜、抽水、净胜负 |
| 顶号 | `KickReason.LoggedInElsewhere` 不自动重连 |
| 音频 | 无强制 |
| 测试 | `frame.biji.test.ts` 覆盖 MsgId 与编解码 |

---

## 14. 验收用例（TDD 用例表）

> 实现时：**一条 ID ≥ 一个自动化测试**。牌例用 `C(suit,rank)`（§2.3）。未列出的边界若在实现中发现，先增补本表再写测试。

### 14.1 牌型与比较（`biji_hand_test` · B0）

| ID | 场景（牌例） | 期望 | 层 |
|----|--------------|------|----|
| T01 | THREE: `{♠A,♥A,♦A}` vs `{♠K,♥K,♦K}` | 前者 > 后者；`type==THREE` | hand |
| T02 | 最小牌例链：SF > Flush > Straight > Pair > High | `CompareDun` 全序 | hand |
| T03 | 顺子 `{♠A,♦2,♥3}` vs `{♠4,♦5,♥6}` vs `{♠Q,♦K,♥A}` | A23 < 456 < QKA | hand |
| T04 | `{♠K,♦A,♥2}` | `type` 不是 STRAIGHT / SF | hand |
| T05 | HIGH 同点：`{♠A,♠K,♠Q}` vs `{♥A,♥K,♥Q}`（若构不成花顺则造同点不同花散牌） | 黑桃侧更大 | hand |
| T06 | head=弱对、mid=中顺、tail=强三条（具体 CardId 固定在测试常量） | `IsLegalArrange` true | hand |
| T07 | 将 T06 的 head/tail 对调 | `IsLegalArrange` false | hand |

### 14.2 代摆与结算（`biji_hand_test` · B1）

| ID | 场景 | 期望 | 层 |
|----|------|------|----|
| T08 | 固定 9 张手牌调用 `AutoArrange` 两次 | 结果位集相等且合法 | hand |
| T09 | 同上手牌：AutoArrange 的 tail ≥ 某一事先手写的合法弱尾摆法的 tail | `CompareDun` ≥ 0 | hand |
| T20 | 4 座单墩键严格序 seat0>1>2>3，`B=100` | delta: +1200,-200,-400,-600 | settle |
| T21 | 2 座，`B=100`，seat0 胜 | +200 / -200 | settle |
| T22 | 3 座，`B=100`，名次 0>1>2 | +600 / -200 / -400 | settle |
| T23 | 某座三墩皆 SF，另 3 人无吃喜；忽略墩分只测吃喜 | 该座 chixi=+30B；其余各 -10B | settle |
| T24 | 三墩皆 SF | `chixi_types` 仅含 `TRIPLE_SF`，无 FLUSH/STRAIGHT 档 | settle |
| T25 | 三墩皆同花（非皆 SF）+ 9 张全红 | `TRIPLE_FLUSH` 与 `ALL_RED` 两笔均计入 | settle |
| T26 | `ApplyRake(gross=1000, bp=500)` | rake=50, net=950 | settle |
| T27 | `gross=0` 与 `gross=-100` | rake=0 | settle |
| T28 | 一局 `ComputeSettle` 后 `sum(net)+sum(rake)==0` | 守恒 | settle |

### 14.3 桌逻辑（`biji_table_test` · B2）

| ID | 场景 | 期望 | 层 |
|----|------|------|----|
| T30 | n=4，固定 rng，全员 Ready→Deal | 每人 9 张；36 张互异；phase=Arrange | table |
| T31 | 合法三墩 `confirm=true` | `locked`；未确认座仍可改 | table |
| T32 | 倒水 `confirm=true` | 返回 false / err 倒水；`locked==false` | table |
| T33 | 已 lock 再 `SetArrange` | 拒绝（对应 6000003） | table |
| T34 | 无人确认，`OnArrangeDeadline` | 全员 lock；各墩合法；可 `Plan()` | table |
| T35 | Arrange 中 `OnDisconnect(seat)` | `trusteeship`；**未** lock，直至 Deadline | table |
| T36 | 第一局尾墩胜者 seat=2；第二局 Deal | `deal_start==(2+1)%n` | table |
| T37 | Arrange 中设草稿后 `BuildSnapshot(seat)` | 含 phase、hand、draft、remain_s、locked 标志 | table |

### 14.4 协议 / 冒烟（B3–B4）

| ID | 场景 | 期望 | 层 |
|----|------|------|----|
| T38 | vitest：slot→msg_id 与关键 message 编解码 | 与 proto 一致 | front |
| T40 | 4 游客匹配 `game_id=6000` 模板 | 收到 GameStart；进入摆牌 | smoke |
| T41 | 全员确认 → Settle | 余额变化；ledger 键 `biji:settle:*` | smoke |
| T42 | 无人确认至超时 | 仍收到 Compare/Settle | smoke |
| T43 | `/biji-lab` 多 `d0` 硬件码同桌 | 互见 Compare | smoke/手工 |

### 14.5 切片出门检查表（勾选）

| 切片 | 必绿 | 仍禁止出现 |
|------|------|------------|
| B0 | T01–T07 | `biji_room_game.cpp` 大段逻辑 |
| B1 | T08–T09, T20–T28 | 网络、Wallet 调用 |
| B2 | T30–T37 | proto 依赖进 table 单测 |
| B3 | T38, T40–T42 | 无幂等的直接改余额 |
| B4 | T43 + 重连手工 | — |

---

## 15. 非目标（本 SPEC 首发不做）

- 旁观  
- 5 人无王、带大小王 / 6 人  
- 朋友场房卡、冲榜  
- 录像产品化  
- 客户端物理动画引擎级需求（逐墩展示间隔仅建议值）  
- `game_id≠6000` 变体  
- 在无单测情况下「先联调页面再补规则」  

---

## 16. 文档与实现顺序（强制 TDD 流水线）

```text
Phase 0  文档
  ├─ 本 SPEC（已定稿）
  ├─ （可选并行）比鸡游戏规则.md —— 数值引用 §4/§8，不替代用例表
  └─ AGENTS / 主 SPEC / game_ids.hpp / errors.hpp 登记 6000（可与 B0 同 PR）

Phase 1  B0 Red→Green
  ├─ CMake 增加 biji_hand_test（可先链空 hand.cpp）
  ├─ 写 T01–T07（全红）
  └─ 实现 card/hand 至 T01–T07 绿  → 可合入

Phase 2  B1 Red→Green
  ├─ T08–T09、T20–T28（红）
  └─ AutoArrange + settle 至绿  → 可合入

Phase 3  B2 Red→Green
  ├─ CMake 增加 biji_table_test；T30–T37（红）
  └─ BijiTable 至绿（无 Wallet）→ 可合入

Phase 4  B3
  ├─ proto/game_biji.proto + npm run proto:gen + T38
  ├─ BijiRoomGame + Registry + lobby 种子 template
  └─ biji_smoke.mjs T40–T42

Phase 5  B4
  └─ Snapshot 完善、/biji + /biji-lab、T43
```

**变更纪律**：改比较或结算时，顺序必须是

```text
§4 / §8 正文 → §14 用例行 → *_test.cpp → 生产代码
```

禁止反向（先改代码再补测试冒充 TDD）。

---

## 17. 修订记录

| 版本 | 日期 | 说明 |
|------|------|------|
| V1.1 | 2026-10-10 | 强化强制 TDD：切片门禁、CMake/夹具、反模式、DoD、T28/T38、Phase 流水线 |
| V1.0 | 2026-10-10 | 初稿：对齐需求 V1.1；强制 TDD；锁定编码/比较键/吃喜/抽水/代摆 |

**关联**：需求 [比鸡游戏-需求文档.md](./比鸡游戏-需求文档.md)；规则说明 [比鸡游戏规则.md](./比鸡游戏规则.md)（可随后补，数值以本 SPEC §4/§8 为准）。
