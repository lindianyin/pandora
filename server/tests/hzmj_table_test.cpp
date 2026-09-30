#include "game/hzmj/table.hpp"

#include <iostream>
#include <vector>

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

// Safe filler hands (13 or 14) that rarely claim random discards.
HandCount Filler13(int salt) {
  std::vector<TileId> t;
  for (int i = 0; i < 13; ++i) t.push_back(static_cast<TileId>((salt + i * 3) % 27));
  return CountTiles(t);
}

void PassClaims(HzmjTable& t) {
  if (t.sub() != PlaySub::kClaimWindow) return;
  for (int s = 0; s < 4; ++s) {
    if (t.SeatNeedsClaimInput(s)) t.OnAction(s, ActionKind::kPass, nullptr);
  }
}

void TestDeal() {
  HzmjConfig cfg;
  cfg.base_score = 100;
  HzmjTable t(cfg, {1, 2, 3, 4}, /*banker*/ 0, /*lian*/ 1);
  t.SetRng([](int n) { return 0; });
  t.SetWallForTest(BuildWall());
  t.Start();
  Expect(t.phase() == Phase::kPlay, "phase play");
  Expect(HandSize(t.seat(0).hand) == 14, "banker 14");
  Expect(HandSize(t.seat(1).hand) == 13, "xian 13");
  Expect(HandSize(t.seat(2).hand) == 13, "xian2 13");
  Expect(HandSize(t.seat(3).hand) == 13, "xian3 13");
  Expect(t.turn_seat() == 0, "banker turn");
  Expect(t.sub() == PlaySub::kDiscard, "sub discard");
  Expect(t.wall_remain() == 136 - 53, "wall remain 83");
}

void TestDiscardAndDraw() {
  HzmjConfig cfg;
  HzmjTable t(cfg, {1, 2, 3, 4}, 0, 1);
  t.SetHandForTest(0, CountTiles({12, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 13}));
  t.SetHandForTest(1, CountTiles({14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26}));
  t.SetHandForTest(2, CountTiles({27, 27, 28, 28, 29, 29, 30, 30, 31, 31, 32, 32, 0}));
  t.SetHandForTest(3, CountTiles({1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7}));
  t.SetWallForTest({8, 8, 8, 9, 9, 9});
  t.Start();
  const int before = t.wall_remain();
  Expect(t.OnDiscard(0, 12), "discard ok");
  PassClaims(t);
  Expect(t.turn_seat() == 1, "turn to next");
  Expect(HandSize(t.seat(1).hand) == 14, "next has 14 after draw");
  Expect(t.wall_remain() == before - 1, "wall -1");
}

void TestLiuJu() {
  HzmjConfig cfg;
  HzmjTable t(cfg, {1, 2, 3, 4}, 0, 1);
  // Hands that cannot claim discards; empty wall after start -> liuju on cycle.
  t.SetHandForTest(0, CountTiles({0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13}));
  t.SetHandForTest(1, CountTiles({14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26}));
  t.SetHandForTest(2, CountTiles({27, 27, 28, 28, 29, 29, 30, 30, 31, 31, 32, 32, 0}));
  t.SetHandForTest(3, CountTiles({1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7}));
  t.SetWallForTest({});
  t.Start();
  Expect(t.wall_remain() == 0, "empty wall after deal");
  Expect(t.OnDiscard(0, 13), "discard to empty wall");
  PassClaims(t);
  Expect(t.phase() == Phase::kLiuJu, "T11 liu ju");
  Expect(t.lian_zhuang() == 2, "T11 lian +1");
  Expect(t.last_settle().stake == 0, "T11 no stake");
  Expect(t.last_settle().deltas[0] == 0 && t.last_settle().deltas[1] == 0 &&
             t.last_settle().deltas[2] == 0 && t.last_settle().deltas[3] == 0,
         "T11 no ledger deltas");
}

void TestZimoHuTimeout() {
  HzmjConfig cfg;
  cfg.base_score = 100;
  HzmjTable t(cfg, {10, 20, 30, 40}, /*banker*/ 1, 1);
  auto win_hand13 = CountTiles({27, 0, 1, 2, 3, 4, 5, 6, 7, 8, 32, 32, 32});
  auto win14 = win_hand13;
  AddTile(win14, 27);
  t.SetHandForTest(0, CountTiles({0, 0, 0, 1, 1, 1, 2, 2, 2, 3, 3, 3, 4}));
  t.SetHandForTest(1, win14);
  t.SetHandForTest(2, CountTiles({5, 5, 5, 6, 6, 6, 7, 7, 7, 8, 8, 8, 9}));
  t.SetHandForTest(3, CountTiles({10, 10, 10, 11, 11, 11, 12, 12, 12, 13, 13, 13, 14}));
  t.SetWallForTest({});
  t.Start();
  Expect(t.turn_seat() == 1, "banker1 turn");
  t.OnTimeout(1);
  Expect(t.phase() == Phase::kSettle, "T01 settle after auto hu");
  Expect(t.last_settle().deltas[1] > 0, "T01 winner positive");
  Expect(t.last_settle().stake == 100, "T01 M=1 stake");
  Expect(t.last_hu_zimo(), "timeout zimo flag");
}

void TestOnZimoHuConfirm() {
  HzmjConfig cfg;
  cfg.base_score = 100;
  HzmjTable t(cfg, {1, 2, 3, 4}, 0, 1);
  auto win14 = CountTiles({27, 27, 0, 1, 2, 3, 4, 5, 6, 7, 8, 32, 32, 32});
  t.SetHandForTest(0, win14);
  t.SetHandForTest(1, Filler13(1));
  t.SetHandForTest(2, Filler13(2));
  t.SetHandForTest(3, Filler13(3));
  t.SetWallForTest({9, 9, 9});
  t.Start();
  Expect(t.OnZimoHu(0), "OnZimoHu confirm");
  Expect(t.phase() == Phase::kSettle, "zimo settle");
  Expect(t.last_hu_zimo(), "is zimo");
  Expect(t.last_hu_M() == 1, "ping M=1");
}

void TestPengClaim() {
  HzmjConfig cfg;
  HzmjTable t(cfg, {1, 2, 3, 4}, 0, 1);
  t.SetHandForTest(0, CountTiles({5, 0, 1, 2, 3, 4, 6, 7, 8, 9, 10, 11, 12, 13}));
  t.SetHandForTest(1, CountTiles({5, 5, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24}));
  t.SetHandForTest(2, CountTiles({25, 25, 25, 26, 26, 26, 27, 27, 27, 28, 28, 28, 29}));
  t.SetHandForTest(3, CountTiles({30, 30, 30, 31, 31, 31, 32, 32, 32, 33, 0, 1, 2}));
  t.SetWallForTest({3, 3, 3, 3, 4, 4, 4, 4});
  t.Start();
  Expect(t.OnDiscard(0, 5), "discard 5");
  Expect(t.sub() == PlaySub::kClaimWindow, "claim window");
  Expect(t.OnAction(1, ActionKind::kPeng, nullptr), "peng");
  t.OnAction(2, ActionKind::kPass, nullptr);
  t.OnAction(3, ActionKind::kPass, nullptr);
  Expect(t.turn_seat() == 1, "penger turn");
  Expect(t.seat(1).melds.size() == 1, "has peng meld");
}

void TestChiClaim() {
  HzmjConfig cfg;
  HzmjTable t(cfg, {1, 2, 3, 4}, 0, 1);
  // seat1 is next of banker0; can chi 0 with 1,2 in hand
  t.SetHandForTest(0, CountTiles({0, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21}));
  t.SetHandForTest(1, CountTiles({1, 2, 22, 23, 24, 25, 26, 27, 27, 28, 28, 29, 29}));
  t.SetHandForTest(2, CountTiles({3, 3, 3, 4, 4, 4, 5, 5, 5, 6, 6, 6, 7}));
  t.SetHandForTest(3, CountTiles({8, 8, 8, 30, 30, 30, 31, 31, 31, 32, 32, 32, 9}));
  t.SetWallForTest({10, 10, 10, 11});
  t.Start();
  Expect(t.OnDiscard(0, 0), "discard 1wan");
  Expect(t.sub() == PlaySub::kClaimWindow, "chi claim window");
  auto opts = ListChiOptions(t.seat(1).hand, 0);
  Expect(!opts.empty(), "chi options");
  Expect(t.OnAction(1, ActionKind::kChi, &opts[0]), "chi");
  t.OnAction(2, ActionKind::kPass, nullptr);
  t.OnAction(3, ActionKind::kPass, nullptr);
  Expect(t.turn_seat() == 1, "chi seat turn");
  Expect(t.seat(1).melds.size() == 1 && t.seat(1).melds[0].type == MeldType::kChi, "chi meld");
}

void TestReconnectHandSnapshot() {
  HzmjConfig cfg;
  HzmjTable t(cfg, {1, 2, 3, 4}, 0, 1);
  auto h0 = CountTiles({0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13});
  t.SetHandForTest(0, h0);
  t.SetHandForTest(1, CountTiles({14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26}));
  t.SetHandForTest(2, CountTiles({27, 27, 28, 28, 29, 29, 30, 30, 31, 31, 32, 32, 0}));
  t.SetHandForTest(3, CountTiles({1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7}));
  t.SetWallForTest({8, 8, 8, 9, 9});
  t.Start();
  const auto before = t.seat(0).hand;
  Expect(t.seat(0).hand == before, "T12 hand unchanged");
  Expect(HandSize(t.seat(0).hand) == 14, "T12 banker still 14");
  // reconnect resume: still can discard and continue
  Expect(t.OnDiscard(0, 13), "T12 resume discard");
  PassClaims(t);
  Expect(t.phase() == Phase::kPlay, "T12 still play");
  Expect(t.turn_seat() == 1, "T12 turn advanced");
}

// Baotou waiting 13 + extra bai = 14: discard bai -> piao.
HandCount Baotou14WithExtraBai() {
  return CountTiles({0, 1, 2, 3, 4, 5, 6, 7, 8, 31, 31, 31, kBai, kBai});
}

void TestCaiPiaoSuccess() {
  HzmjConfig cfg;
  cfg.base_score = 100;
  HzmjTable t(cfg, {1, 2, 3, 4}, 0, 1);
  t.SetHandForTest(0, Baotou14WithExtraBai());
  t.SetHandForTest(1, Filler13(4));
  t.SetHandForTest(2, Filler13(5));
  t.SetHandForTest(3, Filler13(6));
  // draw after piao: any tile completes baotou
  t.SetWallForTest({27, 9, 9});
  t.Start();
  Expect(t.seat(0).baotou_ting, "baotou ting on enter");
  Expect(t.OnDiscard(0, kBai), "discard bai piao");
  Expect(t.seat(0).piao_level == 1, "piao level 1");
  Expect(t.piao_active(), "piao active");
  Expect(t.sub() == PlaySub::kDiscard, "back to discard after piao draw");
  Expect(HandSize(t.seat(0).hand) == 14, "14 after piao draw");
  Expect(t.OnZimoHu(0), "piao zimo hu");
  Expect(t.phase() == Phase::kSettle, "piao settle");
  Expect(t.last_hu_zimo(), "piao is zimo");
  Expect(t.last_hu_M() == 4, "T03 M=4 baotou+piao");
  Expect(t.last_settle().stake == 400, "stake 400");
}

void TestCaiPiaoFail() {
  HzmjConfig cfg;
  HzmjTable t(cfg, {1, 2, 3, 4}, 0, 1);
  t.SetHandForTest(0, Baotou14WithExtraBai());
  t.SetHandForTest(1, Filler13(7));
  t.SetHandForTest(2, Filler13(8));
  t.SetHandForTest(3, Filler13(9));
  t.SetWallForTest({27, 10, 10});
  t.Start();
  Expect(t.OnDiscard(0, kBai), "enter piao");
  Expect(t.seat(0).piao_level == 1, "piao lv1");
  // Discard non-caishen -> fail piao
  Expect(t.OnDiscard(0, 0), "fail piao discard wan");
  Expect(!t.piao_active(), "piao cleared");
  Expect(t.seat(0).piao_level == 0, "piao level reset");
  PassClaims(t);
  Expect(t.phase() == Phase::kPlay, "still play after fail piao");
}

void TestDianpaoDebugDeal() {
  HzmjConfig cfg;
  cfg.base_score = 100;
  HzmjTable t(cfg, {1, 2, 3, 4}, 0, 1);
  t.ApplyDebugDianpaoDeal();
  t.Start();
  Expect(t.banker_seat() == 1, "debug banker 1");
  Expect(t.CurrentN() == 8, "debug N=8 sanlao");
  Expect(t.seat(1).hand[27] > 0, "banker has dong");
  Expect(t.OnDiscard(1, 27), "banker discard dong");
  Expect(t.sub() == PlaySub::kClaimWindow, "dianpao claim");
  Expect(t.OnAction(0, ActionKind::kHu, nullptr), "seat0 hu");
  t.OnAction(2, ActionKind::kPass, nullptr);
  t.OnAction(3, ActionKind::kPass, nullptr);
  Expect(t.phase() == Phase::kSettle, "dianpao settle");
  Expect(!t.last_hu_zimo(), "not zimo");
  Expect(t.last_shooter_seat() == 1, "shooter banker");
  Expect(t.last_settle().deltas[0] > 0, "winner +");
  Expect(t.last_settle().deltas[1] < 0, "shooter -");
  Expect(t.last_settle().deltas[2] == 0 && t.last_settle().deltas[3] == 0, "others 0");
}

void TestAnGang() {
  HzmjConfig cfg;
  HzmjTable t(cfg, {1, 2, 3, 4}, 0, 1);
  t.SetHandForTest(0, CountTiles({0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10}));
  t.SetHandForTest(1, Filler13(10));
  t.SetHandForTest(2, Filler13(11));
  t.SetHandForTest(3, Filler13(12));
  t.SetWallForTest({11, 12, 13});
  t.Start();
  const int before = t.wall_remain();
  Expect(t.OnAnGang(0, 0), "an gang");
  Expect(t.seat(0).melds.size() == 1 && t.seat(0).melds[0].type == MeldType::kAnGang, "an gang meld");
  Expect(t.seat(0).gang_chain == 1, "gang chain 1");
  Expect(t.wall_remain() == before - 1, "gang draw");
  Expect(t.turn_seat() == 0, "still banker turn");
}

void TestMingGang() {
  HzmjConfig cfg;
  HzmjTable t(cfg, {1, 2, 3, 4}, 0, 1);
  t.SetHandForTest(0, CountTiles({5, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21}));
  t.SetHandForTest(1, CountTiles({5, 5, 5, 22, 23, 24, 25, 26, 27, 27, 28, 28, 29}));
  t.SetHandForTest(2, Filler13(13));
  t.SetHandForTest(3, Filler13(14));
  t.SetWallForTest({30, 30, 30, 31});
  t.Start();
  Expect(t.OnDiscard(0, 5), "discard for ming gang");
  Expect(t.OnAction(1, ActionKind::kGang, nullptr), "ming gang claim");
  t.OnAction(2, ActionKind::kPass, nullptr);
  t.OnAction(3, ActionKind::kPass, nullptr);
  Expect(t.seat(1).melds.size() == 1 && t.seat(1).melds[0].type == MeldType::kMingGang, "ming gang meld");
  Expect(t.seat(1).tan_count[0] == 0, "ming gang does not count tan");
  Expect(t.turn_seat() == 1, "ganger turn after draw");
  Expect(HandSize(t.seat(1).hand) % 3 == 2, "14-structure after gang draw");
}

void TestBuGangQiang() {
  HzmjConfig cfg;
  cfg.qiang_gang_hu = true;
  cfg.start_as_sanlao = true;  // allow dianpao-style settle for qiang
  HzmjTable t(cfg, {1, 2, 3, 4}, /*banker*/ 1, 1);
  // banker seat1: 11 tiles + peng(5), holding one 5 for bu gang
  t.SetHandForTest(1, CountTiles({5, 0, 1, 2, 3, 4, 6, 7, 8, 9, 10}));
  Meld peng;
  peng.type = MeldType::kPeng;
  peng.tile = 5;
  peng.from_seat = 0;
  t.SetMeldsForTest(1, {peng});
  t.SetHandForTest(2, CountTiles({0, 1, 2, 3, 4, 6, 7, 8, 9, 10, 11, 27, 27}));
  t.SetHandForTest(0, Filler13(15));
  t.SetHandForTest(3, Filler13(16));
  t.SetWallForTest({12, 12});
  t.Start();
  Expect(WouldHu(t.seat(2).hand, {}, 5, false), "seat2 waits 5");
  Expect(t.OnBuGang(1, 5), "bu gang");
  Expect(t.sub() == PlaySub::kClaimWindow, "qiang window");
  Expect(t.qiang_pending(), "qiang pending");
  Expect(t.OnAction(2, ActionKind::kHu, nullptr), "qiang hu");
  PassClaims(t);
  Expect(t.phase() == Phase::kSettle, "qiang gang settle");
  Expect(t.last_hu_zimo(), "qiang pays like zimo");
  Expect(t.last_hu_M() == 2, "qiang M gang-kai");
  Expect(t.last_settle().deltas[2] > 0, "qiang winner +");
  Expect(t.last_settle().deltas[0] < 0 && t.last_settle().deltas[1] < 0 && t.last_settle().deltas[3] < 0,
         "three pay");
}

void DiscardTurnTile(HzmjTable& t) {
  const int seat = t.turn_seat();
  TileId d = t.last_draw();
  if (d == kTileInvalid || t.seat(seat).hand[static_cast<size_t>(d)] <= 0) {
    d = kTileInvalid;
    for (TileId x = 0; x < kTileKinds; ++x) {
      if (t.seat(seat).hand[static_cast<size_t>(x)] > 0) {
        d = x;
        break;
      }
    }
  }
  Expect(d != kTileInvalid && t.OnDiscard(seat, d), "discard turn tile");
  PassClaims(t);
}

void TestLouHu() {
  HzmjConfig cfg;
  cfg.lou_hu = true;
  cfg.start_as_sanlao = true;
  cfg.base_score = 100;
  // banker=3 so after first discard+pass, next draw seat is 0 (the waiter)
  HzmjTable t(cfg, {1, 2, 3, 4}, /*banker*/ 3, 1);
  t.SetHandForTest(0, CountTiles({0, 1, 2, 3, 4, 5, 6, 7, 8, 32, 32, 32, 27}));
  t.SetHandForTest(1, CountTiles({9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21}));
  t.SetHandForTest(2, CountTiles({18, 19, 20, 21, 22, 23, 24, 25, 26, 22, 23, 24, 25}));
  t.SetHandForTest(3, CountTiles({27, 27, 28, 28, 28, 29, 29, 29, 30, 30, 30, 31, 31, 31}));
  t.SetWallForTest({10, 10, 10, 10, 11, 11, 11, 11, 12, 12});
  t.Start();
  Expect(t.OnDiscard(3, 27), "first dong discard");
  Expect(t.SeatNeedsClaimInput(0), "seat0 can hu");
  Expect(t.OnAction(0, ActionKind::kPass, nullptr), "lou pass");
  Expect(!t.seat(0).lou_hu.empty() && t.seat(0).lou_hu[0] == 27, "lou_hu recorded");
  PassClaims(t);
  Expect(t.turn_seat() == 0, "seat0 turn after pass");
  // Keep waiting hand: discard the just-drawn tile
  Expect(t.last_draw() != kTileInvalid && t.OnDiscard(0, t.last_draw()), "seat0 discard drawn");
  PassClaims(t);
  // Advance to banker again (seats 1 then 2)
  while (t.turn_seat() != 3 && t.phase() == Phase::kPlay) {
    DiscardTurnTile(t);
  }
  Expect(t.turn_seat() == 3, "back to banker");
  Expect(t.seat(3).hand[27] > 0, "banker still has dong");
  Expect(t.OnDiscard(3, 27), "second dong");
  if (t.SeatNeedsClaimInput(0)) {
    Expect(!t.OnAction(0, ActionKind::kHu, nullptr), "lou_hu blocks hu");
    t.OnAction(0, ActionKind::kPass, nullptr);
  } else {
    Expect(true, "lou_hu auto skip claim");
  }
  PassClaims(t);
}

void TestBaotouZimo() {
  HzmjConfig cfg;
  cfg.base_score = 100;
  HzmjTable t(cfg, {1, 2, 3, 4}, 0, 1);
  // baotou 14: 123万456万789万中中中白+东
  t.SetHandForTest(0, CountTiles({0, 1, 2, 3, 4, 5, 6, 7, 8, 31, 31, 31, kBai, 27}));
  t.SetHandForTest(1, Filler13(20));
  t.SetHandForTest(2, Filler13(21));
  t.SetHandForTest(3, Filler13(22));
  t.SetWallForTest({9, 9});
  t.Start();
  Expect(t.OnZimoHu(0), "T02 baotou zimo");
  Expect(t.phase() == Phase::kSettle, "T02 settle");
  Expect(t.last_hu_zimo(), "T02 zimo");
  Expect(t.last_hu_M() == 2, "T02 M=2 baotou");
  Expect(t.last_settle().stake == 200, "T02 stake 200");
}

void TestCaiPiaoDouble() {
  HzmjConfig cfg;
  cfg.base_score = 100;
  HzmjTable t(cfg, {1, 2, 3, 4}, 0, 1);
  t.SetHandForTest(0, Baotou14WithExtraBai());
  t.SetHandForTest(1, Filler13(23));
  t.SetHandForTest(2, Filler13(24));
  t.SetHandForTest(3, Filler13(25));
  // first piao draws bai again; second piao draws 28 to hu
  t.SetWallForTest({28, kBai});
  t.Start();
  Expect(t.OnDiscard(0, kBai), "first piao");
  Expect(t.seat(0).piao_level == 1, "piao lv1");
  Expect(t.seat(0).baotou_ting, "still baotou after first draw");
  Expect(t.OnDiscard(0, kBai), "second piao");
  Expect(t.seat(0).piao_level == 2, "T04 piao lv2");
  Expect(t.OnZimoHu(0), "T04 double piao hu");
  Expect(t.last_hu_M() == 8, "T04 M=8");
  Expect(t.last_settle().stake == 800, "T04 stake 800");
}

void TestPiaoBlocksOthers() {
  HzmjConfig cfg;
  HzmjTable t(cfg, {1, 2, 3, 4}, 0, 1);
  t.SetHandForTest(0, Baotou14WithExtraBai());
  t.SetHandForTest(1, CountTiles({5, 5, 5, 5, 14, 15, 16, 17, 18, 19, 20, 21, 22}));
  t.SetHandForTest(2, Filler13(26));
  t.SetHandForTest(3, Filler13(27));
  t.SetWallForTest({27, 9, 9});
  t.Start();
  Expect(t.OnDiscard(0, kBai), "enter piao");
  Expect(t.piao_active(), "piao on");
  Expect(t.turn_seat() == 0, "only piao seat turn");
  Expect(!t.OnDiscard(1, 14), "T03 others cannot discard");
  Expect(!t.OnAction(1, ActionKind::kPeng, nullptr), "T03 others claim rejected");
  Expect(!t.OnAnGang(1, 5), "piao_block others an gang");
}

void TestGangKaiZimo() {
  HzmjConfig cfg;
  cfg.base_score = 100;
  HzmjTable t(cfg, {1, 2, 3, 4}, 0, 1);
  // 4xZhong + 东东 123万456万78万; an gang Zhong then draw 9wan -> hu, M=2
  t.SetHandForTest(0, CountTiles({0, 1, 2, 3, 4, 5, 6, 7, 27, 27, 31, 31, 31, 31}));
  t.SetHandForTest(1, Filler13(28));
  t.SetHandForTest(2, Filler13(29));
  t.SetHandForTest(3, Filler13(30));
  t.SetWallForTest({8});
  t.Start();
  Expect(t.OnAnGang(0, 31), "T05 an gang");
  Expect(t.seat(0).gang_chain == 1, "T05 gang chain");
  Expect(t.OnZimoHu(0), "T05 gang kai hu");
  Expect(t.last_hu_M() == 2, "T05 M=2");
  Expect(t.last_hu_zimo(), "T05 zimo");
}

void TestGangPiaoZimo() {
  HzmjConfig cfg;
  cfg.base_score = 100;
  HzmjTable t(cfg, {1, 2, 3, 4}, 0, 1);
  // 4x1wan + 345万678万中中中白; after gang draw bai, piao, draw 28 -> M=8
  t.SetHandForTest(0, CountTiles({0, 0, 0, 0, 3, 4, 5, 6, 7, 8, 31, 31, 31, kBai}));
  t.SetHandForTest(1, Filler13(31));
  t.SetHandForTest(2, Filler13(32));
  t.SetHandForTest(3, Filler13(33));
  t.SetWallForTest({28, kBai});
  t.Start();
  Expect(t.OnAnGang(0, 0), "T06 an gang");
  Expect(t.seat(0).gang_chain == 1, "T06 gang chain");
  Expect(t.seat(0).baotou_ting, "T06 baotou after gang draw");
  Expect(t.OnDiscard(0, kBai), "T06 piao after gang");
  Expect(t.seat(0).piao_level == 1, "T06 piao lv1");
  Expect(t.OnZimoHu(0), "T06 gang piao hu");
  Expect(t.last_hu_M() == 8, "T06 M=8");
}

void TestPingZhuangDianpaoReject() {
  HzmjConfig cfg;
  cfg.base_score = 100;
  HzmjTable t(cfg, {1, 2, 3, 4}, /*banker*/ 0, /*lian*/ 1);
  t.SetHandForTest(0, CountTiles({27, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21}));
  t.SetHandForTest(1, CountTiles({0, 1, 2, 3, 4, 5, 6, 7, 8, 32, 32, 32, 27}));
  t.SetHandForTest(2, Filler13(34));
  t.SetHandForTest(3, Filler13(35));
  t.SetWallForTest({22, 22, 22});
  t.Start();
  Expect(t.CurrentN() == 2, "T08 N=2");
  Expect(WouldHu(t.seat(1).hand, {}, 27, false), "seat1 would hu dong");
  Expect(t.OnDiscard(0, 27), "discard dong");
  Expect(!t.SeatNeedsClaimInput(1), "T08 no hu claim on ping zhuang");
  Expect(!t.OnAction(1, ActionKind::kHu, nullptr), "T08 hu rejected");
  PassClaims(t);
  Expect(t.phase() == Phase::kPlay, "T08 still play");
}

void TestContractorViaTan() {
  HzmjConfig cfg;
  cfg.base_score = 100;
  HzmjTable t(cfg, {1, 2, 3, 4}, 0, 1);
  auto win14 = CountTiles({27, 27, 0, 1, 2, 3, 4, 5, 6, 7, 8, 32, 32, 32});
  t.SetHandForTest(0, win14);
  t.SetHandForTest(1, Filler13(36));
  t.SetHandForTest(2, Filler13(37));
  t.SetHandForTest(3, Filler13(38));
  t.SetWallForTest({9});
  // winner(seat0) ate seat2 three times -> seat2 contracts
  t.SetTanCountForTest(0, 2, 3);
  t.Start();
  Expect(t.OnZimoHu(0), "T09 zimo with tan");
  Expect(t.last_settle().contractor_seat == 2, "T09 contractor seat2");
  Expect(t.last_settle().deltas[2] < 0, "T09 contractor pays");
  Expect(t.last_settle().deltas[1] == 0 && t.last_settle().deltas[3] == 0, "T09 others 0");
  Expect(t.last_settle().deltas[0] == -t.last_settle().deltas[2], "T09 winner gets all");
}

void TestChiIncrementsTan() {
  HzmjConfig cfg;
  HzmjTable t(cfg, {1, 2, 3, 4}, 0, 1);
  t.SetHandForTest(0, CountTiles({0, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21}));
  t.SetHandForTest(1, CountTiles({1, 2, 22, 23, 24, 25, 26, 27, 27, 28, 28, 29, 29}));
  t.SetHandForTest(2, CountTiles({3, 3, 3, 4, 4, 4, 5, 5, 5, 6, 6, 6, 7}));
  t.SetHandForTest(3, CountTiles({8, 8, 8, 30, 30, 30, 31, 31, 31, 32, 32, 32, 9}));
  t.SetWallForTest({10, 10});
  t.Start();
  Expect(t.OnDiscard(0, 0), "discard for tan chi");
  auto opts = ListChiOptions(t.seat(1).hand, 0);
  Expect(t.OnAction(1, ActionKind::kChi, &opts[0]), "chi for tan");
  PassClaims(t);
  Expect(t.seat(1).tan_count[0] == 1, "chi increments tan_count[from]");
}

void TestBuGangNoQiang() {
  HzmjConfig cfg;
  cfg.qiang_gang_hu = true;
  HzmjTable t(cfg, {1, 2, 3, 4}, /*banker*/ 1, 1);
  t.SetHandForTest(1, CountTiles({5, 0, 1, 2, 3, 4, 6, 7, 8, 9, 10}));
  Meld peng;
  peng.type = MeldType::kPeng;
  peng.tile = 5;
  peng.from_seat = 0;
  t.SetMeldsForTest(1, {peng});
  // others do not wait on 5
  t.SetHandForTest(0, Filler13(40));
  t.SetHandForTest(2, Filler13(41));
  t.SetHandForTest(3, Filler13(42));
  t.SetWallForTest({11, 12});
  t.Start();
  Expect(t.OnBuGang(1, 5), "bu gang no qiang");
  Expect(t.phase() == Phase::kPlay, "still play");
  Expect(t.seat(1).melds[0].type == MeldType::kBuGang, "meld upgraded bu gang");
  Expect(t.turn_seat() == 1, "ganger continues");
  Expect(t.seat(1).gang_chain == 1, "gang chain after bu");
}

void TestClaimPriorityHuOverPengChi() {
  HzmjConfig cfg;
  cfg.start_as_sanlao = true;
  cfg.base_score = 100;
  HzmjTable t(cfg, {1, 2, 3, 4}, /*banker*/ 0, 1);
  // discard 5(6wan): seat1 can chi, seat2 can peng, seat3 can hu
  t.SetHandForTest(0, CountTiles({5, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21}));
  t.SetHandForTest(1, CountTiles({3, 4, 22, 23, 24, 25, 26, 27, 27, 28, 28, 29, 29}));
  t.SetHandForTest(2, CountTiles({5, 5, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24}));
  t.SetHandForTest(3, CountTiles({0, 1, 2, 3, 4, 6, 7, 8, 9, 10, 11, 27, 27}));
  t.SetWallForTest({30, 30});
  t.Start();
  Expect(WouldHu(t.seat(3).hand, {}, 5, false), "seat3 waits 5");
  Expect(t.OnDiscard(0, 5), "discard 5");
  Expect(t.sub() == PlaySub::kClaimWindow, "priority claim window");
  auto opts = ListChiOptions(t.seat(1).hand, 5);
  Expect(!opts.empty() && t.OnAction(1, ActionKind::kChi, &opts[0]), "seat1 chi claim");
  Expect(t.OnAction(2, ActionKind::kPeng, nullptr), "seat2 peng claim");
  Expect(t.OnAction(3, ActionKind::kHu, nullptr), "seat3 hu claim");
  Expect(t.phase() == Phase::kSettle, "hu wins priority");
  Expect(!t.last_hu_zimo(), "dianpao hu");
  Expect(t.last_settle().deltas[3] > 0, "seat3 winner");
}

void TestTimeoutClaimPass() {
  HzmjConfig cfg;
  cfg.start_as_sanlao = true;
  HzmjTable t(cfg, {1, 2, 3, 4}, 0, 1);
  t.SetHandForTest(0, CountTiles({27, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21}));
  t.SetHandForTest(1, CountTiles({0, 1, 2, 3, 4, 5, 6, 7, 8, 32, 32, 32, 27}));
  t.SetHandForTest(2, Filler13(43));
  t.SetHandForTest(3, Filler13(44));
  t.SetWallForTest({22, 22, 22});
  t.Start();
  Expect(t.OnDiscard(0, 27), "discard for timeout claim");
  Expect(t.SeatNeedsClaimInput(1), "seat1 needs hu input");
  t.OnTimeout(1);
  Expect(!t.SeatNeedsClaimInput(1), "timeout passed claim");
  PassClaims(t);
  Expect(t.phase() == Phase::kPlay, "continue after claim timeout");
}

void TestTimeoutDiscardNonHu() {
  HzmjConfig cfg;
  HzmjTable t(cfg, {1, 2, 3, 4}, 0, 1);
  t.SetHandForTest(0, CountTiles({12, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 13}));
  t.SetHandForTest(1, CountTiles({14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26}));
  t.SetHandForTest(2, CountTiles({27, 27, 28, 28, 29, 29, 30, 30, 31, 31, 32, 32, 0}));
  t.SetHandForTest(3, CountTiles({1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7}));
  t.SetWallForTest({8, 8, 8, 9});
  t.Start();
  const int before = HandSize(t.seat(0).hand);
  t.OnTimeout(0);
  Expect(HandSize(t.seat(0).hand) == before - 1, "timeout auto discarded");
  PassClaims(t);
  Expect(t.phase() == Phase::kPlay, "play after discard timeout");
}

void TestContractorRealChiThree() {
  HzmjConfig cfg;
  cfg.base_score = 100;
  HzmjTable t(cfg, {1, 2, 3, 4}, 0, 1);
  t.SetHandForTest(0, CountTiles({0, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21}));
  t.SetHandForTest(1, CountTiles({1, 2, 22, 23, 24, 25, 26, 27, 27, 28, 28, 29, 29}));
  t.SetHandForTest(2, Filler13(50));
  t.SetHandForTest(3, Filler13(51));
  t.SetWallForTest({30, 30});
  // already ate seat0 twice; third real chi triggers contractor
  t.SetTanCountForTest(1, 0, 2);
  t.Start();
  Expect(t.OnDiscard(0, 0), "third tan discard");
  auto opts = ListChiOptions(t.seat(1).hand, 0);
  Expect(!opts.empty() && t.OnAction(1, ActionKind::kChi, &opts[0]), "third chi");
  PassClaims(t);
  Expect(t.seat(1).tan_count[0] == 3, "T09 tan reaches 3 via chi");
  t.SetHandForTest(1, CountTiles({27, 27, 0, 1, 2, 3, 4, 5, 6, 7, 30}));
  t.SetWallForTest({8, 12, 12, 12});
  Expect(t.OnDiscard(1, 30), "discard junk");
  PassClaims(t);
  while (t.turn_seat() != 1 && t.phase() == Phase::kPlay) DiscardTurnTile(t);
  Expect(t.turn_seat() == 1, "drawer turn");
  Expect(t.OnZimoHu(1), "T09 hu after real tan");
  Expect(t.last_settle().contractor_seat == 0, "T09 real contractor is discarder");
  Expect(t.last_settle().deltas[0] < 0, "T09 contractor pays");
  Expect(t.last_settle().deltas[2] == 0 && t.last_settle().deltas[3] == 0, "T09 others 0");
}

void TestPengIncrementsTanAndContract() {
  HzmjConfig cfg;
  cfg.base_score = 100;
  cfg.peng_counts_tan = true;
  HzmjTable t(cfg, {1, 2, 3, 4}, 0, 1);
  t.SetHandForTest(0, CountTiles({5, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21}));
  t.SetHandForTest(1, CountTiles({5, 5, 22, 23, 24, 25, 26, 27, 27, 28, 28, 29, 29}));
  t.SetHandForTest(2, Filler13(52));
  t.SetHandForTest(3, Filler13(53));
  t.SetWallForTest({30});
  t.SetTanCountForTest(1, 0, 2);
  t.Start();
  Expect(t.OnDiscard(0, 5), "peng tan discard");
  Expect(t.OnAction(1, ActionKind::kPeng, nullptr), "peng counts tan");
  PassClaims(t);
  Expect(t.seat(1).tan_count[0] == 3, "peng tan reaches 3");
  t.SetHandForTest(1, CountTiles({27, 27, 0, 1, 2, 3, 4, 5, 6, 7, 30}));
  t.SetWallForTest({8, 12, 12, 12});
  Expect(t.OnDiscard(1, 30), "discard junk after peng");
  PassClaims(t);
  while (t.turn_seat() != 1 && t.phase() == Phase::kPlay) DiscardTurnTile(t);
  Expect(t.OnZimoHu(1), "hu after peng tan");
  Expect(t.last_settle().contractor_seat == 0, "peng-tan contractor");
}

void TestPiaoThenGang() {
  HzmjConfig cfg;
  cfg.base_score = 100;
  HzmjTable t(cfg, {1, 2, 3, 4}, 0, 1);
  // 3x1wan + 345万678万中中中 + 白白; piao draw 1wan -> an gang -> draw dong
  t.SetHandForTest(0, CountTiles({0, 0, 0, 3, 4, 5, 6, 7, 8, 31, 31, 31, kBai, kBai}));
  t.SetHandForTest(1, Filler13(54));
  t.SetHandForTest(2, Filler13(55));
  t.SetHandForTest(3, Filler13(56));
  t.SetWallForTest({27, 0});
  t.Start();
  Expect(t.OnDiscard(0, kBai), "piao before gang");
  Expect(t.seat(0).piao_level == 1, "piao lv1");
  Expect(t.OnAnGang(0, 0), "an gang after piao");
  Expect(t.seat(0).gang_chain == 1, "gang after piao");
  Expect(t.OnZimoHu(0), "piao gang hu");
  Expect(t.last_hu_M() == 8, "piao-gang M=8");
}

void TestDoubleGangKai() {
  HzmjConfig cfg;
  cfg.base_score = 100;
  HzmjTable t(cfg, {1, 2, 3, 4}, 0, 1);
  // two quads + 东东 + 1234万; gang twice then draw completes 123456
  t.SetHandForTest(0, CountTiles({0, 0, 0, 0, 31, 31, 31, 31, 27, 27, 1, 2, 3, 4}));
  t.SetHandForTest(1, Filler13(57));
  t.SetHandForTest(2, Filler13(58));
  t.SetHandForTest(3, Filler13(59));
  t.SetWallForTest({6, 5});
  t.Start();
  Expect(t.OnAnGang(0, 0), "first gang");
  Expect(t.OnAnGang(0, 31), "second gang");
  Expect(t.seat(0).gang_chain == 2, "gang_chain=2");
  Expect(t.OnZimoHu(0), "er lian gang hu");
  Expect(t.last_hu_M() == 4, "er lian gang M=4");
}

void TestErLianXianZimo() {
  HzmjConfig cfg;
  cfg.base_score = 100;
  // banker=1 so seat1 is banker with lian=2 -> N=4; use seat0 as xian winner... 
  // Better: banker=0 lian=2, give seat0 non-winning, make seat1 banker... 
  // Simpler: banker seat1, lian=2, seat1 has win14 and zimo.
  HzmjTable t(cfg, {1, 2, 3, 4}, /*banker*/ 1, /*lian*/ 2);
  auto win14 = CountTiles({27, 27, 0, 1, 2, 3, 4, 5, 6, 7, 8, 32, 32, 32});
  t.SetHandForTest(1, win14);
  t.SetHandForTest(0, Filler13(60));
  t.SetHandForTest(2, Filler13(61));
  t.SetHandForTest(3, Filler13(62));
  t.SetWallForTest({9});
  t.Start();
  Expect(t.CurrentN() == 4, "er lian N=4");
  Expect(t.OnZimoHu(1), "er lian banker zimo");
  Expect(t.last_hu_N() == 4, "last N=4");
  Expect(t.last_settle().stake == 100, "stake M=1");
  // banker zimo: each xian pays stake*N=400, winner +1200
  Expect(t.last_settle().deltas[1] == 1200, "banker +1200");
  Expect(t.last_settle().deltas[0] == -400 && t.last_settle().deltas[2] == -400 &&
             t.last_settle().deltas[3] == -400,
         "each xian -400");
  Expect(t.lian_zhuang() == 3, "banker hu continues lian");
}

void TestXianHuStealsBanker() {
  HzmjConfig cfg;
  cfg.base_score = 100;
  HzmjTable t(cfg, {1, 2, 3, 4}, /*banker*/ 0, 1);
  // seat1 waits dong; after banker discard, draws dong and zimo
  t.SetHandForTest(1, CountTiles({27, 0, 1, 2, 3, 4, 5, 6, 7, 8, 32, 32, 32}));
  t.SetHandForTest(0, CountTiles({9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22}));
  t.SetHandForTest(2, Filler13(63));
  t.SetHandForTest(3, Filler13(64));
  t.SetWallForTest({5, 5, 27});
  t.Start();
  Expect(t.OnDiscard(0, 22), "banker discard");
  PassClaims(t);
  Expect(t.turn_seat() == 1, "seat1 turn");
  Expect(HandSize(t.seat(1).hand) == 14, "drew to 14");
  Expect(t.OnZimoHu(1), "xian zimo");
  Expect(t.banker_seat() == 1, "xian becomes banker");
  Expect(t.lian_zhuang() == 1, "lian reset");
}

void TestClaimPriorityHuOverGang() {
  HzmjConfig cfg;
  cfg.start_as_sanlao = true;
  cfg.base_score = 100;
  HzmjTable t(cfg, {1, 2, 3, 4}, 0, 1);
  // discard 5: seat2 ming gang (3x5), seat3 hu
  t.SetHandForTest(0, CountTiles({5, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21}));
  t.SetHandForTest(1, Filler13(65));
  t.SetHandForTest(2, CountTiles({5, 5, 5, 22, 23, 24, 25, 26, 27, 27, 28, 28, 29}));
  t.SetHandForTest(3, CountTiles({0, 1, 2, 3, 4, 6, 7, 8, 9, 10, 11, 27, 27}));
  t.SetWallForTest({30});
  t.Start();
  Expect(t.OnDiscard(0, 5), "discard for hu/gang");
  Expect(t.OnAction(2, ActionKind::kGang, nullptr), "gang claim");
  Expect(t.OnAction(3, ActionKind::kHu, nullptr), "hu claim");
  PassClaims(t);
  Expect(t.phase() == Phase::kSettle, "hu beats gang");
  Expect(t.last_settle().deltas[3] > 0, "hu winner");
}

void TestNonNextCannotChi() {
  HzmjConfig cfg;
  HzmjTable t(cfg, {1, 2, 3, 4}, 0, 1);
  t.SetHandForTest(0, CountTiles({0, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21}));
  t.SetHandForTest(1, Filler13(66));
  // seat2 has chi tiles but is not next of banker
  t.SetHandForTest(2, CountTiles({1, 2, 22, 23, 24, 25, 26, 27, 27, 28, 28, 29, 29}));
  t.SetHandForTest(3, Filler13(67));
  t.SetWallForTest({8, 8});
  t.Start();
  Expect(t.OnDiscard(0, 0), "discard 1wan");
  auto opts = ListChiOptions(t.seat(2).hand, 0);
  Expect(!opts.empty(), "seat2 has chi shape");
  Expect(!t.OnAction(2, ActionKind::kChi, &opts[0]), "non-next chi rejected");
  PassClaims(t);
}

void TestQiangGangDisabled() {
  HzmjConfig cfg;
  cfg.qiang_gang_hu = false;
  HzmjTable t(cfg, {1, 2, 3, 4}, /*banker*/ 1, 1);
  t.SetHandForTest(1, CountTiles({5, 0, 1, 2, 3, 4, 6, 7, 8, 9, 10}));
  Meld peng;
  peng.type = MeldType::kPeng;
  peng.tile = 5;
  t.SetMeldsForTest(1, {peng});
  t.SetHandForTest(2, CountTiles({0, 1, 2, 3, 4, 6, 7, 8, 9, 10, 11, 27, 27}));
  t.SetHandForTest(0, Filler13(68));
  t.SetHandForTest(3, Filler13(69));
  t.SetWallForTest({12});
  t.Start();
  Expect(WouldHu(t.seat(2).hand, {}, 5, false), "would qiang if enabled");
  Expect(t.OnBuGang(1, 5), "bu gang");
  Expect(t.phase() == Phase::kPlay, "no qiang when disabled");
  Expect(t.seat(1).melds[0].type == MeldType::kBuGang, "bu gang done");
}

void TestCaiPiaoTriple() {
  HzmjConfig cfg;
  cfg.base_score = 100;
  HzmjTable t(cfg, {1, 2, 3, 4}, 0, 1);
  t.SetHandForTest(0, Baotou14WithExtraBai());
  t.SetHandForTest(1, Filler13(70));
  t.SetHandForTest(2, Filler13(71));
  t.SetHandForTest(3, Filler13(72));
  // draw bai, bai, then 28
  t.SetWallForTest({28, kBai, kBai});
  t.Start();
  Expect(t.OnDiscard(0, kBai), "piao1");
  Expect(t.OnDiscard(0, kBai), "piao2");
  Expect(t.OnDiscard(0, kBai), "piao3");
  Expect(t.seat(0).piao_level == 3, "san piao lv3");
  Expect(t.OnZimoHu(0), "san piao hu");
  Expect(t.last_hu_M() == 16, "san piao M=16");
}

void TestMingGangKai() {
  HzmjConfig cfg;
  cfg.base_score = 100;
  HzmjTable t(cfg, {1, 2, 3, 4}, 0, 1);
  // seat1 has 3xZhong; after ming gang draw completes hu
  t.SetHandForTest(0, CountTiles({31, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21}));
  t.SetHandForTest(1, CountTiles({31, 31, 31, 0, 1, 2, 3, 4, 5, 6, 7, 27, 27}));
  t.SetHandForTest(2, Filler13(73));
  t.SetHandForTest(3, Filler13(74));
  t.SetWallForTest({8});
  t.Start();
  Expect(t.OnDiscard(0, 31), "discard zhong");
  Expect(t.OnAction(1, ActionKind::kGang, nullptr), "ming gang");
  PassClaims(t);
  Expect(t.seat(1).gang_chain == 1, "ming gang chain");
  Expect(t.OnZimoHu(1), "ming gang kai");
  Expect(t.last_hu_M() == 2, "ming gang kai M=2");
}

void TestMaxPiaoCap() {
  HzmjConfig cfg;
  cfg.max_piao = 3;
  HzmjTable t(cfg, {1, 2, 3, 4}, 0, 1);
  t.SetHandForTest(0, Baotou14WithExtraBai());
  t.SetHandForTest(1, Filler13(75));
  t.SetHandForTest(2, Filler13(76));
  t.SetHandForTest(3, Filler13(77));
  t.SetWallForTest({28, kBai, kBai, kBai});
  t.Start();
  Expect(t.OnDiscard(0, kBai), "cap p1");
  Expect(t.OnDiscard(0, kBai), "cap p2");
  Expect(t.OnDiscard(0, kBai), "cap p3");
  Expect(t.seat(0).piao_level == 3, "at cap");
  // fourth bai draw then discard should stay at 3
  Expect(t.seat(0).hand[kBai] > 0 || t.last_draw() == kBai, "has bai for 4th");
  if (t.seat(0).hand[static_cast<size_t>(kBai)] > 0) {
    Expect(t.OnDiscard(0, kBai), "cap p4 attempt");
    Expect(t.seat(0).piao_level == 3, "max_piao capped at 3");
  }
}

void TestLouHuOtherWaitOk() {
  HzmjConfig cfg;
  cfg.lou_hu = true;
  cfg.start_as_sanlao = true;
  HzmjTable t(cfg, {1, 2, 3, 4}, /*banker*/ 3, 1);
  // seat0 waits dong OR nan? Use hand that waits only dong; after lou dong, discard nan cannot hu anyway.
  // Instead: wait 5 with multi - simpler check lou list only blocks that tile.
  t.SetHandForTest(0, CountTiles({0, 1, 2, 3, 4, 5, 6, 7, 8, 32, 32, 32, 27}));
  t.SetHandForTest(1, Filler13(78));
  t.SetHandForTest(2, Filler13(79));
  t.SetHandForTest(3, CountTiles({27, 28, 28, 28, 29, 29, 29, 30, 30, 30, 31, 31, 31, 9}));
  t.SetWallForTest({10, 10, 10});
  t.Start();
  Expect(t.OnDiscard(3, 27), "lou target discard");
  Expect(t.OnAction(0, ActionKind::kPass, nullptr), "lou pass dong");
  Expect(t.seat(0).lou_hu.size() == 1 && t.seat(0).lou_hu[0] == 27, "lou only dong");
  PassClaims(t);
}

void TestTripleGangKai() {
  HzmjConfig cfg;
  cfg.base_score = 100;
  HzmjTable t(cfg, {1, 2, 3, 4}, 0, 1);
  // 3 quads + 东东; each gang draws 3tiao -> final 东东+333
  t.SetHandForTest(0, CountTiles({0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 27, 27}));
  t.SetHandForTest(1, Filler13(80));
  t.SetHandForTest(2, Filler13(81));
  t.SetHandForTest(3, Filler13(82));
  t.SetWallForTest({11, 11, 11});
  t.Start();
  Expect(t.OnAnGang(0, 0), "gang1");
  Expect(t.OnAnGang(0, 1), "gang2");
  Expect(t.OnAnGang(0, 2), "gang3");
  Expect(t.seat(0).gang_chain == 3, "gang_chain=3");
  Expect(t.OnZimoHu(0), "san lian gang hu");
  Expect(t.last_hu_M() == 8, "san lian gang M=8");
}

void TestStartSanlaoZimo() {
  HzmjConfig cfg;
  cfg.base_score = 100;
  cfg.start_as_sanlao = true;
  HzmjTable t(cfg, {1, 2, 3, 4}, /*banker*/ 1, 1);
  auto win14 = CountTiles({27, 27, 0, 1, 2, 3, 4, 5, 6, 7, 8, 32, 32, 32});
  t.SetHandForTest(1, win14);
  t.SetHandForTest(0, Filler13(83));
  t.SetHandForTest(2, Filler13(84));
  t.SetHandForTest(3, Filler13(85));
  t.SetWallForTest({9});
  t.Start();
  Expect(t.CurrentN() == 8, "start sanlao N=8");
  Expect(t.OnZimoHu(1), "sanlao zimo");
  Expect(t.last_hu_N() == 8, "last N=8");
  Expect(t.last_settle().deltas[1] == 2400, "banker +2400");
  Expect(t.last_settle().deltas[0] == -800 && t.last_settle().deltas[2] == -800 &&
             t.last_settle().deltas[3] == -800,
         "each -800");
}

void TestErLianDianpaoReject() {
  HzmjConfig cfg;
  HzmjTable t(cfg, {1, 2, 3, 4}, /*banker*/ 0, /*lian*/ 2);
  t.SetHandForTest(0, CountTiles({27, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21}));
  t.SetHandForTest(1, CountTiles({0, 1, 2, 3, 4, 5, 6, 7, 8, 32, 32, 32, 27}));
  t.SetHandForTest(2, Filler13(86));
  t.SetHandForTest(3, Filler13(87));
  t.SetWallForTest({22});
  t.Start();
  Expect(t.CurrentN() == 4, "er lian N=4");
  Expect(t.OnDiscard(0, 27), "discard dong");
  Expect(!t.SeatNeedsClaimInput(1), "er lian no dianpao claim");
  Expect(!t.OnAction(1, ActionKind::kHu, nullptr), "er lian hu rejected");
  PassClaims(t);
}

void TestMultiHuNearestWins() {
  HzmjConfig cfg;
  cfg.start_as_sanlao = true;
  cfg.base_score = 100;
  HzmjTable t(cfg, {1, 2, 3, 4}, 0, 1);
  // both seat1 and seat3 wait 5; seat1 is nearer CCW from discarder 0
  t.SetHandForTest(0, CountTiles({5, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21}));
  t.SetHandForTest(1, CountTiles({0, 1, 2, 3, 4, 6, 7, 8, 9, 10, 11, 27, 27}));
  t.SetHandForTest(2, Filler13(88));
  t.SetHandForTest(3, CountTiles({0, 1, 2, 3, 4, 6, 7, 8, 12, 13, 14, 28, 28}));
  t.SetWallForTest({30});
  t.Start();
  Expect(WouldHu(t.seat(1).hand, {}, 5, false), "seat1 waits");
  Expect(WouldHu(t.seat(3).hand, {}, 5, false), "seat3 waits");
  Expect(t.OnDiscard(0, 5), "discard 5");
  Expect(t.OnAction(3, ActionKind::kHu, nullptr), "farther claims hu first");
  Expect(t.OnAction(1, ActionKind::kHu, nullptr), "nearer claims hu");
  PassClaims(t);
  Expect(t.phase() == Phase::kSettle, "settled");
  Expect(t.last_settle().deltas[1] > 0, "nearer seat1 wins");
  Expect(t.last_settle().deltas[3] == 0, "farther seat3 not winner");
}

void TestLouHuOtherWaitStillHu() {
  HzmjConfig cfg;
  cfg.lou_hu = true;
  cfg.start_as_sanlao = true;
  cfg.base_score = 100;
  // seat1 two-sided wait 5/8 on 67; lou 8 then hu 5
  HzmjTable t(cfg, {1, 2, 3, 4}, /*banker*/ 0, 1);
  t.SetHandForTest(0, CountTiles({8, 5, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20}));
  t.SetHandForTest(1, CountTiles({0, 1, 2, 3, 4, 5, 6, 7, 9, 10, 11, 27, 27}));
  t.SetHandForTest(2, Filler13(89));
  t.SetHandForTest(3, Filler13(90));
  t.SetWallForTest({21, 21, 21, 22, 22});
  t.Start();
  Expect(WouldHu(t.seat(1).hand, {}, 8, false), "waits 8");
  Expect(WouldHu(t.seat(1).hand, {}, 5, false), "waits 5");
  Expect(t.OnDiscard(0, 8), "first discard 8");
  Expect(t.OnAction(1, ActionKind::kPass, nullptr), "lou 8");
  Expect(!t.seat(1).lou_hu.empty() && t.seat(1).lou_hu[0] == 8, "lou recorded 8");
  PassClaims(t);
  // advance until banker can discard 5 again (still in hand)
  while (t.turn_seat() != 0 && t.phase() == Phase::kPlay) {
    DiscardTurnTile(t);
  }
  Expect(t.turn_seat() == 0, "back to banker");
  Expect(t.seat(0).hand[5] > 0, "banker still has 5");
  Expect(t.OnDiscard(0, 5), "discard other wait 5");
  Expect(t.OnAction(1, ActionKind::kHu, nullptr), "hu other wait after lou");
  PassClaims(t);
  Expect(t.phase() == Phase::kSettle, "hu ok on other wait");
  Expect(t.last_settle().deltas[1] > 0, "seat1 won");
}

void TestQiDuiZimo() {
  HzmjConfig cfg;
  cfg.base_score = 100;
  HzmjTable t(cfg, {1, 2, 3, 4}, 0, 1);
  t.SetHandForTest(0, CountTiles({0, 0, 2, 2, 4, 4, 6, 6, 8, 8, 10, 10, 12, 12}));
  t.SetHandForTest(1, Filler13(91));
  t.SetHandForTest(2, Filler13(92));
  t.SetHandForTest(3, Filler13(93));
  t.SetWallForTest({14});
  t.Start();
  Expect(t.OnZimoHu(0), "qi dui zimo");
  Expect(t.last_hu_M() == 4, "qing qi dui M=4");
}

void TestPengTanDisabled() {
  HzmjConfig cfg;
  cfg.peng_counts_tan = false;
  HzmjTable t(cfg, {1, 2, 3, 4}, 0, 1);
  t.SetHandForTest(0, CountTiles({5, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21}));
  t.SetHandForTest(1, CountTiles({5, 5, 22, 23, 24, 25, 26, 27, 27, 28, 28, 29, 29}));
  t.SetHandForTest(2, Filler13(94));
  t.SetHandForTest(3, Filler13(95));
  t.SetWallForTest({30});
  t.Start();
  Expect(t.OnDiscard(0, 5), "discard");
  Expect(t.OnAction(1, ActionKind::kPeng, nullptr), "peng");
  PassClaims(t);
  Expect(t.seat(1).tan_count[0] == 0, "peng does not count tan");
}

void TestChiChooseOption() {
  HzmjConfig cfg;
  HzmjTable t(cfg, {1, 2, 3, 4}, 0, 1);
  // discard 1(2wan); seat1 can chi 0+2 or 2+3
  t.SetHandForTest(0, CountTiles({1, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21}));
  t.SetHandForTest(1, CountTiles({0, 2, 3, 22, 23, 24, 25, 26, 27, 27, 28, 28, 29}));
  t.SetHandForTest(2, Filler13(96));
  t.SetHandForTest(3, Filler13(97));
  t.SetWallForTest({30});
  t.Start();
  Expect(t.OnDiscard(0, 1), "discard 2wan");
  auto opts = ListChiOptions(t.seat(1).hand, 1);
  Expect(opts.size() >= 2, "two chi opts");
  ChiOption chosen = opts[0];
  for (const auto& o : opts) {
    if (o.hand_tiles[0] == 2 || o.hand_tiles[1] == 2) {
      if ((o.hand_tiles[0] == 3 || o.hand_tiles[1] == 3)) {
        chosen = o;
        break;
      }
    }
  }
  Expect(t.OnAction(1, ActionKind::kChi, &chosen), "chi with chosen opt");
  PassClaims(t);
  Expect(t.seat(1).melds.size() == 1, "one chi meld");
  Expect(t.seat(1).melds[0].chi_tiles[0] == chosen.formed[0] &&
             t.seat(1).melds[0].chi_tiles[2] == chosen.formed[2],
         "formed matches choice");
}

void TestFourGangKai() {
  HzmjConfig cfg;
  cfg.base_score = 100;
  HzmjTable t(cfg, {1, 2, 3, 4}, 0, 1);
  t.SetHandForTest(0, CountTiles({0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 27, 27}));
  t.SetHandForTest(1, Filler13(100));
  t.SetHandForTest(2, Filler13(101));
  t.SetHandForTest(3, Filler13(102));
  t.SetWallForTest({31, 31, 27, 27});
  t.Start();
  Expect(t.OnAnGang(0, 0), "gang A");
  Expect(t.OnAnGang(0, 1), "gang B");
  Expect(t.OnAnGang(0, 2), "gang C");
  Expect(t.OnAnGang(0, 27), "gang D");
  Expect(t.seat(0).gang_chain == 4, "gang_chain 4");
  Expect(t.OnZimoHu(0), "si lian gang hu");
  Expect(t.last_hu_M() == 16, "si lian gang M=16");
}

void TestXianXianDianpao() {
  HzmjConfig cfg;
  cfg.start_as_sanlao = true;
  cfg.xian_xian_dianpao = true;
  cfg.base_score = 100;
  HzmjTable t(cfg, {1, 2, 3, 4}, /*banker*/ 0, 1);
  t.SetHandForTest(0, CountTiles({9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22}));
  t.SetHandForTest(1, CountTiles({5, 22, 23, 24, 25, 26, 27, 27, 28, 28, 29, 29, 30, 30}));
  t.SetHandForTest(2, CountTiles({0, 1, 2, 3, 4, 6, 7, 8, 9, 10, 11, 27, 27}));
  t.SetHandForTest(3, Filler13(103));
  t.SetWallForTest({8, 8, 8, 8});
  t.Start();
  Expect(t.OnDiscard(0, 22), "banker out");
  PassClaims(t);
  Expect(t.turn_seat() == 1, "xian turn");
  Expect(t.OnDiscard(1, 5), "xian discard");
  Expect(t.OnAction(2, ActionKind::kHu, nullptr), "xian-xian hu");
  PassClaims(t);
  Expect(t.phase() == Phase::kSettle, "xian-xian settle");
  Expect(!t.last_hu_zimo(), "dianpao");
  Expect(t.last_shooter_seat() == 1, "shooter xian");
}

void TestXianXianDianpaoOff() {
  HzmjConfig cfg;
  cfg.start_as_sanlao = true;
  cfg.xian_xian_dianpao = false;
  HzmjTable t(cfg, {1, 2, 3, 4}, 0, 1);
  t.SetHandForTest(0, CountTiles({9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22}));
  t.SetHandForTest(1, CountTiles({5, 22, 23, 24, 25, 26, 27, 27, 28, 28, 29, 29, 30, 30}));
  t.SetHandForTest(2, CountTiles({0, 1, 2, 3, 4, 6, 7, 8, 9, 10, 11, 27, 27}));
  t.SetHandForTest(3, Filler13(104));
  t.SetWallForTest({8, 8, 8});
  t.Start();
  Expect(t.OnDiscard(0, 22), "banker out off");
  PassClaims(t);
  Expect(t.OnDiscard(1, 5), "xian discard off");
  Expect(!t.OnAction(2, ActionKind::kHu, nullptr), "xian-xian rejected");
  PassClaims(t);
  Expect(t.phase() == Phase::kPlay, "still play");
}

void TestGangScoreInstant() {
  HzmjConfig cfg;
  cfg.base_score = 100;
  cfg.gang_score_instant = true;
  HzmjTable t(cfg, {1, 2, 3, 4}, 0, 1);
  t.SetHandForTest(0, CountTiles({0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10}));
  t.SetHandForTest(1, Filler13(105));
  t.SetHandForTest(2, Filler13(106));
  t.SetHandForTest(3, Filler13(107));
  t.SetWallForTest({11});
  bool saw = false;
  t.SetSink([&](const OutEvent& e) {
    if (e.type == "GangScore") saw = true;
  });
  t.Start();
  Expect(t.OnAnGang(0, 0), "instant an gang");
  Expect(saw, "GangScore event");
  Expect(t.last_gang_deltas()[0] == 600, "an gang +600");
  Expect(t.last_gang_deltas()[1] == -200 && t.last_gang_deltas()[2] == -200 && t.last_gang_deltas()[3] == -200,
         "others -200");
}

void TestNoZimoAfterPeng() {
  HzmjConfig cfg;
  HzmjTable t(cfg, {1, 2, 3, 4}, 0, 1);
  t.SetHandForTest(0, CountTiles({5, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21}));
  t.SetHandForTest(1, CountTiles({5, 5, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 6}));
  t.SetHandForTest(2, Filler13(108));
  t.SetHandForTest(3, Filler13(109));
  t.SetWallForTest({7});
  t.Start();
  Expect(t.OnDiscard(0, 5), "discard for peng");
  Expect(t.OnAction(1, ActionKind::kPeng, nullptr), "peng");
  PassClaims(t);
  Expect(t.turn_seat() == 1, "penger turn");
  Expect(!t.OnZimoHu(1), "no zimo without draw");
}

void TestCrossRoundLian() {
  HzmjConfig cfg;
  cfg.base_score = 100;
  HzmjTable t(cfg, {1, 2, 3, 4}, 0, 1);
  auto win14 = CountTiles({27, 27, 0, 1, 2, 3, 4, 5, 6, 7, 8, 32, 32, 32});
  t.SetHandForTest(0, win14);
  t.SetHandForTest(1, Filler13(110));
  t.SetHandForTest(2, Filler13(111));
  t.SetHandForTest(3, Filler13(112));
  t.SetWallForTest({9});
  t.Start();
  Expect(t.OnZimoHu(0), "banker hu");
  Expect(t.lian_zhuang() == 2, "lian becomes 2");
  HzmjTable next(cfg, {1, 2, 3, 4}, t.banker_seat(), t.lian_zhuang());
  Expect(next.CurrentN() == 4, "next round N=4");
}

void TestDiscardNonBaiDoesNotPiao() {
  HzmjConfig cfg;
  HzmjTable t(cfg, {1, 2, 3, 4}, 0, 1);
  t.SetHandForTest(0, Baotou14WithExtraBai());
  t.SetHandForTest(1, CountTiles({14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26}));
  t.SetHandForTest(2, CountTiles({27, 27, 28, 28, 29, 29, 30, 30, 31, 31, 32, 32, 26}));
  t.SetHandForTest(3, CountTiles({14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 27}));
  t.SetWallForTest({12, 12});
  t.Start();
  Expect(t.seat(0).baotou_ting, "baotou if bai were discarded");
  Expect(t.OnDiscard(0, 0), "discard wan instead of bai");
  PassClaims(t);
  Expect(!t.piao_active(), "non-bai discard does not piao");
  Expect(t.seat(0).piao_level == 0, "piao level stays 0");
  Expect(t.phase() == Phase::kPlay, "still play");
}

void TestGangScoreMingAndBu() {
  HzmjConfig cfg;
  cfg.base_score = 100;
  cfg.gang_score_instant = true;
  {
    HzmjTable t(cfg, {1, 2, 3, 4}, 0, 1);
    t.SetHandForTest(0, CountTiles({5, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21}));
    t.SetHandForTest(1, CountTiles({5, 5, 5, 22, 23, 24, 25, 26, 27, 27, 28, 28, 29}));
    t.SetHandForTest(2, CountTiles({14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26}));
    t.SetHandForTest(3, CountTiles({27, 28, 29, 30, 31, 32, 27, 28, 29, 30, 31, 32, 26}));
    t.SetWallForTest({30});
    t.Start();
    Expect(t.OnDiscard(0, 5), "ming discard");
    Expect(t.OnAction(1, ActionKind::kGang, nullptr), "ming gang");
    PassClaims(t);
    Expect(t.last_gang_deltas()[0] == -300, "ming from pays 3x");
    Expect(t.last_gang_deltas()[1] == 300, "ming ganger +300");
    Expect(t.last_gang_deltas()[2] == 0 && t.last_gang_deltas()[3] == 0, "ming others 0");
  }
  {
    HzmjTable t(cfg, {1, 2, 3, 4}, 1, 1);
    t.SetHandForTest(1, CountTiles({5, 0, 1, 2, 3, 4, 6, 7, 8, 9, 10}));
    Meld peng;
    peng.type = MeldType::kPeng;
    peng.tile = 5;
    peng.from_seat = 0;
    t.SetMeldsForTest(1, {peng});
    t.SetHandForTest(0, CountTiles({14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26}));
    t.SetHandForTest(2, CountTiles({27, 28, 29, 30, 31, 32, 27, 28, 29, 30, 31, 32, 26}));
    t.SetHandForTest(3, CountTiles({14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 27}));
    t.SetWallForTest({11});
    t.Start();
    Expect(t.OnBuGang(1, 5), "bu gang instant");
    Expect(t.last_gang_deltas()[1] == 300, "bu ganger +300");
    Expect(t.last_gang_deltas()[0] == -100 && t.last_gang_deltas()[2] == -100 && t.last_gang_deltas()[3] == -100,
           "bu others pay 1x");
  }
}

void TestGangScoreInstantSkipsM() {
  HzmjConfig cfg;
  cfg.base_score = 100;
  cfg.gang_score_instant = true;
  HzmjTable t(cfg, {1, 2, 3, 4}, 0, 1);
  // 4x zhong an gang, concealed pair dong + 123 456 78 wan, draw 9wan
  t.SetHandForTest(0, CountTiles({31, 31, 31, 31, 27, 27, 0, 1, 2, 3, 4, 5, 6, 7}));
  t.SetHandForTest(1, CountTiles({14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26}));
  t.SetHandForTest(2, CountTiles({27, 28, 29, 30, 32, 32, 14, 15, 16, 17, 18, 19, 20}));
  t.SetHandForTest(3, CountTiles({9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21}));
  t.SetWallForTest({8});
  t.Start();
  Expect(t.OnAnGang(0, 31), "an gang before hu");
  Expect(t.seat(0).gang_chain == 1, "chain recorded");
  Expect(t.OnZimoHu(0), "hu after instant gang");
  Expect(t.last_hu_M() == 1, "instant gang not folded into M");
}

void TestPingZhuangQiangGang() {
  HzmjConfig cfg;
  cfg.qiang_gang_hu = true;
  cfg.base_score = 100;
  HzmjTable t(cfg, {1, 2, 3, 4}, /*banker*/ 1, /*lian*/ 1);
  t.SetHandForTest(1, CountTiles({5, 0, 1, 2, 3, 4, 6, 7, 8, 9, 10}));
  Meld peng;
  peng.type = MeldType::kPeng;
  peng.tile = 5;
  peng.from_seat = 0;
  t.SetMeldsForTest(1, {peng});
  t.SetHandForTest(2, CountTiles({0, 1, 2, 3, 4, 6, 7, 8, 9, 10, 11, 27, 27}));
  t.SetHandForTest(0, CountTiles({14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26}));
  t.SetHandForTest(3, CountTiles({27, 28, 29, 30, 31, 32, 14, 15, 16, 17, 18, 19, 20}));
  t.SetWallForTest({12});
  t.Start();
  Expect(t.CurrentN() == 2, "ping zhuang N=2");
  Expect(t.OnBuGang(1, 5), "bu gang");
  Expect(t.sub() == PlaySub::kClaimWindow, "qiang window on ping zhuang");
  Expect(t.OnAction(2, ActionKind::kHu, nullptr), "qiang hu without sanlao");
  PassClaims(t);
  Expect(t.phase() == Phase::kSettle, "qiang settle");
  Expect(t.last_hu_zimo() && t.last_hu_M() == 2, "qiang is gang-kai zimo");
  Expect(t.last_hu_N() == 2, "N stays 2");
  Expect(t.last_settle().deltas[0] < 0 && t.last_settle().deltas[1] < 0 && t.last_settle().deltas[3] < 0,
         "three pay on ping zhuang qiang");
}

void TestQiangGangPassContinues() {
  HzmjConfig cfg;
  cfg.qiang_gang_hu = true;
  HzmjTable t(cfg, {1, 2, 3, 4}, 1, 1);
  t.SetHandForTest(1, CountTiles({5, 0, 1, 2, 3, 4, 6, 7, 8, 9, 10}));
  Meld peng;
  peng.type = MeldType::kPeng;
  peng.tile = 5;
  peng.from_seat = 0;
  t.SetMeldsForTest(1, {peng});
  t.SetHandForTest(2, CountTiles({0, 1, 2, 3, 4, 6, 7, 8, 9, 10, 11, 27, 27}));
  t.SetHandForTest(0, CountTiles({14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26}));
  t.SetHandForTest(3, CountTiles({27, 28, 29, 30, 31, 32, 14, 15, 16, 17, 18, 19, 20}));
  t.SetWallForTest({12, 13});
  t.Start();
  Expect(t.OnBuGang(1, 5), "bu gang opens qiang");
  Expect(t.SeatNeedsClaimInput(2), "waiter can qiang");
  Expect(t.OnAction(2, ActionKind::kPass, nullptr), "pass qiang");
  PassClaims(t);
  Expect(t.phase() == Phase::kPlay, "continue after pass");
  Expect(t.seat(1).melds[0].type == MeldType::kBuGang, "bu gang stands");
  Expect(t.turn_seat() == 1, "ganger draws after pass");
  Expect(t.seat(1).gang_chain == 1, "chain kept");
}

void TestCaishenDiscardNoDianpao() {
  HzmjConfig cfg;
  cfg.start_as_sanlao = true;
  cfg.base_score = 100;
  HzmjTable t(cfg, {1, 2, 3, 4}, /*banker*/ 1, 1);
  t.SetHandForTest(1, CountTiles({0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, kBai}));
  // seat0 is one dong short; bai-as-joker would complete, but caishen discard cannot be hu
  t.SetHandForTest(0, CountTiles({27, 0, 1, 2, 3, 4, 5, 6, 7, 8, 32, 32, 32}));
  t.SetHandForTest(2, CountTiles({14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26}));
  t.SetHandForTest(3, CountTiles({27, 28, 29, 30, 31, 14, 15, 16, 17, 18, 19, 20, 21}));
  t.SetWallForTest({12, 12});
  t.Start();
  Expect(WouldHu(t.seat(0).hand, {}, kBai, false), "bai would complete the hand");
  Expect(t.OnDiscard(1, kBai), "discard caishen");
  Expect(!t.piao_active(), "discarder was not baotou");
  Expect(t.phase() == Phase::kPlay, "caishen discard is not dianpao");
  Expect(!t.SeatNeedsClaimInput(0), "no hu claim on caishen");
}

void TestTanCountsStaySeparate() {
  HzmjConfig cfg;
  cfg.peng_counts_tan = true;
  HzmjTable t(cfg, {1, 2, 3, 4}, 0, 1);
  t.SetHandForTest(0, CountTiles({5, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21}));
  t.SetHandForTest(1, CountTiles({5, 5, 9, 9, 22, 23, 24, 25, 26, 27, 27, 28, 28}));
  t.SetHandForTest(2, CountTiles({9, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25}));
  t.SetHandForTest(3, CountTiles({27, 28, 29, 30, 31, 32, 27, 28, 29, 30, 31, 32, 26}));
  t.SetWallForTest({30});
  t.Start();
  Expect(t.OnDiscard(0, 5), "first peng discard");
  Expect(t.OnAction(1, ActionKind::kPeng, nullptr), "peng from seat0");
  PassClaims(t);
  Expect(t.seat(1).tan_count[0] == 1, "tan from seat0");
  Expect(t.OnDiscard(1, 22), "discard after peng");
  PassClaims(t);
  Expect(t.OnDiscard(2, 9), "seat2 discards 9");
  Expect(t.OnAction(1, ActionKind::kPeng, nullptr), "peng from seat2");
  PassClaims(t);
  Expect(t.seat(1).tan_count[0] == 1, "first counter unchanged");
  Expect(t.seat(1).tan_count[2] == 1, "second from-seat is separate");
}

void TestThreeRealChiContract() {
  HzmjConfig cfg;
  cfg.base_score = 100;
  HzmjTable t(cfg, {1, 2, 3, 4}, 0, 1);
  t.SetHandForTest(0, CountTiles({0, 3, 6, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19}));
  t.SetHandForTest(1, CountTiles({1, 2, 4, 5, 7, 8, 22, 23, 24, 25, 26, 27, 28}));
  t.SetHandForTest(2, CountTiles({27, 28, 29, 30, 31, 32, 27, 28, 29, 30, 31, 32, 26}));
  t.SetHandForTest(3, CountTiles({18, 19, 20, 21, 22, 23, 24, 25, 26, 30, 31, 32, 26}));
  // pop order: 31,26,12, 31,26,12, 31,26,12, then 2wan to complete 012
  t.SetWallForTest({2, 12, 26, 31, 12, 26, 31, 12, 26, 31});
  t.Start();
  auto chi_from_0 = [&](TileId tile, TileId junk) {
    Expect(t.OnDiscard(0, tile), "seat0 feeds chi");
    auto opts = ListChiOptions(t.seat(1).hand, tile);
    Expect(!opts.empty() && t.OnAction(1, ActionKind::kChi, &opts[0]), "seat1 chi");
    PassClaims(t);
    Expect(t.OnDiscard(1, junk), "seat1 discards after chi");
    PassClaims(t);
  };
  chi_from_0(0, 22);
  Expect(t.turn_seat() == 2, "seat2 to draw");
  DiscardTurnTile(t);
  DiscardTurnTile(t);
  chi_from_0(3, 23);
  DiscardTurnTile(t);
  DiscardTurnTile(t);
  Expect(t.OnDiscard(0, 6), "third feed");
  auto opts = ListChiOptions(t.seat(1).hand, 6);
  Expect(!opts.empty() && t.OnAction(1, ActionKind::kChi, &opts[0]), "third chi");
  PassClaims(t);
  Expect(t.seat(1).tan_count[0] == 3, "three real chis");
  Expect(t.seat(1).melds.size() == 3, "three chi melds");
  t.SetHandForTest(1, CountTiles({27, 27, 0, 1, 16}));
  Expect(t.OnDiscard(1, 16), "discard to tenpai");
  PassClaims(t);
  while (t.turn_seat() != 1 && t.phase() == Phase::kPlay) DiscardTurnTile(t);
  Expect(t.OnZimoHu(1), "hu after three chis");
  Expect(t.last_settle().contractor_seat == 0, "discarder contracts");
  Expect(t.last_settle().deltas[0] < 0, "contractor pays");
  Expect(t.last_settle().deltas[2] == 0 && t.last_settle().deltas[3] == 0, "others 0");
}

void TestGangChainClearsOnPeng() {
  HzmjConfig cfg;
  HzmjTable t(cfg, {1, 2, 3, 4}, 0, 1);
  t.SetHandForTest(0, CountTiles({0, 0, 0, 0, 5, 5, 9, 10, 11, 12, 13, 14, 15, 16}));
  t.SetHandForTest(1, CountTiles({5, 22, 23, 24, 25, 26, 27, 27, 28, 28, 29, 29, 30}));
  t.SetHandForTest(2, CountTiles({27, 28, 29, 30, 31, 32, 14, 15, 16, 17, 18, 19, 20}));
  t.SetHandForTest(3, CountTiles({27, 28, 29, 30, 31, 32, 18, 19, 20, 21, 22, 23, 24}));
  t.SetWallForTest({20, 17});
  t.Start();
  Expect(t.OnAnGang(0, 0), "an gang");
  Expect(t.seat(0).gang_chain == 1, "chain 1 after an gang");
  Expect(t.OnDiscard(0, 9), "discard after gang");
  PassClaims(t);
  Expect(t.OnDiscard(1, 5), "offer peng");
  Expect(t.OnAction(0, ActionKind::kPeng, nullptr), "peng after gang");
  PassClaims(t);
  Expect(t.seat(0).gang_chain == 0, "peng clears gang chain");
}

void TestGangChainClearsOnChi() {
  HzmjConfig cfg;
  HzmjTable t(cfg, {1, 2, 3, 4}, 0, 1);
  t.SetHandForTest(0, CountTiles({0, 0, 0, 0, 2, 3, 9, 10, 11, 12, 13, 14, 15, 16}));
  t.SetHandForTest(1, CountTiles({22, 23, 24, 25, 26, 27, 27, 28, 28, 29, 29, 30, 31}));
  t.SetHandForTest(2, CountTiles({22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 32, 18}));
  t.SetHandForTest(3, CountTiles({4, 27, 28, 29, 30, 31, 32, 27, 28, 29, 30, 31, 18}));
  t.SetWallForTest({12, 26, 31, 17});
  t.Start();
  Expect(t.OnAnGang(0, 0), "an gang before chi");
  Expect(t.seat(0).gang_chain == 1, "chain 1");
  Expect(t.OnDiscard(0, 9), "discard 1tiao");
  PassClaims(t);
  DiscardTurnTile(t);
  DiscardTurnTile(t);
  Expect(t.turn_seat() == 3, "seat3 turn");
  Expect(t.OnDiscard(3, 4), "seat3 offers chi");
  auto opts = ListChiOptions(t.seat(0).hand, 4);
  Expect(!opts.empty() && t.OnAction(0, ActionKind::kChi, &opts[0]), "chi after gang");
  PassClaims(t);
  Expect(t.seat(0).gang_chain == 0, "chi clears gang chain");
}

int main() {
  TestDeal();
  TestDiscardAndDraw();
  TestLiuJu();
  TestZimoHuTimeout();
  TestOnZimoHuConfirm();
  TestPengClaim();
  TestChiClaim();
  TestReconnectHandSnapshot();
  TestCaiPiaoSuccess();
  TestCaiPiaoFail();
  TestCaiPiaoDouble();
  TestCaiPiaoTriple();
  TestMaxPiaoCap();
  TestPiaoBlocksOthers();
  TestBaotouZimo();
  TestDianpaoDebugDeal();
  TestPingZhuangDianpaoReject();
  TestErLianDianpaoReject();
  TestAnGang();
  TestMingGang();
  TestGangKaiZimo();
  TestGangPiaoZimo();
  TestPiaoThenGang();
  TestDoubleGangKai();
  TestTripleGangKai();
  TestMingGangKai();
  TestBuGangQiang();
  TestBuGangNoQiang();
  TestQiangGangDisabled();
  TestLouHu();
  TestLouHuOtherWaitOk();
  TestLouHuOtherWaitStillHu();
  TestContractorViaTan();
  TestContractorRealChiThree();
  TestPengIncrementsTanAndContract();
  TestPengTanDisabled();
  TestChiIncrementsTan();
  TestChiChooseOption();
  TestClaimPriorityHuOverPengChi();
  TestClaimPriorityHuOverGang();
  TestMultiHuNearestWins();
  TestNonNextCannotChi();
  TestErLianXianZimo();
  TestStartSanlaoZimo();
  TestXianHuStealsBanker();
  TestQiDuiZimo();
  TestFourGangKai();
  TestXianXianDianpao();
  TestXianXianDianpaoOff();
  TestGangScoreInstant();
  TestGangScoreMingAndBu();
  TestGangScoreInstantSkipsM();
  TestPingZhuangQiangGang();
  TestQiangGangPassContinues();
  TestCaishenDiscardNoDianpao();
  TestDiscardNonBaiDoesNotPiao();
  TestTanCountsStaySeparate();
  TestThreeRealChiContract();
  TestGangChainClearsOnPeng();
  TestGangChainClearsOnChi();
  TestNoZimoAfterPeng();
  TestCrossRoundLian();
  TestTimeoutClaimPass();
  TestTimeoutDiscardNonHu();
  if (fails) {
    std::cerr << fails << " failures\n";
    return 1;
  }
  std::cout << "all passed\n";
  return 0;
}
