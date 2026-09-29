#include "game/hzmj/tiles.hpp"

namespace pandora {
namespace hzmj {

HandCount ZeroHand() {
  HandCount h{};
  h.fill(0);
  return h;
}

void AddTile(HandCount& h, TileId t, int n) {
  if (!IsValidTile(t) || n <= 0) return;
  h[static_cast<size_t>(t)] += n;
}

bool RemoveTile(HandCount& h, TileId t, int n) {
  if (!IsValidTile(t) || n <= 0) return false;
  auto& c = h[static_cast<size_t>(t)];
  if (c < n) return false;
  c -= n;
  return true;
}

int HandSize(const HandCount& h) {
  int n = 0;
  for (int c : h) n += c;
  return n;
}

HandCount CountTiles(const std::vector<TileId>& tiles) {
  HandCount h = ZeroHand();
  for (TileId t : tiles) AddTile(h, t);
  return h;
}

std::vector<TileId> BuildWall() {
  std::vector<TileId> wall;
  wall.reserve(kWallSize);
  for (TileId t = 0; t < kTileKinds; ++t) {
    for (int i = 0; i < 4; ++i) wall.push_back(t);
  }
  return wall;
}

}  // namespace hzmj
}  // namespace pandora
