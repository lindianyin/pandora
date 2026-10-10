#include "game/biji/table.hpp"

#include "common/errors.hpp"

#include <iostream>
#include <set>

using namespace pandora::biji;
using pandora::Err;

static int fails = 0;
void Expect(bool cond, const char* msg) {
  if (!cond) {
    std::cerr << "FAIL: " << msg << "\n";
    ++fails;
  } else {
    std::cout << "OK: " << msg << "\n";
  }
}

uint32_t SeqRng(uint32_t& s) {
  s = s * 1664525u + 1013904223u;
  return s;
}

BijiTable MakeTable(int n, uint32_t seed) {
  BijiConfig cfg = DefaultConfig(n);
  cfg.base_score = 100;
  cfg.rake_bp = 0;
  cfg.arrange_timeout_s = 45;
  auto rng = [s = seed]() mutable { return SeqRng(s); };
  BijiTable t(cfg, rng);
  for (int i = 0; i < n; ++i) t.SetSeatUid(i, 100 + i);
  return t;
}

void ReadyAll(BijiTable& t, int n) {
  for (int i = 0; i < n; ++i) t.OnReady(i, true);
}

Arrange LegalFromHand(const Hand9& h) { return AutoArrange(h); }

void TestT30_Deal() {
  auto t = MakeTable(4, 1);
  ReadyAll(t, 4);
  Expect(t.TryStartDeal(1000), "T30 start");
  Expect(t.phase() == Phase::kArrange, "T30 arrange");
  std::set<CardId> all;
  for (int i = 0; i < 4; ++i) {
    for (auto c : t.seat(i).hand) all.insert(c);
  }
  Expect(all.size() == 36, "T30 36 unique");
}

void TestT31_ConfirmLock() {
  auto t = MakeTable(4, 2);
  ReadyAll(t, 4);
  t.TryStartDeal(0);
  const Arrange a = LegalFromHand(t.seat(0).hand);
  Expect(t.SetArrange(0, a.head, a.mid, a.tail, true) == 0, "T31 ok");
  Expect(t.seat(0).locked, "T31 locked");
  Expect(!t.seat(1).locked, "T31 others unlocked");
}

void TestT32_DaoShui() {
  auto t = MakeTable(4, 3);
  ReadyAll(t, 4);
  t.TryStartDeal(0);
  const auto& h = t.seat(0).hand;
  // Force dao shui: put strongest three as head if possible via Auto then swap head/tail
  Arrange a = LegalFromHand(h);
  std::swap(a.head, a.tail);
  const int err = t.SetArrange(0, a.head, a.mid, a.tail, true);
  Expect(err == static_cast<int>(Err::kBijiDaoShui) || err == static_cast<int>(Err::kBijiIllegalArrange),
         "T32 reject");
  Expect(!t.seat(0).locked, "T32 not locked");
}

void TestT33_LockedReject() {
  auto t = MakeTable(4, 4);
  ReadyAll(t, 4);
  t.TryStartDeal(0);
  Arrange a = LegalFromHand(t.seat(0).hand);
  Expect(t.SetArrange(0, a.head, a.mid, a.tail, true) == 0, "T33 lock");
  Expect(t.SetArrange(0, a.head, a.mid, a.tail, true) == static_cast<int>(Err::kBijiAlreadyLocked),
         "T33 reject");
}

void TestT34_Deadline() {
  auto t = MakeTable(4, 5);
  ReadyAll(t, 4);
  t.TryStartDeal(0);
  t.OnArrangeDeadline(99999);
  Expect(t.phase() == Phase::kWaitReady, "T34 back wait");
  Expect(!t.plan().seats.empty(), "T34 plan");
  Expect(t.plan().seats.size() == 4, "T34 4 seats");
}

void TestT35_DisconnectNoImmediate() {
  auto t = MakeTable(4, 6);
  ReadyAll(t, 4);
  t.TryStartDeal(0);
  t.OnDisconnect(1);
  Expect(t.seat(1).trusteeship, "T35 trust");
  Expect(!t.seat(1).locked, "T35 not locked yet");
  Expect(t.phase() == Phase::kArrange, "T35 still arrange");
}

void TestT36_DealStartRotate() {
  auto t = MakeTable(4, 7);
  ReadyAll(t, 4);
  t.TryStartDeal(0);
  // Lock all with auto via deadline
  t.OnArrangeDeadline(1);
  const int winner = t.last_tail_winner();
  Expect(winner >= 0 && winner < 4, "T36 winner");
  ReadyAll(t, 4);
  t.TryStartDeal(100);
  Expect(t.deal_start() == (winner + 1) % 4, "T36 deal_start");
}

void TestT37_Snapshot() {
  auto t = MakeTable(4, 8);
  ReadyAll(t, 4);
  t.TryStartDeal(1000);
  Arrange a = LegalFromHand(t.seat(0).hand);
  t.SetArrange(0, a.head, a.mid, a.tail, false);
  const Snapshot sn = t.BuildSnapshot(0, 1000);
  Expect(sn.phase == Phase::kArrange, "T37 phase");
  Expect(sn.has_draft, "T37 draft");
  Expect(sn.remain_ms > 0, "T37 remain");
  Expect(sn.others_locked.size() == 4, "T37 others");
}

int main() {
  TestT30_Deal();
  TestT31_ConfirmLock();
  TestT32_DaoShui();
  TestT33_LockedReject();
  TestT34_Deadline();
  TestT35_DisconnectNoImmediate();
  TestT36_DealStartRotate();
  TestT37_Snapshot();
  if (fails) {
    std::cerr << fails << " failed\n";
    return 1;
  }
  std::cout << "all biji_table tests passed\n";
  return 0;
}
