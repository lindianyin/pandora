#include "game/hzmj/config.hpp"
#include "game/hzmj/hu.hpp"
#include "game/hzmj/meld.hpp"
#include "game/hzmj/settle.hpp"
#include "game/hzmj/tiles.hpp"

#include <iostream>
#include <map>
#include <string>

using namespace pandora::hzmj;

static int fails = 0;

void Expect(bool cond, const char* msg) {
  if (!cond) {
    std::cerr << "FAIL: " << msg << "\n";
    ++fails;
  } else {
    std::cout << "OK: " << msg << "\n";
  }
}

HandCount H(std::initializer_list<TileId> tiles) { return CountTiles(std::vector<TileId>(tiles)); }

void TestTiles() {
  Expect(kBai == 33, "bai=33");
  Expect(IsWan(0) && IsWan(8) && !IsWan(9), "wan range");
  Expect(IsTiao(9) && IsTiao(17), "tiao range");
  Expect(IsTong(18) && IsTong(26), "tong range");
  Expect(IsFeng(27) && IsJian(33), "feng/jian");
  Expect(IsCaishenFixedBai(33) && !IsCaishenFixedBai(31), "caishen");
  auto wall = BuildWall();
  Expect(static_cast<int>(wall.size()) == 136, "wall 136");
  std::map<TileId, int> cnt;
  for (TileId t : wall) ++cnt[t];
  bool all4 = true;
  for (TileId t = 0; t < 34; ++t)
    if (cnt[t] != 4) all4 = false;
  Expect(all4, "each kind 4");
}

void TestMeld() {
  Expect(!CanMeldDiscard(kBai), "T10 cannot meld discard bai");
  Expect(!CanPeng(H({kBai, kBai}), kBai), "T10 cannot peng bai");
  Expect(CanPeng(H({0, 0, 1}), 0), "peng 1wan");
  Expect(CanMingGang(H({0, 0, 0}), 0), "ming gang");
  Expect(!CanMingGang(H({0, 0}), 0), "no ming gang");
  Expect(CanAnGang(H({5, 5, 5, 5}), 5), "an gang");
  Expect(!CanAnGang(H({kBai, kBai, kBai, kBai}), kBai), "no an gang bai");

  // chi: hand 2,3 wan, discard 1 wan
  auto opts = ListChiOptions(H({1, 2}), 0);
  Expect(!opts.empty(), "chi 123 wan");
  Expect(CanChi(H({1, 2}), 0), "can chi");
  Expect(!CanChi(H({1, 2}), kBai), "no chi bai discard");
  Expect(!CanChi(H({27, 28}), 29), "no chi feng");

  std::vector<Meld> melds;
  melds.push_back(Meld{MeldType::kPeng, 3, {}, 1});
  Expect(CanBuGang(H({3}), melds, 3), "bu gang");
  Expect(!CanBuGang(H({4}), melds, 4), "bu gang wrong tile");
}

void TestHu() {
  // classic: 11 123 123 123 123 — pair of 1wan + four chows of 123 wan... need 14 tiles
  // 1,1, 0,1,2, 0,1,2, 9,10,11, 18,19,20
  {
    auto h = H({0, 0, 0, 1, 2, 0, 1, 2, 9, 10, 11, 18, 19, 20});
    // that's wrong counts for 0. Let me build carefully:
  }
  // pair 东东, chows 123万 456万 789万 123条
  {
    auto h = H({27, 27, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11});
    auto r = CheckHu(h, {}, 27, true);
    Expect(r.ok && r.kind == HuKind::kPing, "ping hu basic");
  }
  // not hu
  {
    auto h = H({0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 14});
    Expect(!CheckHu(h, {}, 14, true).ok, "not hu");
  }
  // seven pairs
  {
    auto h = H({0, 0, 2, 2, 4, 4, 6, 6, 8, 8, 10, 10, 12, 12});
    auto r = CheckHu(h, {}, 12, true);
    Expect(r.ok && r.kind == HuKind::kQiDui && r.qing_qi_dui, "qing qi dui");
  }
  // seven pairs with one caishen
  {
    auto h = H({0, 0, 2, 2, 4, 4, 6, 6, 8, 8, 10, 10, 12, kBai});
    auto r = CheckHu(h, {}, kBai, true);
    Expect(r.ok && r.kind == HuKind::kQiDui && !r.qing_qi_dui, "qi dui with caishen");
  }
  // haohua: one quad + 5 pairs = 14? 4+10=14 -> 2+5=7 pair slots
  {
    auto h = H({0, 0, 0, 0, 2, 2, 4, 4, 6, 6, 8, 8, 10, 10});
    auto r = CheckHu(h, {}, 10, true);
    Expect(r.ok && r.kind == HuKind::kQiDui && r.haohua == 1, "hao hua qi dui");
  }
  // caishen as pair half (baotou style win)
  {
    // pair bai+1wan, rest: 234万 567万 789万 中中中
    auto h = H({kBai, 0, 1, 2, 3, 3, 4, 5, 6, 6, 7, 8, 31, 31, 31});
    // 15 tiles - fix: pair bai+0, chow 123(0,1,2) conflict. Use:
    // pair bai+东, 123万 456万 789万 发发发
    h = H({kBai, 27, 0, 1, 2, 3, 4, 5, 6, 7, 8, 32, 32, 32});
    auto r = CheckHu(h, {}, 27, true);
    Expect(r.ok && r.baotou, "baotou pair caishen+feng");
  }
  // would hu
  {
    auto h13 = H({27, 0, 1, 2, 3, 4, 5, 6, 7, 8, 32, 32, 32});
    Expect(WouldHu(h13, {}, 27, true), "would hu draw dong");
  }
}

void TestScore() {
  // T02 爆头 M=2；T03/T04 财飘；T05/T06 杠开/杠飘
  Expect(ComputeN(1, false) == 2, "T01 N ping=2");
  Expect(ComputeN(2, false) == 4, "N er lian");
  Expect(ComputeN(3, false) == 8, "T07 N san lao=8");
  Expect(ComputeN(1, true) == 8, "N start sanlao");

  HuResult ping;
  ping.ok = true;
  ping.kind = HuKind::kPing;
  Expect(ComputeM(ping, 0, 0) == 1, "T01 M ping=1");
  ping.baotou = true;
  Expect(ComputeM(ping, 0, 0) == 2, "T02 M baotou=2");
  Expect(ComputeM(ping, 1, 0) == 4, "T03 M piao=4");
  Expect(ComputeM(ping, 2, 0) == 8, "T04 M shuang piao=8");
  Expect(ComputeM(ping, 3, 0) == 16, "M san piao");
  ping.baotou = false;
  Expect(ComputeM(ping, 0, 1) == 2, "T05 M gang kai=2");
  Expect(ComputeM(ping, 0, 2) == 4, "M er lian gang");
  ping.baotou = true;
  Expect(ComputeM(ping, 0, 1) == 4, "M gang bao");
  Expect(ComputeM(ping, 1, 1) == 8, "T06 M gang piao=8");

  HuResult qd;
  qd.ok = true;
  qd.kind = HuKind::kQiDui;
  qd.qing_qi_dui = true;
  Expect(ComputeM(qd, 0, 0) == 4, "M qing qi dui");
  qd.haohua = 1;
  Expect(ComputeM(qd, 0, 0) == 8, "M hao hua qing");
}

void TestSettle() {
  // T01 闲家自摸平胡 N=2 M=1 base=100
  SettleInput in;
  in.winner_seat = 1;
  in.banker_seat = 0;
  in.N = 2;
  in.M = 1;
  in.base_score = 100;
  in.is_zimo = true;
  auto p = BuildSettle(in);
  Expect(p.stake == 100, "T01 stake 100");
  Expect(p.deltas[1] == 400, "T01 winner +400");
  Expect(p.deltas[0] == -200, "T01 banker -200");
  Expect(p.deltas[2] == -100 && p.deltas[3] == -100, "T01 xian -100");

  // SPEC example: xian zimo, N=2, M=4, base=100 -> stake=400
  in.M = 4;
  p = BuildSettle(in);
  Expect(p.stake == 400, "stake 400");
  Expect(p.deltas[1] == 1600, "winner +1600");
  Expect(p.deltas[0] == -800, "banker -800");
  Expect(p.deltas[2] == -400 && p.deltas[3] == -400, "xian -400");

  // banker zimo: each xian pays stake*N
  in.winner_seat = 0;
  in.banker_seat = 0;
  in.M = 1;
  in.N = 2;
  in.base_score = 100;
  p = BuildSettle(in);
  Expect(p.stake == 100, "stake 100");
  Expect(p.deltas[0] == 600, "banker win +600");
  Expect(p.deltas[1] == -200 && p.deltas[2] == -200 && p.deltas[3] == -200, "each -200");

  HzmjConfig cfg;
  Expect(!CanDianpao(cfg, 2, 1, 0, 0), "T08 no dianpao ping zhuang");
  Expect(CanDianpao(cfg, 8, 1, 0, 0), "T07 dianpao sanlao banker-xian");
  Expect(!CanDianpao(cfg, 8, 1, 2, 0), "no xian-xian dianpao");

  // T07 dianpao: shooter pays total zimo amount
  in.winner_seat = 0;
  in.banker_seat = 0;
  in.is_zimo = false;
  in.shooter_seat = 2;
  in.M = 1;
  in.N = 8;
  in.base_score = 100;
  p = BuildSettle(in);
  Expect(p.deltas[2] == -2400 && p.deltas[0] == 2400, "T07 dianpao shooter pays all");
  Expect(p.deltas[1] == 0 && p.deltas[3] == 0, "others 0");

  // T09 contractor
  in.is_zimo = true;
  in.shooter_seat = -1;
  in.N = 2;
  in.M = 1;
  in.base_score = 100;
  in.winner_seat = 1;
  in.banker_seat = 0;
  in.tan_count = {};
  in.tan_count[2][1] = 3;  // seat2 ate winner 3 times
  p = BuildSettle(in);
  Expect(p.contractor_seat == 2, "T09 contractor 2");
  Expect(p.deltas[2] < 0 && p.deltas[0] == 0 && p.deltas[3] == 0, "T09 only contractor pays");
  Expect(p.deltas[1] == -p.deltas[2], "T09 winner gets contractor pay");

  Expect(IdemSettleKey(9, 7) == "hzmj:settle:9:7", "idem key");
}

int main() {
  TestTiles();
  TestMeld();
  TestHu();
  TestScore();
  TestSettle();

  if (fails) {
    std::cerr << fails << " failures\n";
    return 1;
  }
  std::cout << "all passed\n";
  return 0;
}
