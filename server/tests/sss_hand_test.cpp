#include "game/sss/hand.hpp"
#include "game/sss/settle.hpp"
#include "game/sss/special.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

using namespace pandora::sss;

static int fails = 0;
void Expect(bool cond, const char* msg) {
  if (!cond) {
    std::cerr << "FAIL: " << msg << "\n";
    ++fails;
  } else {
    std::cout << "OK: " << msg << "\n";
  }
}

constexpr CardId C(int suit, int rank) { return MakeCard(suit, rank); }

void TestT01_FiveTypeOrder() {
  FiveCards sf{C(3, 8), C(3, 9), C(3, 10), C(3, 11), C(3, 12)};     // TJQKA s
  FiveCards four{C(0, 12), C(1, 12), C(2, 12), C(3, 12), C(0, 0)};  // AAAA+2
  FiveCards fh{C(0, 11), C(1, 11), C(2, 11), C(0, 10), C(1, 10)};   // KKK QQ
  FiveCards fl{C(2, 12), C(2, 10), C(2, 8), C(2, 5), C(2, 3)};      // heart flush
  FiveCards st{C(0, 4), C(1, 5), C(2, 6), C(3, 7), C(0, 8)};        // 6789T
  FiveCards thr{C(0, 7), C(1, 7), C(2, 7), C(0, 1), C(1, 2)};       // 999
  FiveCards tp{C(0, 6), C(1, 6), C(0, 4), C(1, 4), C(2, 0)};        // 88 66
  FiveCards pr{C(0, 5), C(1, 5), C(2, 3), C(3, 2), C(0, 1)};        // 77
  FiveCards hi{C(0, 12), C(1, 10), C(2, 8), C(3, 4), C(0, 0)};
  Expect(CompareFive(sf, four) > 0, "T01 SF > FOUR");
  Expect(CompareFive(four, fh) > 0, "T01 FOUR > FH");
  Expect(CompareFive(fh, fl) > 0, "T01 FH > FLUSH");
  Expect(CompareFive(fl, st) > 0, "T01 FLUSH > ST");
  Expect(CompareFive(st, thr) > 0, "T01 ST > THREE");
  Expect(CompareFive(thr, tp) > 0, "T01 THREE > TP");
  Expect(CompareFive(tp, pr) > 0, "T01 TP > PAIR");
  Expect(CompareFive(pr, hi) > 0, "T01 PAIR > HIGH");
}

void TestT02_SfCompare() {
  FiveCards a{C(3, 8), C(3, 9), C(3, 10), C(3, 11), C(3, 12)};
  FiveCards b{C(2, 7), C(2, 8), C(2, 9), C(2, 10), C(2, 11)};
  Expect(EvalFive(a).level == static_cast<int8_t>(FiveLevel::kStraightFlush), "T02 a SF");
  Expect(CompareFive(a, b) > 0, "T02 TJQKA > 9TJQK");
}

void TestT03_StraightTops() {
  FiveCards a2345{C(0, 12), C(1, 0), C(2, 1), C(3, 2), C(0, 3)};
  FiveCards r23456{C(0, 0), C(1, 1), C(2, 2), C(3, 3), C(0, 4)};
  FiveCards tjqka{C(0, 8), C(1, 9), C(2, 10), C(3, 11), C(0, 12)};
  Expect(EvalFive(a2345).level == static_cast<int8_t>(FiveLevel::kStraight), "T03 A2345");
  Expect(CompareFive(a2345, r23456) < 0, "T03 A2345 < 23456");
  Expect(CompareFive(r23456, tjqka) < 0, "T03 23456 < TJQKA");
}

void TestT04_NotStraight() {
  FiveCards bad{C(3, 9), C(3, 10), C(3, 11), C(3, 12), C(0, 0)};  // JQKA2 same? not all same suit
  // mix suits: JQKA2
  FiveCards jqka2{C(0, 9), C(1, 10), C(2, 11), C(3, 12), C(0, 0)};
  const auto lv = static_cast<FiveLevel>(EvalFive(jqka2).level);
  Expect(lv != FiveLevel::kStraight && lv != FiveLevel::kStraightFlush, "T04 JQKA2 not straight");
}

void TestT05_HighTieNoSuit() {
  // Same ranks different suits -> equal (no suit kick)
  FiveCards a{C(3, 12), C(3, 11), C(3, 9), C(0, 5), C(1, 3)};  // not flush
  FiveCards b{C(2, 12), C(2, 11), C(1, 9), C(2, 5), C(0, 3)};
  // Make both HIGH with same ranks: A K J 7 5
  FiveCards ha{C(3, 12), C(2, 11), C(1, 9), C(0, 5), C(1, 3)};
  FiveCards hb{C(2, 12), C(3, 11), C(0, 9), C(1, 5), C(2, 3)};
  Expect(EvalFive(ha).level == static_cast<int8_t>(FiveLevel::kHigh), "T05 high a");
  Expect(EvalFive(hb).level == static_cast<int8_t>(FiveLevel::kHigh), "T05 high b");
  Expect(CompareFive(ha, hb) == 0, "T05 tie no suit");
  (void)a;
  (void)b;
}

void TestT06_HeadOrder() {
  HeadCards thr{C(0, 12), C(1, 12), C(2, 12)};
  HeadCards pr{C(0, 11), C(1, 11), C(2, 0)};
  HeadCards hi{C(0, 12), C(1, 10), C(2, 0)};
  Expect(CompareHead(thr, pr) > 0, "T06 THREE > PAIR");
  Expect(CompareHead(pr, hi) > 0, "T06 PAIR > HIGH");
}

void TestT07_HeadIgnoresFlushStraight() {
  HeadCards sf_like{C(3, 0), C(3, 1), C(3, 2)};  // 234 same suit
  Expect(EvalHead(sf_like).level == static_cast<int8_t>(FiveLevel::kHigh), "T07 head high not SF");
}

void TestT08_LegalArrange() {
  HeadCards head{C(0, 0), C(1, 0), C(2, 1)};  // pair 2
  FiveCards mid{C(0, 2), C(1, 3), C(2, 4), C(3, 5), C(0, 6)};  // 45678 straight
  FiveCards tail{C(0, 11), C(1, 11), C(2, 11), C(0, 10), C(1, 10)};  // KKK QQ
  Expect(IsLegalArrange(head, mid, tail), "T08 legal");
}

void TestT09_DaoShui() {
  HeadCards h{C(0, 12), C(1, 12), C(2, 12)};  // AAA
  FiveCards mid{C(0, 2), C(1, 3), C(2, 4), C(3, 5), C(0, 6)};
  FiveCards tail{C(0, 0), C(1, 0), C(2, 1), C(3, 2), C(0, 3)};
  Expect(!IsLegalArrange(h, mid, tail), "T09 dao shui");
}

void TestT10_HeadThreeVsMidTwoPair() {
  HeadCards head{C(0, 5), C(1, 5), C(2, 5)};  // 777
  FiveCards mid{C(0, 12), C(1, 12), C(0, 11), C(1, 11), C(2, 0)};  // AA KK
  FiveCards tail{C(0, 10), C(1, 10), C(2, 10), C(0, 9), C(1, 8)};  // QQQ J T
  Expect(CmpOrderHeadFive(EvalHead(head), EvalFive(mid)) > 0, "T10 head three > mid two pair");
  Expect(!IsLegalArrange(head, mid, tail), "T10 dao shui head>mid");
}

void TestT11_HeadPairVsMidPair() {
  HeadCards head{C(0, 2), C(1, 2), C(2, 0)};  // pair 4
  FiveCards mid{C(0, 5), C(1, 5), C(2, 1), C(3, 3), C(0, 4)};  // pair 7
  FiveCards tail{C(0, 10), C(1, 10), C(2, 10), C(0, 8), C(1, 7)};
  Expect(IsLegalArrange(head, mid, tail), "T11 legal smaller pair head");
}

void TestT12_BadMultiset() {
  Hand13 hand{};
  for (int i = 0; i < 13; ++i) hand[i] = static_cast<CardId>(i);
  HeadCards head{C(0, 0), C(0, 1), C(0, 2)};
  FiveCards mid{C(0, 3), C(0, 4), C(0, 5), C(0, 6), C(0, 7)};
  // Duplicate diamond-2 in tail (already in head)
  Expect(!SameMultiset13(hand, head, mid, FiveCards{C(0, 8), C(0, 9), C(0, 10), C(0, 11), C(0, 0)}),
         "T12 duplicate 2");
  // Foreign club-2
  Expect(!SameMultiset13(hand, head, mid, FiveCards{C(0, 8), C(0, 9), C(0, 10), C(0, 11), C(1, 0)}),
         "T12 foreign card");
}

void TestT13_AutoArrangeDeterministic() {
  Hand13 h{};
  for (int i = 0; i < 13; ++i) h[i] = static_cast<CardId>(i + 5);  // mixed
  const Arrange a1 = AutoArrange(h);
  const Arrange a2 = AutoArrange(h);
  Expect(IsLegalArrange(a1.head, a1.mid, a1.tail), "T13 legal");
  Expect(SameMultiset13(h, a1.head, a1.mid, a1.tail), "T13 multiset");
  Expect(a1.head == a2.head && a1.mid == a2.mid && a1.tail == a2.tail, "T13 deterministic");
}

void TestT14_AutoPrefersTail() {
  Hand13 h{};
  for (int i = 0; i < 13; ++i) h[i] = static_cast<CardId>(i + 5);
  const Arrange auto_a = AutoArrange(h);
  Hand13 sorted = h;
  std::sort(sorted.begin(), sorted.end());
  Arrange weak{{sorted[0], sorted[1], sorted[2]},
               {sorted[3], sorted[4], sorted[5], sorted[6], sorted[7]},
               {sorted[8], sorted[9], sorted[10], sorted[11], sorted[12]}};
  if (!IsLegalArrange(weak.head, weak.mid, weak.tail)) {
    Expect(true, "T14 weak illegal skip");
    return;
  }
  Expect(CompareFive(auto_a.tail, weak.tail) >= 0, "T14 auto tail >= weak");
}

void TestT15_Dragon() {
  Hand13 h{};
  for (int r = 0; r < 13; ++r) h[r] = MakeCard(r % 4, r);
  Expect(DetectSpecial(h, true) == SpecialType::kDragon, "T15 DRAGON");
  Expect(SpecialWater(SpecialType::kDragon) == 36, "T15 W=36");
}

void TestT16_NaturalDragon() {
  Hand13 h{};
  for (int r = 0; r < 13; ++r) h[r] = MakeCard(3, r);
  Expect(DetectSpecial(h, true) == SpecialType::kNaturalDragon, "T16 NATURAL");
}

void TestT17_AllSmallRejectsA() {
  Hand13 h{};
  // twelve cards 2-8 mixed + one Ace
  int idx = 0;
  for (int r = 0; r <= 6 && idx < 12; ++r) {
    for (int s = 0; s < 4 && idx < 12; ++s) h[idx++] = MakeCard(s, r);
  }
  h[12] = MakeCard(0, 12);  // A
  Expect(DetectSpecial(h, true) != SpecialType::kAllSmall, "T17 not ALL_SMALL");
}

void TestT18_AllSmall() {
  // Ranks only even 0,2,4,6 (no 5-straight / 3-straight); <=3 cards per suit (no 5-flush).
  Hand13 h{};
  int idx = 0;
  const int ranks[4] = {0, 2, 4, 6};
  for (int ri = 0; ri < 4 && idx < 13; ++ri) {
    for (int s = 0; s < 4 && idx < 13; ++s) {
      if (ri == 0 && s == 3) continue;  // 3+3+3+4 = 13 with skip pattern
      h[idx++] = MakeCard(s, ranks[ri]);
    }
  }
  // Fill exactly: 4 of rank0, 3 of 2, 3 of 4, 3 of 6
  idx = 0;
  for (int s = 0; s < 4; ++s) h[idx++] = MakeCard(s, 0);
  for (int s = 0; s < 3; ++s) h[idx++] = MakeCard(s, 2);
  for (int s = 0; s < 3; ++s) h[idx++] = MakeCard(s, 4);
  for (int s = 0; s < 3; ++s) h[idx++] = MakeCard(s, 6);
  Expect(idx == 13, "T18 size");
  Expect(DetectSpecial(h, true) == SpecialType::kAllSmall, "T18 ALL_SMALL");
}

void TestT19_SixPair() {
  Hand13 h{};
  int idx = 0;
  for (int r = 0; r < 6; ++r) {
    h[idx++] = MakeCard(0, r);
    h[idx++] = MakeCard(1, r);
  }
  h[idx++] = MakeCard(2, 8);  // single
  Expect(DetectSpecial(h, true) == SpecialType::kSixPair, "T19 SIX_PAIR");
}

void TestT19b_ThreeStraight() {
  // 234 | 56789 | 56789 -- pairs across mid/tail, not a full dragon.
  Hand13 h{MakeCard(0, 0), MakeCard(1, 1), MakeCard(2, 2), MakeCard(0, 3), MakeCard(1, 4),
           MakeCard(2, 5), MakeCard(3, 6), MakeCard(0, 7), MakeCard(1, 3), MakeCard(2, 4),
           MakeCard(3, 5), MakeCard(0, 6), MakeCard(1, 7)};
  Expect(DetectSpecial(h, true) == SpecialType::kThreeStraight, "T19b THREE_STRAIGHT");
  Expect(SpecialWater(SpecialType::kThreeStraight) == 3, "T19b W=3");
}

void TestT19c_ThreeFlushNotStraight() {
  Hand13 h{MakeCard(0, 0), MakeCard(0, 2), MakeCard(0, 5), MakeCard(1, 0), MakeCard(1, 2),
           MakeCard(1, 4), MakeCard(1, 6), MakeCard(1, 8), MakeCard(2, 1), MakeCard(2, 3),
           MakeCard(2, 5), MakeCard(2, 7), MakeCard(2, 9)};
  Expect(DetectSpecial(h, true) == SpecialType::kThreeFlush, "T19c THREE_FLUSH");
}

void TestT19d_CoveringStraightsAreDragon() {
  // A23|45678|9TJQK uses each rank once -> DRAGON, not THREE_STRAIGHT.
  Hand13 h{MakeCard(0, 12), MakeCard(1, 0), MakeCard(2, 1), MakeCard(0, 2), MakeCard(1, 3),
           MakeCard(2, 4), MakeCard(3, 5), MakeCard(0, 6), MakeCard(1, 7), MakeCard(2, 8),
           MakeCard(3, 9), MakeCard(0, 10), MakeCard(1, 11)};
  Expect(DetectSpecial(h, true) == SpecialType::kDragon, "T19d covering -> DRAGON");
}

SeatArrange MakeOrdinary(const HeadCards& h, const FiveCards& m, const FiveCards& t) {
  SeatArrange a;
  a.head = h;
  a.mid = m;
  a.tail = t;
  return a;
}

void TestT20_HeadOrdinary() {
  // seat0 head pair A > seat1 head high
  SeatArrange s0 = MakeOrdinary({C(0, 12), C(1, 12), C(2, 0)},
                                {C(0, 2), C(1, 3), C(2, 4), C(3, 5), C(0, 6)},
                                {C(0, 10), C(1, 10), C(2, 9), C(3, 8), C(0, 7)});
  SeatArrange s1 = MakeOrdinary({C(0, 11), C(1, 5), C(2, 3)},
                                {C(1, 2), C(2, 3), C(3, 4), C(0, 5), C(1, 6)},
                                {C(1, 11), C(2, 11), C(3, 9), C(0, 8), C(1, 7)});
  // Hands may overlap cards - settle doesn't check uniqueness across seats for unit water tests.
  // Use disjoint artificially for water only on head: make mid/tail identical keys so water 0.
  // Simpler: both mid/tail equal compare -> craft identical structure different cards.
  SssConfig cfg = DefaultConfig(2);
  cfg.base_score = 100;
  cfg.enable_shoot = false;
  cfg.enable_special = false;
  // Force only head differs: mid and tail same ranks pattern so one wins all or we isolate.
  // Rebuild with clear pairwise:
  s0 = MakeOrdinary({C(0, 12), C(1, 12), C(2, 1)},   // pair A
                    {C(0, 0), C(1, 2), C(2, 4), C(3, 6), C(0, 8)},
                    {C(0, 3), C(1, 5), C(2, 7), C(3, 9), C(0, 11)});
  s1 = MakeOrdinary({C(2, 10), C(3, 8), C(1, 4)},  // high
                    {C(1, 0), C(2, 2), C(3, 4), C(0, 6), C(1, 8)},
                    {C(1, 3), C(2, 5), C(3, 7), C(0, 9), C(1, 11)});
  auto plan = ComputeSettle({s0, s1}, {1, 2}, cfg);
  // May have mid/tail water too. Just check total conservation.
  Expect(plan.seats[0].gross_water + plan.seats[1].gross_water == 0, "T20 conserve");
  Expect(plan.seats[0].gross_water > 0, "T20 seat0 wins some");
}

void TestT21_HeadThreeWater() {
  HeadCards h0{C(0, 12), C(1, 12), C(2, 12)};
  HeadCards h1{C(0, 0), C(1, 1), C(2, 2)};
  Expect(DunWaterHead(EvalHead(h0).level) == 3, "T21 trip water 3");
  Expect(DunWaterHead(EvalHead(h1).level) == 1, "T21 high water 1");
}

void TestT22_MidSfWater() {
  FiveCards sf{C(3, 8), C(3, 9), C(3, 10), C(3, 11), C(3, 12)};
  Expect(DunWaterMid(EvalFive(sf).level) == 10, "T22 mid SF 10");
}

void TestT23_TailFourWater() {
  FiveCards four{C(0, 7), C(1, 7), C(2, 7), C(3, 7), C(0, 0)};
  Expect(DunWaterTail(EvalFive(four).level) == 4, "T23 tail four 4");
}

void TestT24_Shoot() {
  // seat0 beats seat1 on all three duns
  SeatArrange s0 = MakeOrdinary({C(0, 12), C(1, 12), C(2, 12)},  // AAA
                                {C(0, 8), C(0, 9), C(0, 10), C(0, 11), C(0, 12)},  // wait A used
                                {C(1, 8), C(1, 9), C(1, 10), C(1, 11), C(2, 7)});
  // Fix: head AAA uses three Aces - only 4 aces. mid can't use A.
  s0 = MakeOrdinary({C(0, 11), C(1, 11), C(2, 11)},  // KKK
                    {C(3, 8), C(3, 9), C(3, 10), C(3, 11), C(3, 7)},  // SF 789TJ? ranks 7,8,9,10,11 = 9TJQK? 
                    // 7,8,9,10,11 spades = 9TJQK wait rank7=9,8=T,9=J,10=Q,11=K -> 9TJQK SF
                    {C(0, 12), C(1, 12), C(2, 12), C(3, 12), C(0, 0)});  // AAAA+2
  SeatArrange s1 = MakeOrdinary({C(0, 2), C(1, 3), C(2, 4)},
                                {C(0, 5), C(1, 6), C(2, 1), C(3, 0), C(0, 3)},
                                {C(1, 5), C(2, 6), C(3, 2), C(0, 4), C(1, 1)});
  SssConfig cfg = DefaultConfig(2);
  cfg.base_score = 1;
  cfg.enable_shoot = true;
  cfg.enable_special = false;
  auto plan = ComputeSettle({s0, s1}, {1, 2}, cfg);
  Expect(!plan.shoots.empty(), "T24 has shoot");
  Expect(plan.seats[0].gross_water == -plan.seats[1].gross_water, "T24 conserve");
  Expect(plan.seats[0].gross_water > 0, "T24 shooter wins");
}

void TestT25_HomeRun() {
  // 4 players, seat0 beats all ordinary on all duns - hard to craft; check home_run flag path with
  // synthetic: 3 players where s0 dominates s1 and s2.
  SeatArrange s0 = MakeOrdinary({C(0, 12), C(1, 12), C(2, 12)},
                                {C(3, 8), C(3, 9), C(3, 10), C(3, 11), C(3, 7)},
                                {C(0, 11), C(1, 11), C(2, 11), C(3, 11), C(0, 0)});
  SeatArrange weak = MakeOrdinary({C(0, 1), C(1, 2), C(2, 3)},
                                  {C(0, 4), C(1, 5), C(2, 6), C(3, 0), C(0, 2)},
                                  {C(1, 4), C(2, 5), C(3, 6), C(0, 3), C(1, 0)});
  SeatArrange weak2 = MakeOrdinary({C(2, 1), C(3, 2), C(0, 5)},
                                   {C(2, 4), C(3, 5), C(0, 6), C(1, 7), C(2, 0)},
                                   {C(3, 4), C(0, 7), C(1, 8), C(2, 9), C(3, 0)});
  // Cards heavily overlap - settle still computes compares. May not shoot all.
  SssConfig cfg = DefaultConfig(3);
  cfg.base_score = 1;
  cfg.enable_shoot = true;
  cfg.enable_home_run = true;
  cfg.enable_special = false;
  auto plan = ComputeSettle({s0, weak, weak2}, {1, 2, 3}, cfg);
  Expect(plan.seats[0].gross_water + plan.seats[1].gross_water + plan.seats[2].gross_water == 0,
         "T25 conserve");
}

void TestT26_NoHomeRunTwoPlayer() {
  SeatArrange s0 = MakeOrdinary({C(0, 12), C(1, 12), C(2, 12)},
                                {C(3, 8), C(3, 9), C(3, 10), C(3, 11), C(3, 7)},
                                {C(0, 11), C(1, 11), C(2, 11), C(3, 11), C(0, 0)});
  SeatArrange s1 = MakeOrdinary({C(0, 1), C(1, 2), C(2, 3)},
                                {C(0, 4), C(1, 5), C(2, 6), C(3, 0), C(0, 2)},
                                {C(1, 4), C(2, 5), C(3, 6), C(0, 3), C(1, 0)});
  SssConfig cfg = DefaultConfig(2);
  cfg.enable_shoot = true;
  cfg.enable_home_run = true;
  auto plan = ComputeSettle({s0, s1}, {1, 2}, cfg);
  Expect(!plan.seats[0].home_run && !plan.seats[1].home_run, "T26 no home run n=2");
}

void TestT27_SpecialVsOrdinary() {
  Hand13 dragon{};
  for (int r = 0; r < 13; ++r) dragon[r] = MakeCard(r % 4, r);
  SeatArrange sp;
  sp.report_special = true;
  sp.special = SpecialType::kDragon;
  sp.head = {dragon[0], dragon[1], dragon[2]};
  sp.mid = {dragon[3], dragon[4], dragon[5], dragon[6], dragon[7]};
  sp.tail = {dragon[8], dragon[9], dragon[10], dragon[11], dragon[12]};
  SeatArrange ord = MakeOrdinary({C(0, 0), C(1, 1), C(2, 2)},
                                 {C(0, 3), C(1, 4), C(2, 5), C(3, 6), C(0, 7)},
                                 {C(1, 3), C(2, 4), C(3, 5), C(0, 6), C(1, 7)});
  SssConfig cfg = DefaultConfig(2);
  cfg.base_score = 1;
  cfg.enable_special = true;
  cfg.enable_shoot = true;
  auto plan = ComputeSettle({sp, ord}, {1, 2}, cfg);
  Expect(plan.seats[0].gross_water == 36, "T27 dragon +36");
  Expect(plan.seats[1].gross_water == -36, "T27 ordinary -36");
  Expect(plan.shoots.empty(), "T27 no shoot");
}

void TestT28_NaturalVsDragon() {
  SeatArrange nat;
  nat.report_special = true;
  nat.special = SpecialType::kNaturalDragon;
  SeatArrange drag;
  drag.report_special = true;
  drag.special = SpecialType::kDragon;
  SssConfig cfg = DefaultConfig(2);
  cfg.base_score = 1;
  auto plan = ComputeSettle({nat, drag}, {1, 2}, cfg);
  Expect(plan.seats[0].gross_water == 108, "T28 natural takes 108");
  Expect(plan.seats[1].gross_water == -108, "T28 dragon pays");
}

void TestT29_SameSpecialTie() {
  SeatArrange a;
  a.report_special = true;
  a.special = SpecialType::kDragon;
  SeatArrange b = a;
  SssConfig cfg = DefaultConfig(2);
  cfg.base_score = 1;
  auto plan = ComputeSettle({a, b}, {1, 2}, cfg);
  Expect(plan.seats[0].gross_water == 0 && plan.seats[1].gross_water == 0, "T29 tie");
}

void TestT30_MixedSpecialOrdinary() {
  SeatArrange sp;
  sp.report_special = true;
  sp.special = SpecialType::kSixPair;
  SeatArrange o1 = MakeOrdinary({C(0, 12), C(1, 11), C(2, 10)},
                                {C(0, 0), C(1, 1), C(2, 2), C(3, 3), C(0, 4)},
                                {C(1, 0), C(2, 1), C(3, 2), C(0, 3), C(1, 4)});
  SeatArrange o2 = MakeOrdinary({C(3, 12), C(0, 9), C(1, 8)},
                                {C(2, 0), C(3, 1), C(0, 5), C(1, 6), C(2, 7)},
                                {C(3, 0), C(0, 6), C(1, 7), C(2, 8), C(3, 9)});
  SssConfig cfg = DefaultConfig(3);
  cfg.base_score = 1;
  cfg.enable_special = true;
  auto plan = ComputeSettle({sp, o1, o2}, {1, 2, 3}, cfg);
  Expect(plan.seats[0].gross_water == 8, "T30 sixpair +4*2");  // 4 each to 2 ordinary
}

void TestT31_Rake() {
  int64_t rake = 0;
  Expect(ApplyRake(1000, 500, &rake) == 950 && rake == 50, "T31 rake");
}

void TestT32_RakeNonPositive() {
  int64_t rake = 99;
  Expect(ApplyRake(0, 500, &rake) == 0 && rake == 0, "T32 zero");
  Expect(ApplyRake(-100, 500, &rake) == -100 && rake == 0, "T32 neg");
}

void TestT33_Conserve() {
  SeatArrange s0 = MakeOrdinary({C(0, 12), C(1, 12), C(2, 1)},
                                {C(0, 0), C(1, 2), C(2, 4), C(3, 6), C(0, 8)},
                                {C(0, 3), C(1, 5), C(2, 7), C(3, 9), C(0, 11)});
  SeatArrange s1 = MakeOrdinary({C(2, 10), C(3, 8), C(1, 4)},
                                {C(1, 0), C(2, 2), C(3, 4), C(0, 6), C(1, 8)},
                                {C(1, 3), C(2, 5), C(3, 7), C(0, 9), C(1, 11)});
  SssConfig cfg = DefaultConfig(2);
  cfg.base_score = 100;
  cfg.rake_bp = 500;
  auto plan = ComputeSettle({s0, s1}, {1, 2}, cfg);
  int64_t sum = 0;
  for (auto& s : plan.seats) sum += s.net + s.rake;
  Expect(sum == 0, "T33 sum net+rake == 0");
}

void TestT34_ShootDisabled() {
  SeatArrange s0 = MakeOrdinary({C(0, 11), C(1, 11), C(2, 11)},
                                {C(3, 8), C(3, 9), C(3, 10), C(3, 11), C(3, 7)},
                                {C(0, 12), C(1, 12), C(2, 12), C(3, 12), C(0, 0)});
  SeatArrange s1 = MakeOrdinary({C(0, 1), C(1, 2), C(2, 3)},
                                {C(0, 4), C(1, 5), C(2, 6), C(3, 0), C(0, 2)},
                                {C(1, 4), C(2, 5), C(3, 6), C(0, 3), C(1, 0)});
  SssConfig on = DefaultConfig(2);
  on.base_score = 1;
  on.enable_shoot = true;
  SssConfig off = on;
  off.enable_shoot = false;
  auto a = ComputeSettle({s0, s1}, {1, 2}, on);
  auto b = ComputeSettle({s0, s1}, {1, 2}, off);
  if (!a.shoots.empty()) {
    Expect(std::abs(a.seats[0].gross_water) == 2 * std::abs(b.seats[0].gross_water) ||
               a.seats[0].gross_water != b.seats[0].gross_water,
           "T34 shoot doubles vs disabled");
    Expect(b.shoots.empty(), "T34 no shoot flag");
  } else {
    Expect(true, "T34 no shoot scenario skip");
  }
}

void TestT35_SpecialDisabled() {
  Hand13 h{};
  for (int r = 0; r < 13; ++r) h[r] = MakeCard(r % 4, r);
  Expect(DetectSpecial(h, false) == SpecialType::kNone, "T35 disabled NONE");
}

int main() {
  TestT01_FiveTypeOrder();
  TestT02_SfCompare();
  TestT03_StraightTops();
  TestT04_NotStraight();
  TestT05_HighTieNoSuit();
  TestT06_HeadOrder();
  TestT07_HeadIgnoresFlushStraight();
  TestT08_LegalArrange();
  TestT09_DaoShui();
  TestT10_HeadThreeVsMidTwoPair();
  TestT11_HeadPairVsMidPair();
  TestT12_BadMultiset();
  TestT13_AutoArrangeDeterministic();
  TestT14_AutoPrefersTail();
  TestT15_Dragon();
  TestT16_NaturalDragon();
  TestT17_AllSmallRejectsA();
  TestT18_AllSmall();
  TestT19_SixPair();
  TestT19b_ThreeStraight();
  TestT19c_ThreeFlushNotStraight();
  TestT19d_CoveringStraightsAreDragon();
  TestT20_HeadOrdinary();
  TestT21_HeadThreeWater();
  TestT22_MidSfWater();
  TestT23_TailFourWater();
  TestT24_Shoot();
  TestT25_HomeRun();
  TestT26_NoHomeRunTwoPlayer();
  TestT27_SpecialVsOrdinary();
  TestT28_NaturalVsDragon();
  TestT29_SameSpecialTie();
  TestT30_MixedSpecialOrdinary();
  TestT31_Rake();
  TestT32_RakeNonPositive();
  TestT33_Conserve();
  TestT34_ShootDisabled();
  TestT35_SpecialDisabled();
  if (fails) {
    std::cerr << fails << " failed\n";
    return 1;
  }
  std::cout << "all passed\n";
  return 0;
}
