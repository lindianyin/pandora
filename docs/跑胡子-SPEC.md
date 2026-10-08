# 跑胡子 — 技术规格说明书（SPEC）

| 项目 | 内容 |
|------|------|
| 文档版本 | V1.0 |
| 创建日期 | 2026-10-08 |
| 文档状态 | 初稿（可开发；代码尚未落地） |
| 依据文档 | [跑胡子规则.md](./跑胡子规则.md) V1.0 |
| 关联主 SPEC | [棋牌游戏服务端-SPEC.md](./棋牌游戏服务端-SPEC.md)（帧格式、鉴权、钱包、房间框架复用） |
| 适用范围 | `pandora-server` 玩法模块、`proto/game_phz.proto`、`game-web` 跑胡子桌面（后续里程碑） |

> **规则冲突**：玩法语义以《跑胡子规则》为准；本 SPEC 将其落实为编码、状态机、协议、结算与模块接口。主仓硬约束（单体、IOCP、钱包幂等、MySQL 权威）不变。

---

## 1. 规格总览

### 1.1 定位

| 项 | 值 |
|----|----|
| `game_id` | **3**（`1`=斗地主，`2`=杭州麻将） |
| 模块名 | `PhzHunanClassic`（跑胡子 · 湖南经典默认档） |
| 人数 | **3**（固定） |
| 代码落点 | `server/src/game/phz/`（规划） |
| 协议 | `proto/game_phz.proto`（规划） |
| msg_id 段 | **7000 – 7999** |

首发实现锁定规则文档默认档：三人、80 张、`min_hu_xi=15`、`dian_pao=false`、偎 / 跑 / 提强制、囤数 `floor((xi-15)/3)+1`、自摸 `zimo_mode=tun_plus_1`。

### 1.2 与主框架的关系

```text
Match / RoomManager
        │  game_id=3
        ▼
   GameRuntime ──注册──► PhzTable（每桌一实例）
        │
        ├─ SessionHub 推送 7xxx
        ├─ WalletService.Adjust（终局结算；幂等键 phz:settle:{round}:{uid}）
        └─ Admin.RecordRound / Activity.OnGameSettled / Social 战绩
```

- 入座、准备、断线托管、倒计时框架复用现有 Room / GameRuntime。
- **胡息、门子、偎跑提、名堂与囤数仅服务端权威**；客户端只做展示与操作请求。
- 对局轨迹复用主 SPEC §15.1：`game=phz`，客户端经 `C2S_ClientTrace`（9001）上报。

### 1.3 交付切片（建议）

| 切片 | 范围 | 验收 |
|------|------|------|
| P0 | 牌编码、洗牌发牌、摸牌示众、出牌、吃碰、流局 | 三开客户端打完无胡一局 |
| P1 | 强制偎 / 跑 / 提、门子拆解、15 息起胡、摸牌胡 | 自摸结算账变正确 |
| P2 | 名堂（红/点/乌/十八小/对对）、囤数、庄闲可选倍 | 规则用例表全绿 |
| P3 | 三提五坎、重连快照、托管、三联调试页 | 冒烟脚本 + 断线重连 |

---

## 2. 牌编码

### 2.1 TileId（协议用 int32）

| 范围 | 含义 | 编码 |
|------|------|------|
| 0–9 | 小字一～十 | `n-1`（一=0 … 十=9） |
| 10–19 | 大字壹～拾 | `10 + (n-1)`（壹=10 … 拾=19） |
| -1 / 255 | 非法 / 占位 | — |

牌墩共 **80** 张：每种面值 4 张。内部用长度 20 的计数数组 `count[20]`，或 `vector<TileId>` 多重集合。

### 2.2 辅助谓词

```text
IsSmall(t) := 0 <= t <= 9
IsBig(t)   := 10 <= t <= 19
Rank(t)    := t % 10                    // 1..10 对应 0..9
IsRed(t)   := Rank(t) ∈ {1, 6, 9}       // 二/七/十 与 贰/柒/拾
SameName(a,b) := Rank(a)==Rank(b) && IsSmall(a)==IsSmall(b)
```

红字面值集合（实现常量）：

```text
kRedTiles = {1, 6, 9, 11, 16, 19}   // 二七十二贰柒拾
```

### 2.3 显示标签（客户端）

| TileId | 标签 |
|--------|------|
| 0..9 | 一…十 |
| 10..19 | 壹…拾 |

---

## 3. 房间模板配置

挂在 `room_template` 扩展 JSON，开局随 `S2C_PhzGameStart` 下发只读快照。

| 键 | 类型 | 默认 | 说明 |
|----|------|------|------|
| `base_score` | int | 模板底分 | 与主 SPEC 场次底分一致 |
| `players` | int | 3 | 固定 3；非 3 拒开局 |
| `min_hu_xi` | int | 15 | 起胡胡息 |
| `dian_pao` | bool | false | 是否允许胡他人打出牌 |
| `force_wei` | bool | true | 偎强制 |
| `force_pao` | bool | true | 跑强制 |
| `force_ti` | bool | true | 提强制 |
| `first_pao_ti_discard` | bool | true | 本局首次跑/提后必须出牌 |
| `tun_formula` | string | `xi15` | `xi15` ⇒ `floor((xi-15)/3)+1` |
| `zimo_mode` | string | `tun_plus_1` | `tun_plus_1` \| `fan_x2` \| `both` |
| `ming_tang_combine` | string | `max_plus_zimo` | 主名堂取最高番；自摸按 `zimo_mode` |
| `fan_dian_hu` | int | 3 | 点胡番 |
| `fan_xiao_hong` | int | 2 | 红字 10..12 |
| `fan_da_hong` | int | 4 | 红字 ≥13 |
| `fan_wu_hu` | int | 5 | 红字 0 |
| `fan_shi_ba_xiao` | int | 6 | 小字 ≥18 |
| `fan_dui_dui` | int | 5 | 对对胡 |
| `hong_hu_progressive` | bool | false | 大红每多 1 红 +1 番 |
| `san_ti_wu_kan` | bool | true | 起手三提或五坎可胡 |
| `cha_jiao` | bool | false | 流局查叫 |
| `banker_double` | bool | false | 庄家输赢 ×2 |
| `lou_chi` | bool | true | 过吃本圈不可再吃同张 |
| `action_timeout_s` | int | 15 | 出牌 / 响应倒计时 |
| `rake_bp` | int | 场次抽水 | 结算后对赢家抽水 |

名堂默认番与规则文档 §8 对齐；改番只改模板，不改协议字段名。

---

## 4. 桌内状态机

```text
WaitReady
    │ 满3人且全员 Ready
    ▼
Deal ──── 洗牌、定庄、发牌（庄21 / 闲20 / 墩19）
    │
    ▼
Play
    │ 摸牌判定（胡/提/偎/跑）→ 示众或入手流程
    │ 出牌 / ClaimWindow（吃碰跑胡）
    ├─ 有人胡 ──────────────────────────► Settle
    └─ 牌墩空且无人胡 ─────────────────► LiuJu ─► WaitReady
Settle ─ 算囤×番、账变、推送 ─► WaitReady（胡家为庄；流局庄连任）
```

### 4.1 Play 子状态

| 子状态 | 说明 |
|--------|------|
| `DrawJudge` | 当前玩家从墩摸一张：先判胡 → 提 → 偎 →（若为他人可跑的示众源则进入示众逻辑）→ 否则示众 |
| `Discard` | 等待当前玩家出牌（吃碰偎后；或首次跑/提后） |
| `ClaimWindow` | 打出或示众后开启吃 / 碰 / 跑 / 胡响应窗 |
| `ForceReveal` | 摸进牌不能组成强制动作且不胡：示众给全桌，进入 ClaimWindow（出牌者座位记为摸牌者） |

### 4.2 操作优先级

```text
Ti(提, 仅摸牌者自动) > Hu > Pao(跑) > Peng > Chi
```

- 多人胡同一**示众牌**：自摸牌者优先；否则自示众者 **下家** 起逆时针，先者独胡。
- 吃仅下家对上家打出 / 示众有效。
- 碰优先于吃。
- `force_*=true` 时偎 / 跑 / 提由服务端直接执行，不经客户端确认。

### 4.3 摸牌不入手（硬约束）

```text
OnDraw(seat, tile):
  if CanHu(seat, tile, from=DRAW): FinishHu(...)
  else if CanTi(seat, tile): DoTi(...)          // 强制
  else if CanWei(seat, tile): DoWei(...)        // 强制
  else:
    // tile 不进入 hand[]；广播示众
    last_reveal = {seat, tile, kind=DRAW_REVEAL}
    OpenClaimWindow(last_reveal)
```

庄家起手第 21 张按「已在手」处理：先扫起手提 / 三提五坎 / 天胡，再进入庄家 `Discard`。

---

## 5. 桌面数据模型（内存）

```text
PhzTable
  room_id, round_id, template_id, cfg
  banker_seat: 0..2
  wall: deque<TileId>             // 剩余牌墩
  seats[3]:
    uid
    hand: count[20]               // 暗手；示众/门前牌不在此
    melds: []Meld                 // 碰偎跑提吃等门前
    kan_in_hand: count of起手坎   // 或由 hand 推导
    pao_ti_count: int             // 本局已跑+提次数（用于免出）
    lou_chi_tiles: set            // 本圈过吃
    trusteeship: bool
  phase, sub, turn_seat
  last_discard: {seat, tile} | null     // 主动打出
  last_reveal: {seat, tile, kind} | null // 摸牌示众
  claim_deadline
```

`Meld`：

| type | 字段 | 他人可见 |
|------|------|----------|
| `CHI` | `tiles[3]`；`from_seat` | 全明 |
| `JIAO` | `tiles[3]`；`from_seat` | 全明 |
| `PENG` | `tile`；`from_seat` | 三明 |
| `WEI` | `tile` | 默认三暗（或己明他暗，配置） |
| `CHOU_WEI` | `tile` | 同偎 |
| `KAN` | `tile` | 暗（在手，可不进 melds，胡时计入） |
| `PAO` | `tile`；`from_seat` 可 -1 | 四明 |
| `TI` | `tile` | 三暗一明 |

---

## 6. 核心算法规格

### 6.1 门子与胡息

| 门子 | 小字息 | 大字息 |
|------|--------|--------|
| 碰 | 1 | 3 |
| 坎 / 偎 / 臭偎 | 3 | 6 |
| 跑 | 6 | 9 |
| 提 | 9 | 12 |
| 一二三 | 3 | — |
| 壹贰叁 | — | 6 |
| 二七十 | 3 | — |
| 贰柒拾 | — | 6 |
| 普通一句话 / 绞 | 0 | 0 |

```text
HuXi(meld):
  if CHI of {0,1,2}: return 3          // 一二三
  if CHI of {10,11,12}: return 6       // 壹贰叁
  if CHI of {1,6,9}: return 3          // 二七十
  if CHI of {11,16,19}: return 6       // 贰柒拾
  if CHI or JIAO: return 0
  size = 3 or 4
  big = IsBig(tile)
  switch type:
    PENG: return big ? 3 : 1
    KAN/WEI/CHOU_WEI: return big ? 6 : 3
    PAO: return big ? 9 : 6
    TI: return big ? 12 : 9
```

### 6.2 胡牌判定

```text
CheckHu(seat, win_tile, from):
  if from == DISCARD && !cfg.dian_pao: return false
  if from == TI_MELD || from == WEI_MELD_OTHER: return false
  hand' = hand ⊕ win_tile（示众/摸进按规则并入拆解，不永久入手）
  if !TrySplitSevenMenzi(hand', melds, has_pao_or_ti): return false
  xi = SumHuXi(melds') + SumHuXi(hand_parts)
  if xi < cfg.min_hu_xi && !IsSanTiWuKanException(...): return false
  return true
```

**七门子拆解** `TrySplitSevenMenzi`：

1. 桌上每个 meld 计 1 门；
2. 手牌 DFS：优先拆提/跑延伸后的将（若 `has_pao_or_ti` 允许 1 对子作将）；
3. 其余拆：刻（坎）、同体系顺、一二三/二七十、绞；
4. 门子总数 == 7 则成功。

输出：

```text
HuResult {
  ok, xi, menzi_count
  red_count, small_count
  is_draw_win          // 自己摸墩
  flags: DIAN_HU | XIAO_HONG | DA_HONG | WU_HU | SHI_BA_XIAO | DUI_DUI | SAN_TI_WU_KAN | ...
}
```

### 6.3 名堂番

```text
ComputeFan(hu, cfg):
  f = 1
  candidates = []
  if red_count == 1: candidates += cfg.fan_dian_hu
  if red_count == 0: candidates += cfg.fan_wu_hu
  if 10 <= red_count <= 12: candidates += cfg.fan_xiao_hong
  if red_count >= 13:
       fan = cfg.fan_da_hong
       if cfg.hong_hu_progressive: fan += (red_count - 13)
       candidates += fan
  if small_count >= 18: candidates += cfg.fan_shi_ba_xiao
  if dui_dui: candidates += cfg.fan_dui_dui
  if ming_tang_combine == max_plus_zimo:
       f = max(candidates) if candidates else 1
  else if multiply:
       f = product(candidates) or 1
  // 自摸番见 zimo_mode，不在此重复乘（tun_plus_1 时）
  return f
```

### 6.4 囤数与结算

```text
ComputeTun(xi, is_draw, is_tian, cfg):
  if is_tian: tun = floor((xi * 2 - 12) / 3)   // 可再配置
  else:       tun = floor((xi - 15) / 3) + 1
  if cfg.zimo_mode in {tun_plus_1, both} && is_draw: tun += 1
  return max(tun, 1)

BuildSettle(winner, hu, cfg):
  x = ComputeTun(...)
  f = ComputeFan(...)
  if cfg.zimo_mode in {fan_x2, both} && hu.is_draw_win: f *= 2
  d = cfg.base_score
  stake = x * f * d
  for each loser != winner:
    pay = stake
    if cfg.banker_double && (winner==banker || loser==banker): pay *= 2
    delta[loser] -= pay
    delta[winner] += pay
  apply rake on positive winner delta
  return deltas
```

幂等键：`phz:settle:{round_id}:{uid}`，`biz_type=game_settle`。

### 6.5 强制偎 / 跑 / 提

```text
CanWei(seat, tile): hand[tile] == 2 && draw_by_self
CanTi(seat, tile):
  hand[tile] == 3 && draw_by_self
  || hand[tile] == 4 at deal
CanPao(seat, tile, source):
  has meld KAN/PENG/WEI of tile
  || (has PENG and tile arrives via discard/reveal)
```

执行后更新 `pao_ti_count`；若 `first_pao_ti_discard && pao_ti_count==1` 则进入 `Discard`，否则轮到下家摸牌。

---

## 7. 协议（msg_id 7000–7999）

帧格式同主 SPEC：`uint32 LE len | uint32 LE msg_id | protobuf`。

| msg_id | 方向 | message | 说明 |
|--------|------|---------|------|
| 7001 | S→C | `S2C_PhzGameStart` | 开局：座位、庄、己方手牌、墩余量、配置快照 |
| 7002 | S→C | `S2C_PhzTurn` | 阶段、当前座位、倒计时、墩余量；出牌座位附带权威 `self_hand`；`claim` 仅推给有牌权座位 + 出牌/示众源座位；`can_hu` 按收件人计算 |
| 7003 | S→C | `S2C_PhzDraw` | 摸牌：摸牌者见 `tile`；他人 `tile=-1` 或仅知座位（示众前） |
| 7004 | S→C | `S2C_PhzReveal` | 摸牌示众：全桌可见 `seat,tile` |
| 7005 | C→S | `C2S_PhzDiscard` | 出牌 |
| 7006 | S→C | `S2C_PhzDiscardBroadcast` | 出牌广播 |
| 7007 | C→S | `C2S_PhzAction` | 吃/碰/过/胡（跑偎提默认服务器强制，仍可回执） |
| 7008 | S→C | `S2C_PhzActionBroadcast` | 门前变化：吃碰偎跑提 |
| 7009 | S→C | `S2C_PhzSettle` | 终局结算 |
| 7010 | S→C | `S2C_PhzLiuJu` | 流局 |
| 7011 | S→C | （可选）`S2C_PhzReconnect` | 首发可复用 7001/7008/7006/7004/7002 快照重放 |
| 7012 | S→C | `S2C_PhzHint` | 可选：可吃/碰/胡掩码与吃牌候选 |

### 7.1 关键消息字段（逻辑级）

**`S2C_PhzGameStart`**

```text
round_id, room_id, template_id
banker_seat
self_hand[]                 // 仅自己；庄含 21
self_seat
wall_remain                 // 19 起
base_score
cfg_snapshot                // §3 只读子集
```

**`S2C_PhzTurn`**

```text
seat_id, sub, timeout_s, wall_remain
self_hand[]                 // 仅当前出牌座位
can_hu                      // 当前若允许申报自摸/胡则 true（默认摸判由服务器自动胡也可关掉手动）
```

`sub`：`discard` \| `claim` \| `draw_judge`（客户端可忽略后者）。

**`C2S_PhzAction`**

```text
action: PASS=0 | CHI=1 | PENG=2 | HU=4
chi_hand_tiles[]            // 吃时两张手牌（第三张为 last_discard/reveal）
```

跑 / 偎 / 提不由客户端主动发起（`force_*=true`）；若将来关闭强制，可扩展 `action`：`WEI=5 | PAO=6 | TI=7`。

**`S2C_PhzActionBroadcast`**

```text
seat_id, action, tile
tiles[]                     // 门子完整牌面
from_seat                   // optional
meld_kind                   // 1吃 2碰 3偎 4臭偎 5跑 6提 7绞
```

**`S2C_PhzSettle`**

```text
round_id, winner_seat, hu_tile, is_draw_win
hu_xi, tun, fan
ming_tang_mask              // bitset：点胡/红胡/乌胡/...
deltas[]: { uid, seat_id, delta_gold }
base_score
```

### 7.2 断线、托管与重连

对齐杭州麻将策略，适配三人与示众：

| 场景 | 行为 |
|------|------|
| 出牌阶段断线 | 标记托管，等到原倒计时结束再代打（优先胡，否则打最小牌号） |
| ClaimWindow 断线 | 立刻代过 |
| 强制偎/跑/提进行中 | 服务器继续执行，不因断线中断 |
| 重连 | 不重新拨满截止点；下发剩余秒 |

重连快照顺序（无独立 7011 时）：

1. `S2C_PhzGameStart`（同 `round_id`、当前手牌与墩余量）
2. 各门前 `S2C_PhzActionBroadcast`
3. 若有未吃走的 `last_discard` / `last_reveal`，重发 Discard 或 Reveal
4. `S2C_PhzTurn`（`timeout_s`=剩余秒）

> 字段级定义落入 `proto/game_phz.proto` 后为准；本 SPEC 锁定 **msg_id 与消息名**。

---

## 8. 模块接口（C++）

```text
namespace pandora::phz {

struct PhzConfig { /* §3 */ };

class PhzTable {
 public:
  void Start(int banker_seat);
  bool OnDiscard(int seat, TileId tile);
  bool OnAction(int seat, Action act, const ChiOption* chi);
  void OnTimeout(int seat);
  // Draw / Wei / Pao / Ti driven internally after discard resolve
};

HuResult CheckHu(const SeatState&, TileId win, From from, const PhzConfig&);
int ComputeTun(int xi, bool draw, bool tian, const PhzConfig&);
int ComputeFan(const HuResult&, const PhzConfig&);
SettlePlan BuildSettle(const PhzTable&, const HuResult&);

}  // namespace
```

注册：

```text
GameRegistry.Register(game_id=3, factory → PhzTable)
```

托管策略（默认）：

| 场景 | 行为 |
|------|------|
| 出牌超时 | 能胡则胡；否则打最小 TileId |
| Claim 超时 | `PASS` |
| 摸牌强制动作 | 服务器直接偎/跑/提，与是否托管无关 |

---

## 9. 存储与战绩

复用 `game_round` / `game_round_player`：

| 字段 | 跑胡子填写 |
|------|------------|
| `game_id` | 3 |
| `template_id` | 场次 |
| `players_json` | 座位、uid、delta、胡息、囤、番、名堂掩码 |
| `base_score` | 底分 |
| `multiplier` | 建议存 **fan**；囤数写入 players_json |

Redis：无新增必选键。

---

## 10. 错误码（玩法段建议）

| code | 含义 |
|------|------|
| 7201 | 非己回合 |
| 7202 | 非法出牌 |
| 7203 | 非法吃碰 |
| 7204 | 不可胡（息不足/门子不足/来源禁止） |
| 7205 | 过吃限制 |
| 7206 | 动作超时已托管 |

走统一 `S2C_Error`；非法胡回 `1003` 时可带明确文案：「息数不足」「不能胡打出牌」等。

---

## 11. 客户端（game-web）要点

| 页/组件 | 要求 |
|---------|------|
| 字牌桌 | 3 人布局；门前区区分明暗；示众区；墩余量；红字着色 |
| 操作条 | 出牌、吃/碰/胡/过；倒计时用服务端 `timeout_s` |
| 局号 | 显示 `round_id`，便于日志过滤 |
| 结算层 | 展示胡息、囤、番、名堂、金币变化 |
| 联调 | `/phz-lab` 三联（规划，对齐 `/ddz-lab`） |

重连：同局号保留桌面状态，示众/弃牌回放避免重复追加。

---

## 12. 验收用例（摘要）

| ID | 场景 | 期望 |
|----|------|------|
| T01 | 庄发 21 / 闲 20 / 墩 19 | 张数正确 |
| T02 | 摸牌不能偎跑提胡 | 示众，不入手 |
| T03 | 手两张 + 自摸第三张 | 强制偎，暗放，须出牌 |
| T04 | 已碰再来第四张示众 | 强制跑，四明 |
| T05 | 坎 + 自摸第四张 | 强制提，三暗一明 |
| T06 | 15 息 7 门子自摸 | 可胡；两家各付 `tun*fan*base` |
| T07 | 14 息 | 拒胡 |
| T08 | 他人打出牌 | 默认不可胡 |
| T09 | 红字 0 / 1 / 11 / 13 | 乌/点/小红/大红番正确 |
| T10 | 首次跑后 | 必须出牌；第二次跑免出 |
| T11 | 流局 | 无账变；庄连任 |
| T12 | 断线重连 | 手牌一致；倒计时剩余秒 |
| T13 | 三提五坎（开） | 起手可胡 |

完整用例落入 `server/tests/phz_*`（实现阶段）。

---

## 13. 非目标（本 SPEC 首发不做）

- 四川 / 贵州地方字牌完整规则
- 四人跑胡子
- 花牌、癞子
- 放炮胡与一炮多响（`dian_pao` 默认 false；开启为后续）
- 独立重连消息 7011（可先复用快照重放）

---

## 14. 文档与实现顺序

```text
改 docs/跑胡子规则.md
  → 改本 SPEC
  → 主 SPEC 登记 game_id=3 / msg 7000–7999（若尚未登记）
  → proto/game_phz.proto
  → server/src/game/phz/
  → game-web 桌面与 /phz-lab
  → server/scripts/phz_smoke.mjs + tests
```

改默认规则时：规则文档与本 SPEC 必须同提交。
