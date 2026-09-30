# 杭州麻将 — 技术规格说明书（SPEC）

| 项目 | 内容 |
|------|------|
| 文档版本 | V1.0 |
| 创建日期 | 2026-09-29 |
| 文档状态 | 初稿（可开发） |
| 依据文档 | [杭州麻将规则.md](./杭州麻将规则.md) V1.0 |
| 关联主 SPEC | [棋牌游戏服务端-SPEC.md](./棋牌游戏服务端-SPEC.md)（帧格式、鉴权、钱包、房间框架复用） |
| 适用范围 | `pandora-server` 玩法模块、`proto/game_hzmj.proto`、`game-web` 麻将桌面（后续里程碑） |

> **规则冲突**：玩法语义以《杭州麻将规则》为准；本 SPEC 将其落实为编码、状态机、协议、结算与模块接口。主仓硬约束（单体、IOCP、钱包幂等、MySQL 权威）不变。

---

## 1. 规格总览

### 1.1 定位

| 项 | 值 |
|----|----|
| `game_id` | **2**（`1`=斗地主已占用） |
| 模块名 | `HzmjClassic`（杭州麻将 · 默认配置档） |
| 人数 | **4**（固定） |
| 代码落点 | `server/src/game/hzmj/` |
| 协议 | `proto/game_hzmj.proto` |
| msg_id 段 | **6000 – 6999** |

首发实现锁定规则文档默认档：`caishen_mode=fixed_bai`（白板固定财神）。`flip` 翻财神作为 P1 扩展开关，接口预留字段，首发可不实现翻牌逻辑。

### 1.2 与主框架的关系

```text
Match / RoomManager
        │  game_id=2
        ▼
   GameRuntime ──注册──► HzmjTable（每桌一实例）
        │
        ├─ SessionHub 推送 6xxx
        ├─ WalletService.Adjust（结算/可选即时杠分）
        └─ Admin.RecordRound / Activity.OnGameSettled / Social 战绩
```

- 入座、准备、断线托管、倒计时框架复用现有 Room / GameRuntime。
- **所有胡牌判定、吃碰杠合法性、倍数与承包计算仅服务端权威**；客户端只做展示与操作请求。

### 1.3 交付切片（建议）

| 切片 | 范围 | 验收 |
|------|------|------|
| H0 | 牌编码、洗牌发牌、摸打、吃碰杠基础、流局 | 四开客户端打完无胡一局 |
| H1 | 白板财神、听牌/爆头、自摸胡、庄闲倍数结算 | 自摸结算账变正确 |
| H2 | 财飘、杠开/杠串、三牢点炮、三摊承包 | 规则用例表全绿 |
| H3 | 七对系列、漏胡、抢杠、重连快照、托管 | 冒烟脚本 + 断线重连 |

---

## 2. 牌编码

### 2.1 TileId（uint8 / int32 皆可，协议用 int32）

| 范围 | 含义 | 编码 |
|------|------|------|
| 0–8 | 万 1–9 | `0 + (n-1)` |
| 9–17 | 条 1–9 | `9 + (n-1)` |
| 18–26 | 筒 1–9 | `18 + (n-1)` |
| 27–30 | 东南西北 | 27=东 … 30=北 |
| 31–33 | 中发白 | 31=中, 32=发, **33=白** |
| 255 | 非法 / 占位 | — |

牌墙共 **136** 张：每种面值 4 张。内部可用 `vector<TileId>` 表示多重集合；也可用长度 34 的计数数组 `count[34]`。

### 2.2 财神

| `caishen_mode` | 财神面值 | 张数 |
|----------------|----------|------|
| `fixed_bai`（默认） | `33`（白） | 4 |
| `flip`（P1） | 翻出指示牌面值 | 3（白板改作指示牌替身） |

辅助函数：

```text
IsCaishen(tile) := (tile ∈ caishen_set)
CanMeldWithDiscard(discard) := !IsCaishen(discard)   // 打出的财神不可被吃碰杠胡
```

---

## 3. 房间模板配置

挂在 `room_template` 的扩展 JSON（或独立列 + JSON），开局随 `S2C_HzmjGameStart` 下发只读快照。

| 键 | 类型 | 默认 | 说明 |
|----|------|------|------|
| `base_score` | int | 模板底分 | 与主 SPEC 场次底分一致 |
| `caishen_mode` | string | `fixed_bai` | 见 §2.2 |
| `allow_chi` | bool | true | |
| `sanlao_dianpao` | bool | true | 三连庄起庄闲可点炮 |
| `xian_xian_dianpao` | bool | false | |
| `qiang_gang_hu` | bool | true | |
| `lou_hu` | bool | true | |
| `peng_counts_tan` | bool | true | 碰计入三摊 |
| `gang_score_mode` | string | `instant` | `instant` \| `fold_into_hu` |
| `start_as_sanlao` | bool | false | true 则 N 恒为 8 |
| `max_piao` | int | 3 | 连飘封顶 |
| `piao_block_an_gang` | bool | true | 飘期间禁止他家暗杠 |
| `mult_combine` | string | `multiply` | 系列倍数相乘；`enum_max` 为备选 |
| `action_timeout_s` | int | 15 | 出牌/响应倒计时 |
| `rake_bp` | int | 场次抽水 | 结算后对赢家抽水 |

庄闲倍数 N：

```text
if start_as_sanlao: N = 8
else if lian_zhuang <= 1: N = 2      // 平庄/一连
else if lian_zhuang == 2: N = 4
else: N = 8                        // ≥3 封顶
```

`lian_zhuang`：当前庄家连续坐庄局数（含本局将开的这一局）；流局 +1；换庄重置为 1。

---

## 4. 桌内状态机

```text
WaitReady
    │ 满4人且全员 Ready
    ▼
Deal ──────── 洗牌、定庄、发牌（庄14/闲13）、初始化财神
    │
    ├─(flip)─► FlipCaishen ──►
    │                          │
    └──────────────────────────┘
    ▼
Play
    │ 摸打 / 吃碰杠响应 / 飘状态
    ├─ 有人胡 ──────────────────────────► Settle
    └─ 牌墙空且无人胡 ─────────────────► LiuJu ─► WaitReady
Settle ─ 算分、账变、推送 ─► WaitReady（更新庄家/连庄）
```

### 4.1 Play 子状态

| 子状态 | 说明 |
|--------|------|
| `Draw` | 当前玩家摸牌（或杠后补牌） |
| `Discard` | 等待当前玩家出牌（可暗杠/补杠） |
| `ClaimWindow` | 出牌后开启吃碰杠胡响应窗；收齐或超时后结算优先级 |
| `PiaoLock` | 飘家已打出财神，等待飘家摸牌；他家仅允许摸打（受配置约束） |

### 4.2 操作优先级（ClaimWindow）

```text
Hu > Gang(明杠) > Peng > Chi
```

- 多人胡：自出牌者 **下家** 起逆时针，**先者独胡**（一炮一响）。
- 同时有胡与碰：胡优先。
- 吃仅下家对上家出牌有效。

---

## 5. 桌面数据模型（内存）

```text
HzmjTable
  room_id, round_id, template_id, cfg
  banker_seat: 0..3
  lian_zhuang: int
  wall: queue<TileId>          // 剩余牌墙
  seats[4]:
    uid
    hand: multiset / count[34]
    melds: []Meld               // 吃碰杠副露
    flowers: []                 // 本玩法恒空
    ting_mask / baotou: bool
    piao_level: 0..max_piao     // 当前连飘成功层数（胡时取值）
    lou_hu_tiles: set           // 本圈漏胡牌面
    tan_count[4]: int           // 对本家「摊」次数：tan_count[from_seat]
    gang_chain: int             // 连续杠次数（未打断）
  phase, turn_seat, claim_deadline
  caishen_tiles: set<TileId>
  last_discard: {seat, tile} | null
  piao_active: {seat, level} | null
```

`Meld`：

| type | 字段 |
|------|------|
| `CHI` | `tile_mid` 或三张列表；`from_seat` |
| `PENG` | `tile`；`from_seat` |
| `MING_GANG` | `tile`；`from_seat` |
| `AN_GANG` | `tile` |
| `BU_GANG` | `tile`；由碰升级 |

---

## 6. 核心算法规格

### 6.1 胡牌判定

输入：手牌计数（含刚摸入）、副露列表、财神集合、是否七对通道。

**标准胡**：存在拆解「1 将 + 4 面子」，面子为顺或刻；财神可代替任意牌。算法可用：

1. 枚举将牌（含财神参与的将）；
2. 对其余牌做面子 DFS / 查表；
3. 财神作万能填补。

**七对**：手牌 14 张且无副露（或仅允许暗杠折算进七对的产品定义——**默认要求无吃碰明杠副露**）；7 组对子，财神可补对；四张相同计「豪华」层数。

输出结构：

```text
HuResult {
  ok: bool
  kind: PING | QI_DUI | ...
  baotou: bool
  piao_level: int          // 0=无飘, 1=财飘, 2=双, 3=三
  gang_chain: int          // 本次胡若来自杠补，取桌面 gang_chain
  haohua: 0..3             // 七对豪华数
  qing_qi_dui: bool        // 七对且胡牌时手中无财神
  is_zimo: bool
  is_qiang_gang: bool
}
```

### 6.2 爆头判定

在听牌集合计算时：若存在「将 = 财神 + 真牌 T」且其余已成面子，则摸入 **任意牌** 均可胡 → `baotou_ting=true`。  
胡牌时若实际以爆头将型完成 → `baotou=true`。

### 6.3 财飘状态机

```text
baotou_ting && 打出 IsCaishen(tile)
  → piao_active = {seat, level = prev+1 capped max_piao}
  → 他家进入摸打限制

飘家摸牌:
  if 可胡: 允许声明胡（倍数含对应 piao_level）
  else 出牌:
    if IsCaishen: level++（封顶）
    else: piao_active = null（飘失败）
```

### 6.4 倍数 M（默认 `multiply`）

```text
M = 1
M *= f_caishen(baotou, piao_level)   // 平1 / 爆2 / 飘4 / 双8 / 三16；取最高档互斥
M *= f_gang(gang_chain)              // 无杠开1；杠开2；二连4；三连8；四连16
M *= f_qidui(kind, haohua, qing)     // 非七对则1；见规则 §7.3
// 七客：七对且爆头 → 至少 ×4（与表一致；若已乘爆头与七对，按房间表锁死避免重复）
```

**首发锁死表**（`mult_combine=multiply` + 互斥规则）：

| 条件 | M |
|------|---|
| 仅平胡 | 1 |
| 仅爆头 | 2 |
| 财飘 / 双 / 三 | 4 / 8 / 16 |
| 杠开链 k=1..4 | ×(2^k) |
| 七对 / 豪华×n / 清七对… | 按规则文档 §7.3 |
| 杠爆 | 杠开×爆头 |
| 杠飘 / 飘杠 | 杠开×财飘 |

实现须带 **单元测试黄金用例**（见 §12），禁止运行时「口头解释」倍数。

### 6.5 结算金额

```text
stake = base_score * M

function PayZimo(winner, banker, N):
  for each loser ≠ winner:
    factor = (loser == banker || winner == banker) ? N : 1
    // 更精确：
    // 庄赢：每闲付 stake*N
    // 闲赢：庄付 stake*N，另两闲付 stake*1
  写入 delta[seat]

function PayDianpao(winner, shooter, banker, N):  // 仅规则允许时
  // 默认：点炮者支付「若自摸时三家应付总和」
  total = SumWouldPayIfZimo(winner, banker, N)
  delta[shooter] -= total
  delta[winner] += total

if 三摊承包触发:
  contractor 支付 winner 本应收到的全部正分之和
  其余应付方 delta 清零（其应付转由 contractor 承担）
```

抽水：对每个 `delta>0` 的赢家按 `rake_bp` 扣减（向下取整），`biz_type=rake`；幂等键：

```text
hzmj:settle:{round_id}:{uid}
hzmj:gang:{round_id}:{seq}:{uid}     // instant 杠分
```

### 6.6 三摊

```text
OnChiOrPeng(actor, from):
  if cfg.peng_counts_tan || action==CHI:
    seats[actor].tan_count[from]++

Contractor(winner):
  for s in seats:
    if s.tan_count[winner] >= 3: return s   // 吃了胡家满三摊 → 承包胡家
    if winner.tan_count[s] >= 3: return s   // 胡家吃了 s 满三摊 → s 承包
  // 若规则「谁胡谁被承包」仅第一种：默认同时支持规则文档两种方向
```

冲突（两人都满三摊）时：**优先「吃胡家满三摊」的承包者**；写入结算日志。

### 6.7 点炮合法性

```text
CanDianpao(shooter, winner):
  if !cfg.sanlao_dianpao || N < 8: return false   // 未到三牢
  if shooter 与 winner 皆为闲 && !cfg.xian_xian_dianpao: return false
  if lou_hu 且 tile ∈ winner.lou_hu_tiles: return false
  if IsCaishen(discard): return false
  return WinnerCanHu(discard)
```

---

## 7. 协议（msg_id 6000–6999）

帧格式同主 SPEC：`uint32 LE len | uint32 LE msg_id | protobuf`。

| msg_id | 方向 | message | 说明 |
|--------|------|---------|------|
| 6001 | S→C | `S2C_HzmjGameStart` | 开局：座位、庄、连庄、财神、己方手牌、配置快照 |
| 6002 | S→C | `S2C_HzmjTurn` | 阶段、当前座位、倒计时、牌墙余量、飘状态、`can_zimo`；出牌座位附带权威 `self_hand` |
| 6003 | S→C | `S2C_HzmjDraw` | 仅摸牌座位收到摸到的牌；他人只收「某座摸牌」 |
| 6004 | C→S | `C2S_HzmjDiscard` | 出牌；可附带声明暗杠/补杠意图用独立消息 |
| 6005 | S→C | `S2C_HzmjDiscardBroadcast` | 出牌广播 |
| 6006 | C→S | `C2S_HzmjAction` | 吃/碰/杠/胡/过（claim 窗内） |
| 6007 | S→C | `S2C_HzmjActionBroadcast` | 副露/胡结果广播 |
| 6008 | S→C | `S2C_HzmjGangScore` | 即时杠分（可选） |
| 6009 | S→C | `S2C_HzmjSettle` | 终局结算 |
| 6010 | S→C | `S2C_HzmjLiuJu` | 流局 |
| 6011 | S→C | （未单独实现） | 重连复用 6001 / 6007 / 6005 / 6002，见 §7.2 |
| 6012 | C→S | `C2S_HzmjGang` | 暗杠/补杠（轮到己方 Discard 阶段） |
| 6013 | S→C | `S2C_HzmjHint` | 可选：可胡/可碰等提示掩码 |

### 7.1 关键消息字段（逻辑级）

**`S2C_HzmjGameStart`**

```text
round_id, room_id, template_id
banker_seat, lian_zhuang, N
caishen_tiles[]
self_hand[]                 // 仅自己
seats[]: { uid, seat, gold }
cfg_snapshot                // §3 只读
wall_remain
```

**`S2C_HzmjTurn`**

```text
seat_id, sub, timeout_s, wall_remain, piao_seat
self_hand[]                 // 仅发给当前出牌座位，权威纠偏
can_zimo                    // 本回合可自摸：摸牌后或庄家起手为 true；吃/碰后的出牌回合为 false
```

`timeout_s` 在新回合是配置的完整秒数。重连下发的是距原截止点的剩余秒数（毫秒向上取整），不把截止点重新拨满。

客户端只在 `sub` 为 `discard` 或 `piao`、轮到自己、`can_zimo` 为真且牌型可胡时显示「自摸」。吃碰后若仍发送胡，服务端回 `S2C_Error` `1003`，文案「吃碰之后须出牌，不能自摸」。牌型确实未成胡时文案为「不能自摸胡：牌型未成胡」。

**`C2S_HzmjAction`**

```text
action: PASS | CHI | PENG | GANG | HU
chi_tiles[]                 // 吃时两张手牌 + 目标构成
```

**`S2C_HzmjSettle`**

```text
winner_seat, hu_tile, is_zimo, shooter_seat
M, N, baotou, piao_level, gang_chain, qidui_flags
contractor_seat             // -1 无承包
deltas[]: { seat, uid, delta_gold, balance_after }
rake[]
hand_reveal[]               // 各手牌+副露（可配置仅胜方）
```

### 7.2 断线、托管与重连

出牌 / 飘阶段断线只标记托管，**不跳过剩余倒计时**；到点才代打（自摸优先，否则打出刚摸的牌，再否则打最小牌号）。鸣牌窗内断线立刻代打过，避免整桌停在窗口上。重连清除该座位的托管标记。

重连不新开截止点，按顺序只发给该连接：

1. `S2C_HzmjGameStart`（同一 `round_id`、己方手牌、当前牌墙）
2. 各家已有副露的 `S2C_HzmjActionBroadcast`
3. 若仍有未吃走的最新弃牌，`S2C_HzmjDiscardBroadcast`
4. `S2C_HzmjTurn`，`timeout_s` 为剩余秒，`can_zimo` 对应当前回合

同一页面重连（内存里局号未丢）保留河牌与余牌张数，弃牌回放若河牌已以该张结尾则不再追加。桌面显示局号，便于在日志里按 `round=` 过滤。

> 字段级定义以 `proto/game_hzmj.proto` 为准；本 SPEC 锁定 **msg_id 与消息名**。对局轨迹见主 SPEC §15.1。

---

## 8. 模块接口（C++）

```text
namespace pandora::hzmj {

class HzmjTable {
 public:
  void OnReadyAll();
  void OnDiscard(int seat, TileId tile);
  void OnAction(int seat, Action act);
  void OnGang(int seat, GangKind kind, TileId tile);
  void OnTimeout(int seat);
  void OnReconnect(int64_t uid);
  // ...
};

// 纯函数便于单测
HuResult CheckHu(const Hand&, const Melds&, const CaishenSet&, TileId win_tile, bool zimo);
int ComputeM(const HuResult&, const HzmjConfig&);
SettlePlan BuildSettle(const HzmjTable&, const HuResult&);

}  // namespace
```

注册：

```text
GameRegistry.Register(game_id=2, factory → HzmjTable)
```

托管策略（默认）：

| 场景 | 行为 |
|------|------|
| 出牌阶段断线 | 标记托管，等到原倒计时结束再代打 |
| 出牌超时 | 能自摸则自摸；否则打出刚摸进的牌；再否则打最小牌号 |
| 鸣牌窗断线或超时 | `PASS` |
| 吃 / 碰之后 | `can_zimo` 为假，不自动自摸，须出牌 |

---

## 9. 存储与战绩

复用 `game_round` / `game_round_player`：

| 字段 | 杭州麻将填写 |
|------|----------------|
| `game_id` | 2 |
| `template_id` | 场次 |
| `players_json` | 座位、uid、delta、是否庄、M/N、是否承包等摘要 |
| `base_score` | 底分 |
| `multiplier` | 建议存 **M**（胡牌倍数）；庄闲 N 写入 players_json |

Redis：无新增必选键；对局状态仅内存 + 落库。

---

## 10. 错误码（玩法段建议）

| code | 含义 |
|------|------|
| 6201 | 非己回合 |
| 6202 | 非法出牌（手牌无此张） |
| 6203 | 非法吃碰杠 |
| 6204 | 不可胡（未听/点炮规则禁止/漏胡） |
| 6205 | 飘期间禁止操作 |
| 6206 | 财神不可被鸣牌 |
| 6207 | 动作超时已托管 |

当前实现走统一 `S2C_Error`。自摸被拒为 `1003`（见 §7.1），不是上表的 620x。

---

## 11. 客户端（game-web）要点

| 页/组件 | 要求 |
|---------|------|
| 麻将桌 | 4 人布局、手牌、副露、牌墙余量、财神角标、飘状态横幅 |
| 操作条 | 出牌、吃/碰/杠/胡/过；倒计时沿用服务端下发的 `timeout_s`（重连为剩余秒） |
| 自摸按钮 | 仅 `can_zimo` 且牌型可胡时显示 |
| 局号 | 牌桌与四联调试显示 `round_id` |
| 结算层 | 展示 M/N、爆头/飘/杠串、承包箭头、金币变化 |
| 重连 | 按 §7.2 重放 6001 / 6007 / 6005 / 6002，不依赖 6011 |

联调：四开窗口同一场次，打完含自摸与一流局。

### 11.1 客户端 TDD（Vitest）

权威规则仍在服务端；客户端单测覆盖契约与 UI 预判，不重复结算黄金用例。

| 范围 | 路径 | 说明 |
|------|------|------|
| 帧 / 编解码 / `tileLabel` | `game-web/src/net/frame.test.ts` | 粘包、proto3 seat0、`Turn.self_hand`、`can_zimo`、signed tile、`C2S_ClientTrace` |
| 吃碰杠预判 | `game-web/src/net/hzmjMeld.test.ts` | 对齐服务端 T10 / chi 边界 |
| 手牌同步 / 点炮胡按钮 | `game-web/src/net/hzmjHand.test.ts` | 重连幂等扣牌、N≥8 才显示胡 |

```powershell
cd game-web
npm test
```

新逻辑：先写失败用例 → 实现 → 保持 `npm test` 全绿；Vue 组件联调仍用手测 / `hzmj_smoke`。

---

## 12. 验收用例（摘要）

| ID | 场景 | 期望 |
|----|------|------|
| T01 | 平庄自摸平胡 | 三家按 N=2 付分；M=1 |
| T02 | 爆头自摸 | M=2 |
| T03 | 财飘成功胡 | M=4；飘期间他家吃碰被拒 |
| T04 | 双财飘 | M=8 |
| T05 | 杠开 | M=2 |
| T06 | 杠飘 | M=8 |
| T07 | 三牢庄闲点炮 | 允许；点炮者付合计 |
| T08 | 平庄点炮 | 拒绝 |
| T09 | 吃同一家 3 次后该家胡 | 承包者付三家份 |
| T10 | 打出白板（财神） | 不可被碰 |
| T11 | 流局 | 无账变；连庄+1 |
| T12 | 断线重连 | 手牌一致、可续打；倒计时为剩余秒，不重新扣满 |
| T13 | 吃碰后手牌已是和牌形 | 不可自摸；`can_zimo=false`，须先出牌 |

完整用例表实现阶段放入 `server/tests/hzmj/`。

---

## 13. 非目标（本 SPEC 首发不做）

- 花牌、定缺、买码、下注飘分（推倒胡那类）
- 一炮多响
- 翻财神完整流程（仅预留配置）
- 与斗地主共用 msg_id 段

---

## 14. 修订记录

| 版本 | 日期 | 说明 |
|------|------|------|
| V1.0 | 2026-09-29 | 依据《杭州麻将规则》V1.0 产出：game_id=2、牌编码、配置、状态机、倍数/结算/承包算法、msg_id 6000–6999、模块与验收 |

**关联**：规则语义 → `docs/杭州麻将规则.md`；平台级约束 → `docs/棋牌游戏服务端-SPEC.md`。
