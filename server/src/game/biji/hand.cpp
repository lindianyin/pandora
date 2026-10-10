#include "game/biji/hand.hpp"

#include <algorithm>
#include <cstring>

namespace pandora {
namespace biji {
namespace {

void SortByRankDesc(CardId* c) {
  std::sort(c, c + 3, [](CardId a, CardId b) {
    if (RankOf(a) != RankOf(b)) return RankOf(a) > RankOf(b);
    return SuitOf(a) > SuitOf(b);
  });
}

bool TryStraightTop(int r0, int r1, int r2, int8_t* top) {
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

int CompareKey(const DunKey& a, const DunKey& b) {
  if (a.type != b.type) return a.type < b.type ? -1 : 1;
  if (a.primary != b.primary) return a.primary < b.primary ? -1 : 1;
  if (a.secondary != b.secondary) return a.secondary < b.secondary ? -1 : 1;
  if (a.tertiary != b.tertiary) return a.tertiary < b.tertiary ? -1 : 1;
  if (a.suit0 != b.suit0) return a.suit0 < b.suit0 ? -1 : 1;
  if (a.suit1 != b.suit1) return a.suit1 < b.suit1 ? -1 : 1;
  if (a.suit2 != b.suit2) return a.suit2 < b.suit2 ? -1 : 1;
  return 0;
}

}  // namespace

DunKey EvalDun(const DunCards& cards) {
  CardId c[3] = {cards[0], cards[1], cards[2]};
  SortByRankDesc(c);
  const int r0 = RankOf(c[0]);
  const int r1 = RankOf(c[1]);
  const int r2 = RankOf(c[2]);
  const int s0 = SuitOf(c[0]);
  const int s1 = SuitOf(c[1]);
  const int s2 = SuitOf(c[2]);
  const bool same_suit = (s0 == s1 && s1 == s2);
  int8_t st_top = 0;
  const bool is_st = TryStraightTop(r0, r1, r2, &st_top);

  DunKey k{};
  if (r0 == r1 && r1 == r2) {
    k.type = static_cast<int8_t>(DunType::kThree);
    k.primary = static_cast<int8_t>(r0);
    k.suit0 = static_cast<int8_t>(std::max({s0, s1, s2}));
    return k;
  }
  if (same_suit && is_st) {
    k.type = static_cast<int8_t>(DunType::kStraightFlush);
    k.primary = st_top;
    k.suit0 = static_cast<int8_t>(std::max({s0, s1, s2}));
    return k;
  }
  if (same_suit) {
    k.type = static_cast<int8_t>(DunType::kFlush);
    k.primary = static_cast<int8_t>(r0);
    k.secondary = static_cast<int8_t>(r1);
    k.tertiary = static_cast<int8_t>(r2);
    k.suit0 = static_cast<int8_t>(s0);
    k.suit1 = static_cast<int8_t>(s1);
    k.suit2 = static_cast<int8_t>(s2);
    return k;
  }
  if (is_st) {
    k.type = static_cast<int8_t>(DunType::kStraight);
    k.primary = st_top;
    k.suit0 = static_cast<int8_t>(std::max({s0, s1, s2}));
    return k;
  }
  if (r0 == r1 || r1 == r2 || r0 == r2) {
    k.type = static_cast<int8_t>(DunType::kPair);
    int pair_r = 0;
    int kick_r = 0;
    int pair_s_max = 0;
    int kick_s = 0;
    if (r0 == r1) {
      pair_r = r0;
      kick_r = r2;
      pair_s_max = std::max(s0, s1);
      kick_s = s2;
    } else if (r1 == r2) {
      pair_r = r1;
      kick_r = r0;
      pair_s_max = std::max(s1, s2);
      kick_s = s0;
    } else {
      pair_r = r0;
      kick_r = r1;
      pair_s_max = std::max(s0, s2);
      kick_s = s1;
    }
    k.primary = static_cast<int8_t>(pair_r);
    k.secondary = static_cast<int8_t>(kick_r);
    k.suit0 = static_cast<int8_t>(pair_s_max);
    k.suit1 = static_cast<int8_t>(kick_s);
    return k;
  }
  k.type = static_cast<int8_t>(DunType::kHigh);
  k.primary = static_cast<int8_t>(r0);
  k.secondary = static_cast<int8_t>(r1);
  k.tertiary = static_cast<int8_t>(r2);
  k.suit0 = static_cast<int8_t>(s0);
  k.suit1 = static_cast<int8_t>(s1);
  k.suit2 = static_cast<int8_t>(s2);
  return k;
}

int CompareDun(const DunKey& a, const DunKey& b) { return CompareKey(a, b); }

int CompareDun(const DunCards& a, const DunCards& b) {
  return CompareKey(EvalDun(a), EvalDun(b));
}

bool IsLegalArrange(const DunCards& head, const DunCards& mid, const DunCards& tail) {
  return CompareDun(head, mid) <= 0 && CompareDun(mid, tail) <= 0;
}

Arrange AutoArrange(const Hand9& hand) {
  Arrange best{};
  bool found = false;
  DunKey best_tail{};
  DunKey best_mid{};
  DunKey best_head{};

  // Enumerate C(9,3)*C(6,3)*C(3,3) ordered partitions.
  for (int i = 0; i < 9; ++i) {
    for (int j = i + 1; j < 9; ++j) {
      for (int k = j + 1; k < 9; ++k) {
        DunCards head{hand[i], hand[j], hand[k]};
        int rem[6];
        int ri = 0;
        for (int t = 0; t < 9; ++t) {
          if (t != i && t != j && t != k) rem[ri++] = t;
        }
        for (int a = 0; a < 6; ++a) {
          for (int b = a + 1; b < 6; ++b) {
            for (int c = b + 1; c < 6; ++c) {
              DunCards mid{hand[rem[a]], hand[rem[b]], hand[rem[c]]};
              int tail_idx[3];
              int ti = 0;
              for (int t = 0; t < 6; ++t) {
                if (t != a && t != b && t != c) tail_idx[ti++] = rem[t];
              }
              DunCards tail{hand[tail_idx[0]], hand[tail_idx[1]], hand[tail_idx[2]]};
              if (!IsLegalArrange(head, mid, tail)) continue;
              DunKey kt = EvalDun(tail);
              DunKey km = EvalDun(mid);
              DunKey kh = EvalDun(head);
              bool better = false;
              if (!found) {
                better = true;
              } else {
                const int ct = CompareKey(kt, best_tail);
                if (ct > 0) {
                  better = true;
                } else if (ct == 0) {
                  const int cm = CompareKey(km, best_mid);
                  if (cm > 0) {
                    better = true;
                  } else if (cm == 0 && CompareKey(kh, best_head) > 0) {
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
  if (!found) {
    // Defensive: sort and slice.
    Hand9 sorted = hand;
    std::sort(sorted.begin(), sorted.end());
    best.head = {sorted[0], sorted[1], sorted[2]};
    best.mid = {sorted[3], sorted[4], sorted[5]};
    best.tail = {sorted[6], sorted[7], sorted[8]};
  }
  return best;
}

}  // namespace biji
}  // namespace pandora
