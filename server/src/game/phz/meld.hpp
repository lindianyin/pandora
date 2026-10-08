#pragma once

#include "game/phz/tiles.hpp"

#include <vector>

namespace pandora {
namespace phz {

enum class MeldKind {
  kChi = 1,
  kPeng = 2,
  kWei = 3,
  kChouWei = 4,
  kPao = 5,
  kTi = 6,
  kJiao = 7,
  kKan = 8,  // in-hand triplet (may stay in hand)
};

struct Meld {
  MeldKind kind{MeldKind::kPeng};
  TileId tile{kTileInvalid};       // primary tile for peng/wei/pao/ti
  std::vector<TileId> tiles;       // full face
  int from_seat{-1};
};

struct ChiOption {
  std::vector<TileId> hand_tiles;  // two tiles from hand
};

int HuXiOfMeld(const Meld& m);
int HuXiOfKan(TileId t);  // in-hand kan
bool CanFormChi(const HandCount& hand, TileId claim, ChiOption* out);
bool CanPeng(const HandCount& hand, TileId claim);
std::vector<TileId> ChiTiles(TileId claim, const ChiOption& opt);

}  // namespace phz
}  // namespace pandora
