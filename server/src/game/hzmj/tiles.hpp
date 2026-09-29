#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace pandora {
namespace hzmj {

using TileId = int32_t;

constexpr TileId kTileInvalid = 255;
constexpr int kTileKinds = 34;
constexpr int kWallSize = 136;
constexpr TileId kBai = 33;  // 白板 = 固定财神

using HandCount = std::array<int, kTileKinds>;

inline bool IsValidTile(TileId t) { return t >= 0 && t < kTileKinds; }
inline bool IsBai(TileId t) { return t == kBai; }
inline bool IsCaishenFixedBai(TileId t) { return IsBai(t); }

inline bool IsWan(TileId t) { return t >= 0 && t <= 8; }
inline bool IsTiao(TileId t) { return t >= 9 && t <= 17; }
inline bool IsTong(TileId t) { return t >= 18 && t <= 26; }
inline bool IsFeng(TileId t) { return t >= 27 && t <= 30; }
inline bool IsJian(TileId t) { return t >= 31 && t <= 33; }
inline bool IsNumbered(TileId t) { return IsWan(t) || IsTiao(t) || IsTong(t); }

inline int SuitBase(TileId t) {
  if (IsWan(t)) return 0;
  if (IsTiao(t)) return 9;
  if (IsTong(t)) return 18;
  return -1;
}

inline int RankInSuit(TileId t) {
  const int base = SuitBase(t);
  return base < 0 ? -1 : (t - base);  // 0..8 for 1..9
}

HandCount ZeroHand();
void AddTile(HandCount& h, TileId t, int n = 1);
bool RemoveTile(HandCount& h, TileId t, int n = 1);
int HandSize(const HandCount& h);
HandCount CountTiles(const std::vector<TileId>& tiles);

// 136 tiles: 4 of each kind 0..33
std::vector<TileId> BuildWall();

}  // namespace hzmj
}  // namespace pandora
