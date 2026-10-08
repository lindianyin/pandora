#pragma once

#include "game/phz/config.hpp"
#include "game/phz/hu.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace pandora {
namespace phz {

struct SettlePlan {
  int winner_seat{-1};
  TileId hu_tile{kTileInvalid};
  bool is_draw_win{false};
  int hu_xi{0};
  int tun{0};
  int fan{1};
  int ming_tang_mask{0};
  std::array<int64_t, kSeats> deltas{};
};

int ComputeTun(int xi, bool is_draw, bool is_tian, const PhzConfig& cfg);
SettlePlan BuildSettle(int winner, const HuResult& hu, const PhzConfig& cfg, int banker_seat);
std::string IdemSettleKey(int64_t round_id, int64_t uid);

}  // namespace phz
}  // namespace pandora
