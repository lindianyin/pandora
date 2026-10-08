#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace pandora {
namespace phz {

using TileId = int32_t;

constexpr TileId kTileInvalid = -1;
constexpr int kTileKinds = 20;
constexpr int kWallSize = 80;
constexpr int kSeats = 3;

using HandCount = std::array<int, kTileKinds>;

inline bool IsValidTile(TileId t) { return t >= 0 && t < kTileKinds; }
inline bool IsSmall(TileId t) { return t >= 0 && t <= 9; }
inline bool IsBig(TileId t) { return t >= 10 && t <= 19; }
inline int Rank(TileId t) { return IsValidTile(t) ? (t % 10) : -1; }
inline bool IsRed(TileId t) {
  const int r = Rank(t);
  return r == 1 || r == 6 || r == 9;
}
inline bool SameName(TileId a, TileId b) {
  return IsValidTile(a) && IsValidTile(b) && Rank(a) == Rank(b) && IsSmall(a) == IsSmall(b);
}

HandCount ZeroHand();
void AddTile(HandCount& h, TileId t, int n = 1);
bool RemoveTile(HandCount& h, TileId t, int n = 1);
int HandSize(const HandCount& h);
HandCount CountTiles(const std::vector<TileId>& tiles);
std::vector<TileId> HandToList(const HandCount& h);

// 80 tiles: 4 of each kind 0..19
std::vector<TileId> BuildWall();

}  // namespace phz
}  // namespace pandora
