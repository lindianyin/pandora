#include "game/hzmj/meld.hpp"

#include <algorithm>

namespace pandora {
namespace hzmj {

bool CanMeldDiscard(TileId discard) {
  if (!IsValidTile(discard)) return false;
  return !IsCaishenFixedBai(discard);
}

bool CanPeng(const HandCount& hand, TileId discard) {
  if (!CanMeldDiscard(discard)) return false;
  return hand[static_cast<size_t>(discard)] >= 2;
}

bool CanMingGang(const HandCount& hand, TileId discard) {
  if (!CanMeldDiscard(discard)) return false;
  return hand[static_cast<size_t>(discard)] >= 3;
}

bool CanAnGang(const HandCount& hand, TileId tile) {
  if (!IsValidTile(tile) || IsCaishenFixedBai(tile)) return false;
  return hand[static_cast<size_t>(tile)] >= 4;
}

bool CanBuGang(const HandCount& hand, const std::vector<Meld>& melds, TileId tile) {
  if (!IsValidTile(tile) || IsCaishenFixedBai(tile)) return false;
  if (hand[static_cast<size_t>(tile)] < 1) return false;
  for (const auto& m : melds) {
    if (m.type == MeldType::kPeng && m.tile == tile) return true;
  }
  return false;
}

std::vector<ChiOption> ListChiOptions(const HandCount& hand, TileId discard) {
  std::vector<ChiOption> out;
  if (!CanMeldDiscard(discard) || !IsNumbered(discard)) return out;
  const int base = SuitBase(discard);
  const int r = RankInSuit(discard);
  // three patterns: discard is low/mid/high of the chow
  const int shifts[3][2] = {{1, 2}, {-1, 1}, {-2, -1}};
  for (auto sh : shifts) {
    const int a = r + sh[0];
    const int b = r + sh[1];
    if (a < 0 || a > 8 || b < 0 || b > 8) continue;
    const TileId ta = base + a;
    const TileId tb = base + b;
    if (hand[static_cast<size_t>(ta)] < 1 || hand[static_cast<size_t>(tb)] < 1) continue;
    // cannot use caishen as substitute for chi from discard
    if (IsCaishenFixedBai(ta) || IsCaishenFixedBai(tb)) continue;
    ChiOption opt;
    opt.hand_tiles = {ta, tb};
    std::array<TileId, 3> formed{ta, tb, discard};
    std::sort(formed.begin(), formed.end());
    opt.formed = formed;
    out.push_back(opt);
  }
  return out;
}

bool CanChi(const HandCount& hand, TileId discard) { return !ListChiOptions(hand, discard).empty(); }

HandCount ApplyPeng(HandCount hand, TileId discard) {
  RemoveTile(hand, discard, 2);
  return hand;
}

HandCount ApplyMingGang(HandCount hand, TileId discard) {
  RemoveTile(hand, discard, 3);
  return hand;
}

HandCount ApplyAnGang(HandCount hand, TileId tile) {
  RemoveTile(hand, tile, 4);
  return hand;
}

HandCount ApplyChi(HandCount hand, const ChiOption& opt) {
  RemoveTile(hand, opt.hand_tiles[0], 1);
  RemoveTile(hand, opt.hand_tiles[1], 1);
  return hand;
}

}  // namespace hzmj
}  // namespace pandora
