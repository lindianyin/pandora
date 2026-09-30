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
    Expect(r.ok && r.kind == HuKind::kQiDui && !r.qing_qi_dui && r.baotou, "qi dui with caishen is qi ke");
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
  // baotou ting: 123万456万789万中中中白
  {
    auto h13 = H({0, 1, 2, 3, 4, 5, 6, 7, 8, 31, 31, 31, kBai});
    auto ting = ComputeTing(h13, {});
    Expect(ting.baotou_ting, "baotou ting any-draw");
    Expect(static_cast<int>(ting.waits.size()) >= 30, "baotou waits many");
  }
  // hu with exposed peng meld
  {
    std::vector<Meld> melds;
    melds.push_back(Meld{MeldType::kPeng, 31, {}, 1});
    auto h = H({27, 27, 0, 1, 2, 3, 4, 5, 6, 7, 8});
    Expect(CheckHu(h, melds, 27, true).ok, "hu with peng meld");
  }
  // shuang hao hua qing qi dui M path via haohua=2
  {
    auto h = H({0, 0, 0, 0, 2, 2, 2, 2, 4, 4, 6, 6, 8, 8});
    auto r = CheckHu(h, {}, 8, true);
    Expect(r.ok && r.kind == HuKind::kQiDui && r.haohua == 2, "shuang hao hua");
  }
  // chi multi options for discard mid
  {
    auto opts = ListChiOptions(H({0, 2, 3}), 1);
    Expect(opts.size() >= 2, "chi multi options");
  }
  // 4 exposed melds + pair (caishen as half of the pair) is hu; 3 melds + that pair is short
  {
    std::vector<Meld> melds = {
        Meld{MeldType::kPeng, 9, {}, 0},
        Meld{MeldType::kPeng, 18, {}, 1},
        Meld{MeldType::kPeng, 27, {}, 2},
        Meld{MeldType::kChi, 0, {}, 3},
    };
    auto pair = H({2, kBai});
    Expect(CheckHu(pair, melds, kBai, true).ok, "4 melds + caishen pair");
    Expect(CheckHu(H({2, 2}), melds, 2, true).ok, "4 melds + real pair");
    melds.pop_back();
    Expect(!CheckHu(pair, melds, kBai, true).ok, "3 melds + pair is short");
  }
  // qi ke M comes from CheckHu, not a hand-built HuResult (must stay x4, not baotou x2 * qi dui x2)
  {
    auto h = H({0, 0, 2, 2, 4, 4, 6, 6, 8, 8, 10, 10, 12, kBai});
    auto r = CheckHu(h, {}, kBai, true);
    Expect(r.ok && r.kind == HuKind::kQiDui && r.baotou, "checkhu qi ke flags");
    Expect(ComputeM(r, 0, 0) == 4, "qi ke M from CheckHu is 4");
  }
  // exposed meld blocks qi dui even when the concealed tiles are pairs
  {
    auto closed = H({0, 0, 2, 2, 4, 4, 6, 6, 8, 8, 10, 10, 12, 12});
    Expect(CheckHu(closed, {}, 12, true).kind == HuKind::kQiDui, "closed qi dui");
    std::vector<Meld> melds = {Meld{MeldType::kPeng, 31, {}, 1}};
    auto r = CheckHu(closed, melds, 12, true);
    Expect(!r.ok && r.kind != HuKind::kQiDui, "14 plus peng is not qi dui");
    auto pairs = H({0, 0, 2, 2, 4, 4, 6, 6, 8, 8, 10});
    r = CheckHu(pairs, melds, 10, true);
    Expect(!r.ok && r.kind != HuKind::kQiDui, "pairs plus peng is not qi dui");
  }
  // three quads + one pair
  {
    auto h = H({0, 0, 0, 0, 2, 2, 2, 2, 4, 4, 4, 4, 6, 6});
    auto r = CheckHu(h, {}, 6, true);
    Expect(r.ok && r.kind == HuKind::kQiDui && r.haohua == 3 && r.qing_qi_dui, "san haohua qing qi dui");
    Expect(ComputeM(r, 0, 0) == 32, "san haohua qing M=32");
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
  Expect(ComputeM(ping, 1, 1) == 8, "M piao gang=8");  // same product
  ping.baotou = false;
  Expect(ComputeM(ping, 0, 3) == 8, "M san lian gang");
  Expect(ComputeM(ping, 0, 4) == 16, "M si lian gang");

  HuResult qd;
  qd.ok = true;
  qd.kind = HuKind::kQiDui;
  qd.qing_qi_dui = false;
  Expect(ComputeM(qd, 0, 0) == 2, "M qi dui with caishen");
  qd.haohua = 1;
  Expect(ComputeM(qd, 0, 0) == 4, "M hao hua qi dui");
  qd.haohua = 2;
  Expect(ComputeM(qd, 0, 0) == 8, "M shuang hao hua qi dui");
  qd.haohua = 3;
  Expect(ComputeM(qd, 0, 0) == 16, "M san hao hua qi dui");
  qd.haohua = 0;
  qd.qing_qi_dui = true;
  Expect(ComputeM(qd, 0, 0) == 4, "M qing qi dui");
  qd.haohua = 1;
  Expect(ComputeM(qd, 0, 0) == 8, "M hao hua qing");
  qd.haohua = 2;
  Expect(ComputeM(qd, 0, 0) == 16, "M shuang hao hua qing");
  qd.haohua = 3;
  Expect(ComputeM(qd, 0, 0) == 32, "M san hao hua qing");
  // qi ke: qi dui + baotou
  qd.haohua = 0;
  qd.qing_qi_dui = false;
  qd.baotou = true;
  Expect(ComputeM(qd, 0, 0) == 4, "M qi ke");
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
  cfg.xian_xian_dianpao = true;
  Expect(CanDianpao(cfg, 8, 1, 2, 0), "xian-xian dianpao when enabled");
  cfg.xian_xian_dianpao = false;

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

  // winner ate someone 3 times -> that someone contracts
  in.tan_count = {};
  in.tan_count[1][3] = 3;
  p = BuildSettle(in);
  Expect(p.contractor_seat == 3, "T09 reverse contractor");

  // both directions full: prefer the seat who ate the winner
  in.tan_count = {};
  in.tan_count[2][1] = 3;  // seat2 ate winner
  in.tan_count[1][3] = 3;  // winner ate seat3
  p = BuildSettle(in);
  Expect(p.contractor_seat == 2, "conflict prefers eater of winner");

  // er lian N=4 xian zimo M=1 base=100: stake=100, banker pays 400, xian 100 each
  in.winner_seat = 1;
  in.banker_seat = 0;
  in.is_zimo = true;
  in.shooter_seat = -1;
  in.N = 4;
  in.M = 1;
  in.base_score = 100;
  in.tan_count = {};
  p = BuildSettle(in);
  Expect(p.stake == 100, "er lian stake");
  Expect(p.deltas[1] == 600, "er lian winner +600");
  Expect(p.deltas[0] == -400, "er lian banker -400");
  Expect(p.deltas[2] == -100 && p.deltas[3] == -100, "er lian xian -100");

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
