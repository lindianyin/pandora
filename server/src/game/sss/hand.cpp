#include "game/sss/hand.hpp"

#include <algorithm>
#include <cstring>
#include <vector>

namespace pandora {
namespace sss {
namespace {

constexpr int8_t kNeg = -128;

int CmpI8(int8_t a, int8_t b) {
  if (a < b) return -1;
  if (a > b) return 1;
  return 0;
}

int CompareFiveKey(const FiveKey& a, const FiveKey& b) {
  if (a.level != b.level) return a.level < b.level ? -1 : 1;
  if (int c = CmpI8(a.p0, b.p0)) return c;
  if (int c = CmpI8(a.p1, b.p1)) return c;
  if (int c = CmpI8(a.p2, b.p2)) return c;
  if (int c = CmpI8(a.p3, b.p3)) return c;
  if (int c = CmpI8(a.p4, b.p4)) return c;
  return 0;
}

int CompareHeadKey(const HeadKey& a, const HeadKey& b) {
  if (a.level != b.level) return a.level < b.level ? -1 : 1;
  if (int c = CmpI8(a.p0, b.p0)) return c;
  if (int c = CmpI8(a.p1, b.p1)) return c;
  if (int c = CmpI8(a.p2, b.p2)) return c;
  return 0;
}

bool TryFiveStraightTop(const int* ranks_sorted_asc, int8_t* top) {
  // ranks_sorted_asc length 5, ascending unique ranks.
  const int a0 = ranks_sorted_asc[0];
  const int a1 = ranks_sorted_asc[1];
  const int a2 = ranks_sorted_asc[2];
  const int a3 = ranks_sorted_asc[3];
  const int a4 = ranks_sorted_asc[4];
  // A2345
  if (a0 == 0 && a1 == 1 && a2 == 2 && a3 == 3 && a4 == 12) {
    *top = -1;
    return true;
  }
  if (a1 == a0 + 1 && a2 == a1 + 1 && a3 == a2 + 1 && a4 == a3 + 1) {
    *top = static_cast<int8_t>(a4);
    return true;
  }
  return false;
}

bool TryThreeStraightTop(int r0, int r1, int r2, int8_t* top) {
  int a[3] = {r0, r1, r2};
  std::sort(a, a + 3);
  if (a[0] == 0 && a[1] == 1 && a[2] == 12) {
    *top = -1;
    return true;
  }
  if (a[1] == a[0] + 1 && a[2] == a[1] + 1) {
    *top = static_cast<int8_t>(a[2]);
    return true;
  }
  return false;
}

}  // namespace

FiveKey EvalFive(const FiveCards& cards) {
  CardId c[5] = {cards[0], cards[1], cards[2], cards[3], cards[4]};
  std::sort(c, c + 5, [](CardId a, CardId b) {
    if (RankOf(a) != RankOf(b)) return RankOf(a) > RankOf(b);
    return SuitOf(a) > SuitOf(b);
  });
  int cnt[13];
  std::memset(cnt, 0, sizeof(cnt));
  for (int i = 0; i < 5; ++i) cnt[RankOf(c[i])]++;

  int ranks_desc[5];
  for (int i = 0; i < 5; ++i) ranks_desc[i] = RankOf(c[i]);

  const bool same_suit =
      SuitOf(c[0]) == SuitOf(c[1]) && SuitOf(c[1]) == SuitOf(c[2]) && SuitOf(c[2]) == SuitOf(c[3]) &&
      SuitOf(c[3]) == SuitOf(c[4]);

  bool five_distinct = true;
  for (int r = 0; r < 13; ++r) {
    if (cnt[r] > 1) five_distinct = false;
  }
  int8_t st_top = 0;
  bool is_st = false;
  if (five_distinct) {
    int asc[5];
    int ai = 0;
    for (int r = 0; r < 13; ++r) {
      if (cnt[r]) asc[ai++] = r;
    }
    is_st = TryFiveStraightTop(asc, &st_top);
  }

  FiveKey k{};
  k.p0 = k.p1 = k.p2 = k.p3 = k.p4 = kNeg;

  // Find pattern groups by count
  int quad = -1, trip = -1, pairs[2] = {-1, -1}, npair = 0;
  for (int r = 12; r >= 0; --r) {
    if (cnt[r] == 4) quad = r;
    else if (cnt[r] == 3) trip = r;
    else if (cnt[r] == 2) {
      if (npair < 2) pairs[npair++] = r;
    }
  }

  if (same_suit && is_st) {
    k.level = static_cast<int8_t>(FiveLevel::kStraightFlush);
    k.p0 = st_top;
    return k;
  }
  if (quad >= 0) {
    k.level = static_cast<int8_t>(FiveLevel::kFour);
    k.p0 = static_cast<int8_t>(quad);
    for (int r = 12; r >= 0; --r) {
      if (cnt[r] == 1) {
        k.p1 = static_cast<int8_t>(r);
        break;
      }
    }
    return k;
  }
  if (trip >= 0 && npair >= 1) {
    k.level = static_cast<int8_t>(FiveLevel::kFullHouse);
    k.p0 = static_cast<int8_t>(trip);
    k.p1 = static_cast<int8_t>(pairs[0]);
    return k;
  }
  if (same_suit) {
    k.level = static_cast<int8_t>(FiveLevel::kFlush);
    k.p0 = static_cast<int8_t>(ranks_desc[0]);
    k.p1 = static_cast<int8_t>(ranks_desc[1]);
    k.p2 = static_cast<int8_t>(ranks_desc[2]);
    k.p3 = static_cast<int8_t>(ranks_desc[3]);
    k.p4 = static_cast<int8_t>(ranks_desc[4]);
    return k;
  }
  if (is_st) {
    k.level = static_cast<int8_t>(FiveLevel::kStraight);
    k.p0 = st_top;
    return k;
  }
  if (trip >= 0) {
    k.level = static_cast<int8_t>(FiveLevel::kThree);
    k.p0 = static_cast<int8_t>(trip);
    int ki = 1;
    for (int r = 12; r >= 0; --r) {
      if (cnt[r] == 1) {
        if (ki == 1) k.p1 = static_cast<int8_t>(r);
        else if (ki == 2) k.p2 = static_cast<int8_t>(r);
        ++ki;
      }
    }
    return k;
  }
  if (npair == 2) {
    k.level = static_cast<int8_t>(FiveLevel::kTwoPair);
    k.p0 = static_cast<int8_t>(pairs[0]);
    k.p1 = static_cast<int8_t>(pairs[1]);
    for (int r = 12; r >= 0; --r) {
      if (cnt[r] == 1) {
        k.p2 = static_cast<int8_t>(r);
        break;
      }
    }
    return k;
  }
  if (npair == 1) {
    k.level = static_cast<int8_t>(FiveLevel::kPair);
    k.p0 = static_cast<int8_t>(pairs[0]);
    int ki = 1;
    for (int r = 12; r >= 0; --r) {
      if (cnt[r] == 1) {
        if (ki == 1) k.p1 = static_cast<int8_t>(r);
        else if (ki == 2) k.p2 = static_cast<int8_t>(r);
        else if (ki == 3) k.p3 = static_cast<int8_t>(r);
        ++ki;
      }
    }
    return k;
  }
  k.level = static_cast<int8_t>(FiveLevel::kHigh);
  k.p0 = static_cast<int8_t>(ranks_desc[0]);
  k.p1 = static_cast<int8_t>(ranks_desc[1]);
  k.p2 = static_cast<int8_t>(ranks_desc[2]);
  k.p3 = static_cast<int8_t>(ranks_desc[3]);
  k.p4 = static_cast<int8_t>(ranks_desc[4]);
  return k;
}

HeadKey EvalHead(const HeadCards& cards) {
  CardId c[3] = {cards[0], cards[1], cards[2]};
  std::sort(c, c + 3, [](CardId a, CardId b) {
    if (RankOf(a) != RankOf(b)) return RankOf(a) > RankOf(b);
    return SuitOf(a) > SuitOf(b);
  });
  const int r0 = RankOf(c[0]);
  const int r1 = RankOf(c[1]);
  const int r2 = RankOf(c[2]);
  HeadKey k{};
  k.p0 = k.p1 = k.p2 = kNeg;
  if (r0 == r1 && r1 == r2) {
    k.level = static_cast<int8_t>(FiveLevel::kThree);
    k.p0 = static_cast<int8_t>(r0);
    return k;
  }
  if (r0 == r1 || r1 == r2 || r0 == r2) {
    k.level = static_cast<int8_t>(FiveLevel::kPair);
    if (r0 == r1) {
      k.p0 = static_cast<int8_t>(r0);
      k.p1 = static_cast<int8_t>(r2);
    } else if (r1 == r2) {
      k.p0 = static_cast<int8_t>(r1);
      k.p1 = static_cast<int8_t>(r0);
    } else {
      k.p0 = static_cast<int8_t>(r0);
      k.p1 = static_cast<int8_t>(r1);
    }
    return k;
  }
  k.level = static_cast<int8_t>(FiveLevel::kHigh);
  k.p0 = static_cast<int8_t>(r0);
  k.p1 = static_cast<int8_t>(r1);
  k.p2 = static_cast<int8_t>(r2);
  return k;
}

int CompareFive(const FiveKey& a, const FiveKey& b) { return CompareFiveKey(a, b); }
int CompareFive(const FiveCards& a, const FiveCards& b) {
  return CompareFiveKey(EvalFive(a), EvalFive(b));
}
int CompareHead(const HeadKey& a, const HeadKey& b) { return CompareHeadKey(a, b); }
int CompareHead(const HeadCards& a, const HeadCards& b) {
  return CompareHeadKey(EvalHead(a), EvalHead(b));
}

int CmpOrderHeadFive(const HeadKey& head, const FiveKey& five) {
  if (head.level != five.level) return head.level < five.level ? -1 : 1;
  // Same level in {HIGH, PAIR, THREE}
  if (head.level == static_cast<int8_t>(FiveLevel::kThree)) {
    if (int c = CmpI8(head.p0, five.p0)) return c;
    return -1;  // defensive: head < five
  }
  if (head.level == static_cast<int8_t>(FiveLevel::kPair)) {
    if (int c = CmpI8(head.p0, five.p0)) return c;
    // five kicker = max of p1,p2,p3
    int8_t fk = five.p1;
    if (five.p2 > fk) fk = five.p2;
    if (five.p3 > fk) fk = five.p3;
    if (int c = CmpI8(head.p1, fk)) return c;
    return -1;
  }
  // HIGH: compare first 3 of five (p0,p1,p2) vs head
  if (int c = CmpI8(head.p0, five.p0)) return c;
  if (int c = CmpI8(head.p1, five.p1)) return c;
  if (int c = CmpI8(head.p2, five.p2)) return c;
  return -1;
}

int CmpOrderFiveFive(const FiveKey& a, const FiveKey& b) { return CompareFiveKey(a, b); }

bool SameMultiset13(const Hand13& hand, const HeadCards& head, const FiveCards& mid,
                    const FiveCards& tail) {
  int cnt[52];
  std::memset(cnt, 0, sizeof(cnt));
  for (CardId x : hand) {
    if (x < 0 || x > 51) return false;
    cnt[x]++;
  }
  auto eat = [&](CardId x) {
    if (x < 0 || x > 51 || cnt[x] <= 0) return false;
    cnt[x]--;
    return true;
  };
  for (CardId x : head)
    if (!eat(x)) return false;
  for (CardId x : mid)
    if (!eat(x)) return false;
  for (CardId x : tail)
    if (!eat(x)) return false;
  for (int i = 0; i < 52; ++i)
    if (cnt[i]) return false;
  return true;
}

bool IsLegalArrange(const HeadCards& head, const FiveCards& mid, const FiveCards& tail) {
  const HeadKey hk = EvalHead(head);
  const FiveKey mk = EvalFive(mid);
  const FiveKey tk = EvalFive(tail);
  return CmpOrderHeadFive(hk, mk) <= 0 && CmpOrderFiveFive(mk, tk) <= 0;
}

Arrange AutoArrange(const Hand13& hand) {
  Arrange best{};
  bool found = false;
  FiveKey best_tail{};
  FiveKey best_mid{};
  HeadKey best_head{};

  // Enumerate C(13,3)*C(10,5).
  for (int i = 0; i < 13; ++i) {
    for (int j = i + 1; j < 13; ++j) {
      for (int k = j + 1; k < 13; ++k) {
        HeadCards head{hand[i], hand[j], hand[k]};
        int rem[10];
        int ri = 0;
        for (int t = 0; t < 13; ++t) {
          if (t != i && t != j && t != k) rem[ri++] = t;
        }
        // Choose 5 of 10 for mid
        for (int a = 0; a < 10; ++a) {
          for (int b = a + 1; b < 10; ++b) {
            for (int c = b + 1; c < 10; ++c) {
              for (int d = c + 1; d < 10; ++d) {
                for (int e = d + 1; e < 10; ++e) {
                  FiveCards mid{hand[rem[a]], hand[rem[b]], hand[rem[c]], hand[rem[d]],
                                hand[rem[e]]};
                  bool used[10] = {};
                  used[a] = used[b] = used[c] = used[d] = used[e] = true;
                  int ti = 0;
                  FiveCards tail{};
                  for (int t = 0; t < 10; ++t) {
                    if (!used[t]) tail[ti++] = hand[rem[t]];
                  }
                  if (!IsLegalArrange(head, mid, tail)) continue;
                  FiveKey kt = EvalFive(tail);
                  FiveKey km = EvalFive(mid);
                  HeadKey kh = EvalHead(head);
                  bool better = false;
                  if (!found) {
                    better = true;
                  } else {
                    const int ct = CompareFiveKey(kt, best_tail);
                    if (ct > 0) {
                      better = true;
                    } else if (ct == 0) {
                      const int cm = CompareFiveKey(km, best_mid);
                      if (cm > 0) {
                        better = true;
                      } else if (cm == 0 && CompareHeadKey(kh, best_head) > 0) {
                        better = true;
                      }
                    }
                  }
                  if (better) {
                    found = true;
                    best = {head, mid, tail};
                    best_tail = kt;
                    best_mid = km;
                    best_head = kh;
                  }
                }
              }
            }
          }
        }
      }
    }
  }
  if (!found) {
    Hand13 sorted = hand;
    std::sort(sorted.begin(), sorted.end());
    best.head = {sorted[0], sorted[1], sorted[2]};
    best.mid = {sorted[3], sorted[4], sorted[5], sorted[6], sorted[7]};
    best.tail = {sorted[8], sorted[9], sorted[10], sorted[11], sorted[12]};
  }
  return best;
}

// Expose three-card straight helper for special.cpp via anonymous - reimplement there.

}  // namespace sss
}  // namespace pandora
