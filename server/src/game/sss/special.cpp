#include "game/sss/special.hpp"

#include <algorithm>
#include <cstring>

namespace pandora {
namespace sss {
namespace {

bool ThreeStraight(const HeadCards& h) {
  int r[3] = {RankOf(h[0]), RankOf(h[1]), RankOf(h[2])};
  std::sort(r, r + 3);
  if (r[0] == 0 && r[1] == 1 && r[2] == 12) return true;
  return r[1] == r[0] + 1 && r[2] == r[1] + 1;
}

bool ThreeFlush(const HeadCards& h) {
  return SuitOf(h[0]) == SuitOf(h[1]) && SuitOf(h[1]) == SuitOf(h[2]);
}

bool ThreeSf(const HeadCards& h) { return ThreeFlush(h) && ThreeStraight(h); }

bool FiveIsStraightFamily(const FiveKey& k) {
  return k.level == static_cast<int8_t>(FiveLevel::kStraight) ||
         k.level == static_cast<int8_t>(FiveLevel::kStraightFlush);
}
bool FiveIsFlushFamily(const FiveKey& k) {
  return k.level == static_cast<int8_t>(FiveLevel::kFlush) ||
         k.level == static_cast<int8_t>(FiveLevel::kStraightFlush);
}
bool FiveIsSf(const FiveKey& k) {
  return k.level == static_cast<int8_t>(FiveLevel::kStraightFlush);
}

enum class PartNeed { kFlush, kStraight, kSf };

bool ExistsPartition(const Hand13& hand, PartNeed need) {
  for (int i = 0; i < 13; ++i) {
    for (int j = i + 1; j < 13; ++j) {
      for (int k = j + 1; k < 13; ++k) {
        HeadCards head{hand[i], hand[j], hand[k]};
        bool head_ok = false;
        if (need == PartNeed::kSf) head_ok = ThreeSf(head);
        else if (need == PartNeed::kStraight) head_ok = ThreeStraight(head);
        else head_ok = ThreeFlush(head);
        if (!head_ok) continue;
        int rem[10];
        int ri = 0;
        for (int t = 0; t < 13; ++t) {
          if (t != i && t != j && t != k) rem[ri++] = t;
        }
        for (int a = 0; a < 10; ++a) {
          for (int b = a + 1; b < 10; ++b) {
            for (int c = b + 1; c < 10; ++c) {
              for (int d = c + 1; d < 10; ++d) {
                for (int e = d + 1; e < 10; ++e) {
                  FiveCards mid{hand[rem[a]], hand[rem[b]], hand[rem[c]], hand[rem[d]],
                                hand[rem[e]]};
                  bool used[10] = {};
                  used[a] = used[b] = used[c] = used[d] = used[e] = true;
                  FiveCards tail{};
                  int ti = 0;
                  for (int t = 0; t < 10; ++t) {
                    if (!used[t]) tail[ti++] = hand[rem[t]];
                  }
                  const FiveKey mk = EvalFive(mid);
                  const FiveKey tk = EvalFive(tail);
                  bool mid_ok = false;
                  bool tail_ok = false;
                  if (need == PartNeed::kSf) {
                    mid_ok = FiveIsSf(mk);
                    tail_ok = FiveIsSf(tk);
                  } else if (need == PartNeed::kStraight) {
                    mid_ok = FiveIsStraightFamily(mk);
                    tail_ok = FiveIsStraightFamily(tk);
                  } else {
                    mid_ok = FiveIsFlushFamily(mk);
                    tail_ok = FiveIsFlushFamily(tk);
                  }
                  if (mid_ok && tail_ok) return true;
                }
              }
            }
          }
        }
      }
    }
  }
  return false;
}

}  // namespace

int32_t SpecialWater(SpecialType t) {
  switch (t) {
    case SpecialType::kNaturalDragon:
      return 108;
    case SpecialType::kDragon:
      return 36;
    case SpecialType::kTwelveRoyals:
      return 24;
    case SpecialType::kThreeSf:
    case SpecialType::kThreeFour:
      return 20;
    case SpecialType::kAllBig:
    case SpecialType::kAllSmall:
    case SpecialType::kOneColor:
      return 10;
    case SpecialType::kFourThree:
      return 6;
    case SpecialType::kFivePairThree:
      return 5;
    case SpecialType::kSixPair:
      return 4;
    case SpecialType::kThreeStraight:
    case SpecialType::kThreeFlush:
      return 3;
    default:
      return 0;
  }
}

SpecialType DetectSpecial(const Hand13& hand, bool enable) {
  if (!enable) return SpecialType::kNone;

  int cnt[13];
  std::memset(cnt, 0, sizeof(cnt));
  int suit_cnt[4] = {};
  bool all_red = true;
  bool all_black = true;
  for (CardId c : hand) {
    cnt[RankOf(c)]++;
    suit_cnt[SuitOf(c)]++;
    if (!IsRed(c)) all_red = false;
    if (!IsBlack(c)) all_black = false;
  }

  bool ranks_full = true;
  for (int r = 0; r < 13; ++r) {
    if (cnt[r] != 1) ranks_full = false;
  }
  if (ranks_full) {
    for (int s = 0; s < 4; ++s) {
      if (suit_cnt[s] == 13) return SpecialType::kNaturalDragon;
    }
    return SpecialType::kDragon;
  }

  bool royals = true;
  for (CardId c : hand) {
    const int r = RankOf(c);
    if (r < 9) {
      royals = false;
      break;
    }
  }
  if (royals) return SpecialType::kTwelveRoyals;

  if (ExistsPartition(hand, PartNeed::kSf)) return SpecialType::kThreeSf;

  int nquad = 0, ntrip = 0, npair = 0, nsingle = 0;
  for (int r = 0; r < 13; ++r) {
    if (cnt[r] == 4) ++nquad;
    else if (cnt[r] == 3) ++ntrip;
    else if (cnt[r] == 2) ++npair;
    else if (cnt[r] == 1) ++nsingle;
  }
  if (nquad == 3 && nsingle == 1) return SpecialType::kThreeFour;

  bool all_big = true;
  bool all_small = true;
  for (CardId c : hand) {
    const int r = RankOf(c);
    if (r < 6) all_big = false;
    if (r > 6) all_small = false;  // A=12 excluded
  }
  if (all_big) return SpecialType::kAllBig;
  if (all_small) return SpecialType::kAllSmall;

  if (all_red || all_black) return SpecialType::kOneColor;

  if (ntrip == 4 && nsingle == 1) return SpecialType::kFourThree;
  if (ntrip == 1 && npair == 5) return SpecialType::kFivePairThree;
  if (npair == 6 && nsingle == 1) return SpecialType::kSixPair;

  if (ExistsPartition(hand, PartNeed::kStraight)) return SpecialType::kThreeStraight;
  if (ExistsPartition(hand, PartNeed::kFlush)) return SpecialType::kThreeFlush;

  return SpecialType::kNone;
}

}  // namespace sss
}  // namespace pandora
