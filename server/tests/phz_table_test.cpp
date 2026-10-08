#include "game/phz/table.hpp"

#include <iostream>
#include <vector>

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

void PassClaims(PhzTable& t) {
  if (t.sub() != PlaySub::kClaimWindow) return;
  for (int s = 0; s < kSeats; ++s) {
    if (t.SeatNeedsClaimInput(s)) t.OnAction(s, ActionKind::kPass, nullptr);
  }
}

int OwnedTiles(const SeatState& s) {
  int n = HandSize(s.hand);
  for (const auto& m : s.melds) n += static_cast<int>(m.tiles.size());
  return n;
}

void TestDealT01() {
  PhzConfig cfg;
  PhzTable t(cfg, {1, 2, 3}, 0);
  t.SetRng([](int n) { return 0; });
  t.SetWallForTest(BuildWall());
  t.Start();
  Expect(t.phase() == Phase::kPlay, "phase play");
  // Deal-time ti moves 4 tiles into melds; owned total stays 21/20.
  Expect(OwnedTiles(t.seat(0)) == 21, "T01 banker 21");
  Expect(OwnedTiles(t.seat(1)) == 20, "T01 xian 20");
  Expect(OwnedTiles(t.seat(2)) == 20, "T01 xian2 20");
  Expect(t.wall_remain() == 19, "T01 wall 19");
  Expect(t.turn_seat() == 0 && t.sub() == PlaySub::kDiscard, "banker discard");
}

void TestRevealT02() {
  PhzConfig cfg;
  cfg.force_wei = true;
  PhzTable t(cfg, {1, 2, 3}, 0);
  // Hands with no pairs of upcoming draw tile 5
  t.SetHandForTest(0, CountTiles({0, 1, 2, 3, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 0, 1}));
  t.SetHandForTest(1, CountTiles({2, 3, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 0, 1, 2}));
  t.SetHandForTest(2, CountTiles({3, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 0, 1, 2, 3}));
  t.SetWallForTest({7, 8, 5});  // pop_back draws 5 first for seat1
  std::vector<std::string> evs;
  t.SetSink([&](const OutEvent& e) { evs.push_back(e.type); });
  t.Start();
  Expect(t.OnDiscard(0, 19), "discard");
  PassClaims(t);
  bool saw_reveal = false;
  for (const auto& e : evs)
    if (e == "Reveal") saw_reveal = true;
  Expect(saw_reveal, "T02 reveal event");
  Expect(t.seat(1).hand[5] == 0, "T02 tile not in hand");
}

void TestWeiT03() {
  PhzConfig cfg;
  PhzTable t(cfg, {1, 2, 3}, 0);
  HandCount h0 = CountTiles({0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 0});
  t.SetHandForTest(0, h0);
  HandCount h1 = ZeroHand();
  AddTile(h1, 5, 2);
  for (TileId x = 0; x < 18; ++x) AddTile(h1, x == 5 ? 0 : x, 1);
  // ensure size 20 with exactly 2 of tile 5
  h1 = ZeroHand();
  AddTile(h1, 5, 2);
  for (int i = 0; i < 18; ++i) AddTile(h1, static_cast<TileId>(i == 5 ? 6 : i), 1);
  Expect(HandSize(h1) == 20, "h1 size");
  t.SetHandForTest(1, h1);
  t.SetHandForTest(2, CountTiles({0, 1, 2, 3, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 0}));
  t.SetWallForTest({7, 8, 5});
  t.Start();
  Expect(t.OnDiscard(0, 19), "disc");
  PassClaims(t);
  Expect(!t.seat(1).melds.empty() && t.seat(1).melds[0].kind == MeldKind::kWei, "T03 force wei");
  Expect(t.sub() == PlaySub::kDiscard && t.turn_seat() == 1, "T03 must discard");
}

void TestPaoT04() {
  PhzConfig cfg;
  PhzTable t(cfg, {1, 2, 3}, 0);
  t.SetHandForTest(0, CountTiles({5, 1, 2, 3, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 0, 1}));
  t.SetHandForTest(1, CountTiles({1, 2, 3, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 0, 1}));
  t.SetHandForTest(2, CountTiles({2, 3, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 0, 1, 2}));
  t.SetMeldsForTest(1, {{MeldKind::kPeng, 5, {5, 5, 5}, 0}});
  t.SetWallForTest({7, 8, 9});
  t.Start();
  Expect(t.OnDiscard(0, 5), "disc 5");
  Expect(!t.seat(1).melds.empty() && t.seat(1).melds[0].kind == MeldKind::kPao, "T04 force pao");
}

void TestTiT05() {
  PhzConfig cfg;
  PhzTable t(cfg, {1, 2, 3}, 0);
  HandCount h1 = ZeroHand();
  AddTile(h1, 5, 3);
  for (int i = 0; i < 17; ++i) AddTile(h1, static_cast<TileId>(i == 5 ? 6 : i), 1);
  t.SetHandForTest(0, CountTiles({0, 1, 2, 3, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 0, 1}));
  t.SetHandForTest(1, h1);
  t.SetHandForTest(2, CountTiles({0, 1, 2, 3, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 0}));
  t.SetWallForTest({7, 8, 5});
  t.Start();
  Expect(t.OnDiscard(0, 19), "disc");
  PassClaims(t);
  bool ti = false;
  for (const auto& m : t.seat(1).melds)
    if (m.kind == MeldKind::kTi) ti = true;
  Expect(ti, "T05 force ti");
}

void TestPaoDiscardOnceT10() {
  PhzConfig cfg;
  cfg.first_pao_ti_discard = true;
  PhzTable t(cfg, {1, 2, 3}, 0);
  t.SetHandForTest(0, CountTiles({5, 1, 2, 3, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 0, 1}));
  t.SetHandForTest(1, CountTiles({1, 2, 3, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 0, 1}));
  t.SetHandForTest(2, CountTiles({2, 3, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 0, 1, 2}));
  t.SetMeldsForTest(1, {{MeldKind::kPeng, 5, {5, 5, 5}, 0}});
  t.SetWallForTest({7, 8, 9, 10});
  t.Start();
  Expect(t.OnDiscard(0, 5), "pao");
  Expect(t.seat(1).pao_ti_count == 1, "pao count 1");
  Expect(t.sub() == PlaySub::kDiscard && t.turn_seat() == 1, "T10 must discard after first pao");
}

void TestLiuJuT11() {
  PhzConfig cfg;
  PhzTable t(cfg, {1, 2, 3}, 0);
  t.SetHandForTest(0, CountTiles({0, 1, 2, 3, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 0, 1}));
  t.SetHandForTest(1, CountTiles({2, 3, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 0, 1, 2}));
  t.SetHandForTest(2, CountTiles({3, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 0, 1, 2, 3}));
  t.SetWallForTest({});
  t.Start();
  Expect(t.OnDiscard(0, 19), "disc");
  PassClaims(t);
  Expect(t.phase() == Phase::kLiuJu, "T11 liu ju");
}

void TestTimeout() {
  PhzConfig cfg;
  PhzTable t(cfg, {1, 2, 3}, 0);
  t.SetHandForTest(0, CountTiles({0, 1, 2, 3, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 0, 1}));
  t.SetHandForTest(1, CountTiles({2, 3, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 0, 1, 2}));
  t.SetHandForTest(2, CountTiles({3, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 0, 1, 2, 3}));
  t.SetWallForTest({5, 5, 5});
  t.Start();
  t.OnTimeout(0);
  Expect(t.sub() == PlaySub::kClaimWindow || t.phase() == Phase::kPlay || t.phase() == Phase::kLiuJu, "timeout acted");
}

void TestDebugQuickHu() {
  PhzConfig cfg;
  PhzTable t(cfg, {1, 2, 3}, 0);
  t.ApplyDebugQuickHuDeal();
  Expect(HandSize(t.seat(0).hand) == 21, "debug banker 21");
  Expect(HandSize(t.seat(1).hand) == 20, "debug seat1 20");
  Expect(t.seat(0).hand[19] >= 1, "debug banker has Shi");
  Expect(t.seat(1).hand[0] == 2, "debug seat1 pair Yi");
  t.Start();
  Expect(t.OnDiscard(0, 19), "debug discard Shi");
  PassClaims(t);
  Expect(t.phase() == Phase::kSettle, "debug seat1 zimo settle");
  Expect(t.banker_seat() == 1, "debug winner banker");
}

int main() {
  TestDealT01();
  TestRevealT02();
  TestWeiT03();
  TestPaoT04();
  TestTiT05();
  TestPaoDiscardOnceT10();
  TestLiuJuT11();
  TestTimeout();
  TestDebugQuickHu();
  if (fails) {
    std::cerr << fails << " failed\n";
    return 1;
  }
  std::cout << "all ok\n";
  return 0;
}
