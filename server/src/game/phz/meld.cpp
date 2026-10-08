#include "game/phz/meld.hpp"

#include <algorithm>

namespace pandora {
namespace phz {

static bool IsSpecialChi(const std::vector<TileId>& t) {
  if (t.size() != 3) return false;
  std::vector<TileId> s = t;
  std::sort(s.begin(), s.end());
  auto eq = [&](TileId a, TileId b, TileId c) { return s[0] == a && s[1] == b && s[2] == c; };
  return eq(0, 1, 2) || eq(10, 11, 12) || eq(1, 6, 9) || eq(11, 16, 19);
}

int HuXiOfMeld(const Meld& m) {
  if (m.kind == MeldKind::kChi || m.kind == MeldKind::kJiao) {
    if (m.tiles.size() == 3) {
      std::vector<TileId> s = m.tiles;
      std::sort(s.begin(), s.end());
      if (s[0] == 0 && s[1] == 1 && s[2] == 2) return 3;
      if (s[0] == 10 && s[1] == 11 && s[2] == 12) return 6;
      if (s[0] == 1 && s[1] == 6 && s[2] == 9) return 3;
      if (s[0] == 11 && s[1] == 16 && s[2] == 19) return 6;
    }
    return 0;
  }
  const TileId t = m.tile;
  if (!IsValidTile(t)) return 0;
  const bool big = IsBig(t);
  switch (m.kind) {
    case MeldKind::kPeng:
      return big ? 3 : 1;
    case MeldKind::kWei:
    case MeldKind::kChouWei:
    case MeldKind::kKan:
      return big ? 6 : 3;
    case MeldKind::kPao:
      return big ? 9 : 6;
    case MeldKind::kTi:
      return big ? 12 : 9;
    default:
      return 0;
  }
}

int HuXiOfKan(TileId t) {
  Meld m;
  m.kind = MeldKind::kKan;
  m.tile = t;
  m.tiles = {t, t, t};
  return HuXiOfMeld(m);
}

static bool TryChiPair(const HandCount& hand, TileId a, TileId b, TileId claim, ChiOption* out) {
  if (!IsValidTile(a) || !IsValidTile(b)) return false;
  HandCount h = hand;
  if (a == claim || b == claim) return false;
  if (!RemoveTile(h, a, 1) || !RemoveTile(h, b, 1)) return false;
  std::vector<TileId> three{a, b, claim};
  std::sort(three.begin(), three.end());
  // Straight same size consecutive ranks
  if (IsSmall(three[0]) == IsSmall(three[1]) && IsSmall(three[1]) == IsSmall(three[2])) {
    if (Rank(three[0]) + 1 == Rank(three[1]) && Rank(three[1]) + 1 == Rank(three[2])) {
      if (out) out->hand_tiles = {a, b};
      return true;
    }
  }
  if (IsSpecialChi(three)) {
    if (out) out->hand_tiles = {a, b};
    return true;
  }
  // Jiao: two of one size + one of other same ranks pattern — allow mixed same-rank pairs + third
  // Simple: ranks form consecutive after mapping by Rank only (ignore size) for jiao-like
  return false;
}

bool CanFormChi(const HandCount& hand, TileId claim, ChiOption* out) {
  if (!IsValidTile(claim)) return false;
  // Try all pairs of distinct positions in hand
  for (TileId a = 0; a < kTileKinds; ++a) {
    if (hand[static_cast<size_t>(a)] <= 0) continue;
    for (TileId b = a; b < kTileKinds; ++b) {
      const int need_b = (a == b) ? 2 : 1;
      if (hand[static_cast<size_t>(b)] < need_b) continue;
      if (a == claim || b == claim) continue;
      ChiOption opt;
      if (TryChiPair(hand, a, b, claim, &opt)) {
        if (out) *out = opt;
        return true;
      }
    }
  }
  return false;
}

bool CanPeng(const HandCount& hand, TileId claim) {
  return IsValidTile(claim) && hand[static_cast<size_t>(claim)] >= 2;
}

std::vector<TileId> ChiTiles(TileId claim, const ChiOption& opt) {
  std::vector<TileId> t = opt.hand_tiles;
  t.push_back(claim);
  std::sort(t.begin(), t.end());
  return t;
}

}  // namespace phz
}  // namespace pandora
