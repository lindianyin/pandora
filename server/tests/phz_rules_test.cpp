#include "game/phz/config.hpp"
#include "game/phz/hu.hpp"
#include "game/phz/meld.hpp"
#include "game/phz/settle.hpp"
#include "game/phz/tiles.hpp"

#include <iostream>

using namespace pandora::phz;

static int fails = 0;
void Expect(bool cond, const char* msg) {
  if (!cond) {
    std::cerr << "FAIL: " << msg << "\n";
    ++fails;
  } else {
    std::cout << "OK: " << msg << "\n";
  }
}

void TestTiles() {
  Expect(kTileKinds == 20, "kinds 20");
  Expect(IsSmall(0) && IsSmall(9) && !IsSmall(10), "small");
  Expect(IsBig(10) && IsBig(19), "big");
  Expect(IsRed(1) && IsRed(6) && IsRed(9) && IsRed(11) && IsRed(16) && IsRed(19), "red");
  Expect(!IsRed(0) && !IsRed(10), "black");
  auto wall = BuildWall();
  Expect(static_cast<int>(wall.size()) == 80, "wall 80");
  HandCount cnt = ZeroHand();
  for (TileId t : wall) AddTile(cnt, t, 1);
  bool all4 = true;
  for (TileId t = 0; t < kTileKinds; ++t)
    if (cnt[static_cast<size_t>(t)] != 4) all4 = false;
  Expect(all4, "each kind 4");
}

void TestHuXi() {
  Meld peng_s{MeldKind::kPeng, 0, {0, 0, 0}, -1};
  Meld peng_b{MeldKind::kPeng, 10, {10, 10, 10}, -1};
  Expect(HuXiOfMeld(peng_s) == 1, "peng small 1");
  Expect(HuXiOfMeld(peng_b) == 3, "peng big 3");
  Meld wei{MeldKind::kWei, 0, {0, 0, 0}, -1};
  Expect(HuXiOfMeld(wei) == 3, "wei small 3");
  Meld ti{MeldKind::kTi, 10, {10, 10, 10, 10}, -1};
  Expect(HuXiOfMeld(ti) == 12, "ti big 12");
  Meld chi123{MeldKind::kChi, 0, {0, 1, 2}, -1};
  Expect(HuXiOfMeld(chi123) == 3, "123 xi 3");
}

void TestTunFan() {
  PhzConfig cfg;
  cfg.base_score = 100;
  Expect(ComputeTun(15, false, false, cfg) == 1, "tun 15 -> 1");
  Expect(ComputeTun(18, false, false, cfg) == 2, "tun 18 -> 2");
  Expect(ComputeTun(15, true, false, cfg) == 2, "zimo tun+1");

  HuResult hu;
  hu.ok = true;
  hu.xi = 15;
  hu.is_draw_win = true;
  hu.red_count = 1;
  hu.fan = ComputeFan(hu, cfg);
  Expect(hu.fan == 3, "T09 dian hu fan 3");
  hu.red_count = 0;
  Expect(ComputeFan(hu, cfg) == 5, "T09 wu hu 5");
  hu.red_count = 11;
  Expect(ComputeFan(hu, cfg) == 2, "T09 xiao hong 2");
  hu.red_count = 13;
  Expect(ComputeFan(hu, cfg) == 4, "T09 da hong 4");

  hu.red_count = 0;
  hu.fan = 1;
  hu.is_draw_win = true;
  auto plan = BuildSettle(0, hu, cfg, 0);
  Expect(plan.tun == 2, "settle tun zimo");
  Expect(plan.deltas[0] == 400 && plan.deltas[1] == -200 && plan.deltas[2] == -200, "T06 pay each stake");
}

void TestHuThreshold() {
  PhzConfig cfg;
  cfg.min_hu_xi = 15;
  cfg.dian_pao = false;

  // 5 big wei (6*5=30) + pair as jiang needs pao/ti — use 5 wei melds + 1 ti + pair in hand? 
  // Simpler: 2 ti big (12*2=24) + 1 wei small (3) + chi 0xi + pair — menzi count
  std::vector<Meld> melds;
  melds.push_back({MeldKind::kTi, 10, {10, 10, 10, 10}, -1});
  melds.push_back({MeldKind::kTi, 11, {11, 11, 11, 11}, -1});
  melds.push_back({MeldKind::kWei, 0, {0, 0, 0}, -1});
  melds.push_back({MeldKind::kPeng, 2, {2, 2, 2}, -1});
  melds.push_back({MeldKind::kChi, 3, {3, 4, 5}, -1});
  // hand: pair 7 + need one more menzi — add kan 8 in hand via win tile making 3?
  // table 5 melds; hand needs 2 menzi. With pao/ti, pair counts. Hand: 7,7 and win 8 with 8,8 already?
  HandCount hand = CountTiles({7, 7, 8, 8});
  // win 8 -> kan 8 + pair 7 = 2 menzi, total 7; xi = 12+12+3+1+0 + HuXiOfKan(8)=3 = 31
  auto ok = CheckHu(hand, melds, 8, HuFrom::kDraw, cfg, true);
  Expect(ok.ok && ok.xi >= 15, "T06 hu ok >=15");

  // 14 xi fail: only small pengs
  std::vector<Meld> low;
  for (int i = 0; i < 5; ++i) {
    TileId t = static_cast<TileId>(i);
    low.push_back({MeldKind::kPeng, t, {t, t, t}, -1});
  }
  HandCount h2 = CountTiles({5, 5, 6, 6});
  // 5 peng *1 =5 + kan6=3 + pair = xi 8
  auto bad = CheckHu(h2, low, 6, HuFrom::kDraw, cfg, true);
  Expect(!bad.ok, "T07 reject low xi");

  // T08 discard not allowed
  auto disc = CheckHu(hand, melds, 8, HuFrom::kDiscard, cfg, true);
  Expect(!disc.ok, "T08 no dian pao");
}

void TestSanTiWuKanT13() {
  PhzConfig cfg;
  cfg.san_ti_wu_kan = true;
  cfg.min_hu_xi = 15;
  std::vector<Meld> melds;
  melds.push_back({MeldKind::kTi, 10, {10, 10, 10, 10}, -1});
  melds.push_back({MeldKind::kTi, 11, {11, 11, 11, 11}, -1});
  melds.push_back({MeldKind::kTi, 12, {12, 12, 12, 12}, -1});
  HandCount hand = CountTiles({0, 0});
  auto hu = CheckHu(hand, melds, 0, HuFrom::kDraw, cfg, true);
  Expect(hu.ok && (hu.ming_tang_mask & kMtSanTiWuKan), "T13 san ti");
}

void TestIdemKey() {
  Expect(IdemSettleKey(42, 7) == "phz:settle:42:7", "idem key");
}

int main() {
  TestTiles();
  TestHuXi();
  TestTunFan();
  TestHuThreshold();
  TestSanTiWuKanT13();
  TestIdemKey();
  if (fails) {
    std::cerr << fails << " failed\n";
    return 1;
  }
  std::cout << "all ok\n";
  return 0;
}
