#include "game/hzmj/hu.hpp"

#include <algorithm>
#include <cstring>

namespace pandora {
namespace hzmj {
namespace {

bool TryMelds(int c[34], int jokers);

bool RemoveChow(int c[34], int jokers, int base, int r0) {
  // consume tiles base+r0, r0+1, r0+2 with jokers filling gaps
  int need[3] = {0, 0, 0};
  for (int i = 0; i < 3; ++i) {
    const int t = base + r0 + i;
    if (c[t] > 0) {
      --c[t];
    } else {
      ++need[i];
    }
  }
  const int need_j = need[0] + need[1] + need[2];
  if (need_j > jokers) {
    for (int i = 0; i < 3; ++i) {
      const int t = base + r0 + i;
      if (need[i] == 0) ++c[t];
    }
    return false;
  }
  if (!TryMelds(c, jokers - need_j)) {
    for (int i = 0; i < 3; ++i) {
      const int t = base + r0 + i;
      if (need[i] == 0) ++c[t];
    }
    return false;
  }
  return true;
}

bool TryMelds(int c[34], int jokers) {
  int sum = jokers;
  for (int i = 0; i < 34; ++i) sum += c[i];
  if (sum == 0) return true;
  if (sum % 3 != 0) return false;

  int i = 0;
  while (i < 34 && c[i] == 0) ++i;
  if (i >= 34) {
    // only jokers left: each 3 jokers = one meld
    return jokers % 3 == 0;
  }

  // pung
  if (c[i] >= 3) {
    c[i] -= 3;
    if (TryMelds(c, jokers)) {
      c[i] += 3;
      return true;
    }
    c[i] += 3;
  }
  if (c[i] >= 2 && jokers >= 1) {
    c[i] -= 2;
    if (TryMelds(c, jokers - 1)) {
      c[i] += 2;
      return true;
    }
    c[i] += 2;
  }
  if (c[i] >= 1 && jokers >= 2) {
    c[i] -= 1;
    if (TryMelds(c, jokers - 2)) {
      c[i] += 1;
      return true;
    }
    c[i] += 1;
  }
  if (c[i] == 0 && jokers >= 3) {
    if (TryMelds(c, jokers - 3)) return true;
  }

  // chow for numbered suits
  if (IsNumbered(i)) {
    const int base = SuitBase(i);
    const int r = RankInSuit(i);
    for (int start = std::max(0, r - 2); start <= r && start <= 6; ++start) {
      if (RemoveChow(c, jokers, base, start)) return true;
    }
  }
  return false;
}

bool StandardWin(const HandCount& hand, bool* out_baotou) {
  int c[34];
  for (int i = 0; i < 34; ++i) c[i] = hand[static_cast<size_t>(i)];
  int jokers = c[kBai];
  c[kBai] = 0;
  int total = jokers;
  for (int i = 0; i < 34; ++i) total += c[i];
  // Closed 14, or remaining after exposed melds: 11/8/5/2 (pair + 3/2/1/0 sets).
  if (total < 2 || total > 14 || total % 3 != 2) return false;

  // enumerate pair
  for (int p = 0; p < 34; ++p) {
    if (p == kBai) continue;
    for (int use_j = 0; use_j <= 2; ++use_j) {
      const int from_tile = 2 - use_j;
      if (c[p] < from_tile) continue;
      if (jokers < use_j) continue;
      c[p] -= from_tile;
      const int j2 = jokers - use_j;
      if (TryMelds(c, j2)) {
        c[p] += from_tile;
        if (out_baotou) *out_baotou = (use_j == 1);
        return true;
      }
      c[p] += from_tile;
    }
  }
  // pair of two jokers
  if (jokers >= 2) {
    if (TryMelds(c, jokers - 2)) {
      if (out_baotou) *out_baotou = false;
      return true;
    }
  }
  return false;
}

HuResult CheckQiDui(const HandCount& hand, const std::vector<Meld>& melds) {
  HuResult r;
  if (!melds.empty()) return r;
  int c[34];
  for (int i = 0; i < 34; ++i) c[i] = hand[static_cast<size_t>(i)];
  int jokers = c[kBai];
  c[kBai] = 0;
  int total = jokers;
  for (int i = 0; i < 34; ++i) total += c[i];
  if (total != 14) return r;

  int pairs_needed = 0;
  int haohua = 0;
  bool has_caishen = jokers > 0;
  for (int i = 0; i < 34; ++i) {
    if (c[i] == 0) continue;
    if (c[i] == 1) {
      ++pairs_needed;  // need 1 joker
    } else if (c[i] == 2) {
      // ok pair
    } else if (c[i] == 3) {
      ++pairs_needed;  // treat as pair +1 odd
    } else if (c[i] == 4) {
      ++haohua;
    } else {
      return r;
    }
  }
  // each 4 counts as one pair-group for seven pairs (still 7 pairs: 4=2 pairs)
  // recount properly:
  int pair_slots = 0;
  haohua = 0;
  pairs_needed = 0;
  for (int i = 0; i < 34; ++i) {
    int n = c[i];
    if (n == 0) continue;
    if (n == 4) {
      pair_slots += 2;
      ++haohua;
    } else if (n == 3) {
      pair_slots += 1;
      ++pairs_needed;  // one leftover
    } else if (n == 2) {
      pair_slots += 1;
    } else if (n == 1) {
      ++pairs_needed;
    } else {
      return r;
    }
  }
  if (pairs_needed > jokers) return r;
  const int jokers_left = jokers - pairs_needed;
  pair_slots += pairs_needed;           // each odd completed to a pair
  pair_slots += jokers_left / 2;        // leftover jokers form pairs
  if (pair_slots != 7) return r;
  if (jokers_left % 2 != 0) return r;

  r.ok = true;
  r.kind = HuKind::kQiDui;
  r.haohua = haohua;
  r.qing_qi_dui = !has_caishen;
  // Qi ke: at least one caishen completes a real singleton into a pair.
  r.baotou = pairs_needed > 0;
  return r;
}

}  // namespace

HuResult CheckHu(const HandCount& hand, const std::vector<Meld>& melds, TileId /*win_tile*/, bool /*zimo*/) {
  const int meld_tiles = static_cast<int>(melds.size()) * 3;
  // an/ming gang still count as one meld consuming 3 from "14 structure" visually;
  // for hand size: concealed + win should be 14 - 3*num_melds (gangs in melds: we store as meld of type gang,
  // still one set).
  int gang_extra = 0;
  for (const auto& m : melds) {
    if (m.type == MeldType::kMingGang || m.type == MeldType::kAnGang || m.type == MeldType::kBuGang) {
      // gang exposes 4 but structure uses one pung-slot; hand removed 3 or 4 already.
      (void)gang_extra;
    }
  }
  const int expect = 14 - meld_tiles;
  if (HandSize(hand) != expect && !(melds.empty() && HandSize(hand) == 14)) {
    // allow 14 always for closed hand path
    if (!(melds.empty() && HandSize(hand) == 14)) {
      HuResult bad;
      return bad;
    }
  }

  auto qd = CheckQiDui(hand, melds);
  if (qd.ok) return qd;

  bool baotou = false;
  if (!StandardWin(hand, &baotou)) return HuResult{};
  HuResult r;
  r.ok = true;
  r.kind = HuKind::kPing;
  r.baotou = baotou;
  return r;
}

bool WouldHu(HandCount hand, const std::vector<Meld>& melds, TileId tile, bool zimo) {
  if (!IsValidTile(tile)) return false;
  AddTile(hand, tile);
  return CheckHu(hand, melds, tile, zimo).ok;
}

TingInfo ComputeTing(const HandCount& hand, const std::vector<Meld>& melds) {
  TingInfo info;
  // 13-tile waiting hand (or 13-3m)
  const int expect_wait = 13 - static_cast<int>(melds.size()) * 3;
  if (HandSize(hand) != expect_wait && !(melds.empty() && HandSize(hand) == 13)) {
    return info;
  }

  int win_count = 0;
  for (TileId t = 0; t < kTileKinds; ++t) {
    if (WouldHu(hand, melds, t, true)) {
      info.waits.push_back(t);
      ++win_count;
    }
  }
  // baotou ting: wins on every non-impossible tile — practically wins on all 34 kinds
  // (or all tiles still in play). SPEC: next any draw wins with caishen pair.
  if (win_count >= 30) {
    info.baotou_ting = true;
  } else {
    // also detect: pair is caishen+X style — if waits cover all tiles that aren't over-drawn
    // simpler heuristic already above; additionally check StandardWin baotou for each
    bool all_baotou = !info.waits.empty();
    for (TileId t : info.waits) {
      HandCount h2 = hand;
      AddTile(h2, t);
      auto r = CheckHu(h2, melds, t, true);
      if (!r.ok || !r.baotou) {
        all_baotou = false;
        break;
      }
    }
    if (all_baotou && info.waits.size() >= 20) info.baotou_ting = true;
  }
  return info;
}

}  // namespace hzmj
}  // namespace pandora
