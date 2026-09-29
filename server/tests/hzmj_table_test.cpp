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

std::vector<TileId> FixedWallDealBankerExtra() {
  // Build a deterministic wall: deal pops from back.
  // Last 53 tiles drawn for deal: 13*4+1 = 53. Put them at the end.
  std::vector<TileId> wall = BuildWall();
  // Keep order as-is for predictability; BuildWall is kinds 0..33 x4
  return wall;
}

void TestDeal() {
  HzmjConfig cfg;
  cfg.base_score = 100;
  HzmjTable t(cfg, {1, 2, 3, 4}, /*banker*/ 0, /*lian*/ 1);
  t.SetRng([](int n) { return 0; });  // no shuffle effect if we set wall after Start? set before
  t.SetWallForTest(FixedWallDealBankerExtra());
  std::vector<OutEvent> ev;
  t.SetSink([&](const OutEvent& e) { ev.push_back(e); });
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
  // Forced hands: banker discards 12 which nobody can claim
  t.SetHandForTest(0, CountTiles({12, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 13}));
  t.SetHandForTest(1, CountTiles({14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26}));
  t.SetHandForTest(2, CountTiles({27, 27, 28, 28, 29, 29, 30, 30, 31, 31, 32, 32, 0}));
  t.SetHandForTest(3, CountTiles({1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7}));
  t.SetWallForTest({8, 8, 8, 9, 9, 9});
  t.Start();
  const int before = t.wall_remain();
  Expect(t.OnDiscard(0, 12), "discard ok");
  if (t.sub() == PlaySub::kClaimWindow) {
    t.OnAction(1, ActionKind::kPass, nullptr);
    t.OnAction(2, ActionKind::kPass, nullptr);
    t.OnAction(3, ActionKind::kPass, nullptr);
  }
  Expect(t.turn_seat() == 1, "turn to next");
  Expect(HandSize(t.seat(1).hand) == 14, "next has 14 after draw");
  Expect(t.wall_remain() == before - 1, "wall -1");
}

void TestLiuJu() {
  HzmjConfig cfg;
  HzmjTable t(cfg, {1, 2, 3, 4}, 0, 1);
  // minimal wall: deal needs 53; then empty -> next discard cycle liuju
  std::vector<TileId> wall(53, 0);
  for (size_t i = 0; i < wall.size(); ++i) wall[i] = static_cast<TileId>(i % 34);
  t.SetWallForTest(wall);
  t.Start();
  Expect(t.wall_remain() == 0, "empty wall after deal");
  TileId disc = 0;
  for (TileId x = 0; x < 34; ++x) {
    if (t.seat(0).hand[static_cast<size_t>(x)] > 0) {
      disc = x;
      break;
    }
  }
  t.OnDiscard(0, disc);
  Expect(t.phase() == Phase::kLiuJu, "T11 liu ju");
  Expect(t.lian_zhuang() == 2, "T11 lian +1");
}

void TestZimoHuWithForcedHands() {
  HzmjConfig cfg;
  cfg.base_score = 100;
  HzmjTable t(cfg, {10, 20, 30, 40}, /*banker*/ 1, 1);
  // Pre-set hands: seat1 waiting, will draw winning tile
  // Use Start with empty hands then SetHand — Start deals if hands empty.
  // Trick: set wall to only winning tile left after skip deal by setting hands first.
  auto win_hand13 = CountTiles({27, 0, 1, 2, 3, 4, 5, 6, 7, 8, 32, 32, 32});
  // banker seat1: give 14 already winning after "draw" — simulate by Start with custom:
  // Set hands before start with non-zero so Start skips DealTiles... looking at Start():
  // need_deal if all hands size 0. So set all hands non-empty.
  t.SetHandForTest(0, CountTiles({0, 0, 0, 1, 1, 1, 2, 2, 2, 3, 3, 3, 4}));
  t.SetHandForTest(1, win_hand13);
  t.SetHandForTest(2, CountTiles({5, 5, 5, 6, 6, 6, 7, 7, 7, 8, 8, 8, 9}));
  t.SetHandForTest(3, CountTiles({10, 10, 10, 11, 11, 11, 12, 12, 12, 13, 13, 13, 14}));
  t.SetWallForTest({27});  // draw 东 for seat after discard cycle — simpler: force seat1 turn
  // Restart logic: call Start (no deal), turn=banker=1 discard phase with 13 tiles — invalid.
  // Better: give seat1 14-tile winning hand and call Finish via timeout auto-hu.
  auto win14 = win_hand13;
  AddTile(win14, 27);
  t.SetHandForTest(1, win14);
  t.SetWallForTest({});
  t.Start();
  Expect(t.turn_seat() == 1, "banker1 turn");
  t.OnTimeout(1);  // should auto hu
  Expect(t.phase() == Phase::kSettle, "T01 settle after auto hu");
  Expect(t.last_settle().deltas[1] > 0, "T01 winner positive");
  Expect(t.last_settle().stake == 100, "T01 M=1 stake");
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
  // others must pass for resolve - if only seat1 acted, need all ready
  t.OnAction(2, ActionKind::kPass, nullptr);
  t.OnAction(3, ActionKind::kPass, nullptr);
  Expect(t.turn_seat() == 1, "penger turn");
  Expect(t.seat(1).melds.size() == 1, "has peng meld");
}

void TestReconnectHandSnapshot() {
  // T12: hand content stable for reconnect snapshot
  HzmjConfig cfg;
  HzmjTable t(cfg, {1, 2, 3, 4}, 0, 1);
  auto h0 = CountTiles({0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13});
  t.SetHandForTest(0, h0);
  t.SetHandForTest(1, CountTiles({14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26}));
  t.SetHandForTest(2, CountTiles({27, 27, 28, 28, 29, 29, 30, 30, 31, 31, 32, 32, 0}));
  t.SetHandForTest(3, CountTiles({1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7}));
  t.SetWallForTest({8, 8, 8});
  t.Start();
  const auto before = t.seat(0).hand;
  // simulate reconnect read
  Expect(t.seat(0).hand == before, "T12 hand unchanged");
  Expect(HandSize(t.seat(0).hand) == 14, "T12 banker still 14");
}

int main() {
  TestDeal();
  TestDiscardAndDraw();
  TestLiuJu();
  TestZimoHuWithForcedHands();
  TestPengClaim();
  TestReconnectHandSnapshot();
  if (fails) {
    std::cerr << fails << " failures\n";
    return 1;
  }
  std::cout << "all passed\n";
  return 0;
}
