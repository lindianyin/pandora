#pragma once

#include "game/hzmj/config.hpp"
#include "game/hzmj/hu.hpp"

#include <array>
#include <string>

namespace pandora {
namespace hzmj {

struct SettleInput {
  int winner_seat{0};
  int banker_seat{0};
  int N{2};
  int M{1};
  int base_score{100};
  bool is_zimo{true};
  int shooter_seat{-1};  // dianpao
  // tan_count[actor][from] >= 3 triggers contractor
  std::array<std::array<int, 4>, 4> tan_count{};
};

struct SettlePlan {
  int stake{0};
  int contractor_seat{-1};
  std::array<int64_t, 4> deltas{};  // gold delta per seat
};

bool CanDianpao(const HzmjConfig& cfg, int N, int shooter_seat, int winner_seat, int banker_seat);

int ResolveContractor(const SettleInput& in);

SettlePlan BuildSettle(const SettleInput& in);

std::string IdemSettleKey(int64_t round_id, int64_t uid);

}  // namespace hzmj
}  // namespace pandora
