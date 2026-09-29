#pragma once

#include "game/hzmj/tiles.hpp"

#include <optional>
#include <vector>

namespace pandora {
namespace hzmj {

enum class MeldType { kChi, kPeng, kMingGang, kAnGang, kBuGang };

struct Meld {
  MeldType type{MeldType::kChi};
  TileId tile{kTileInvalid};           // peng/gang tile; chi: lowest of the three
  std::array<TileId, 3> chi_tiles{};   // the three tiles for chi
  int from_seat{-1};
};

struct ChiOption {
  std::array<TileId, 2> hand_tiles{};  // two tiles from hand
  std::array<TileId, 3> formed{};     // ascending three
};

// discarder_seat relative: chi only if claimer is next of discarder (caller checks seats).
bool CanPeng(const HandCount& hand, TileId discard);
bool CanMingGang(const HandCount& hand, TileId discard);
bool CanAnGang(const HandCount& hand, TileId tile);
bool CanBuGang(const HandCount& hand, const std::vector<Meld>& melds, TileId tile);

// Returns possible chi compositions using hand + discard (discard must not be caishen).
std::vector<ChiOption> ListChiOptions(const HandCount& hand, TileId discard);
bool CanChi(const HandCount& hand, TileId discard);

// Caishen (fixed bai) cannot be claimed from discard.
bool CanMeldDiscard(TileId discard);

HandCount ApplyPeng(HandCount hand, TileId discard);
HandCount ApplyMingGang(HandCount hand, TileId discard);
HandCount ApplyAnGang(HandCount hand, TileId tile);
HandCount ApplyChi(HandCount hand, const ChiOption& opt);

}  // namespace hzmj
}  // namespace pandora
