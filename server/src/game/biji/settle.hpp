#pragma once

#include "game/biji/hand.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace pandora {
namespace biji {

enum class ChixiType : int8_t {
  kNone = 0,
  kTripleThree = 1,
  kTripleSf = 2,
  kTripleFlush = 3,
  kTripleStraight = 4,
  kAllBlack = 5,
  kAllRed = 6,
};

struct SeatArrange {
  DunCards head{};
  DunCards mid{};
  DunCards tail{};
};

struct ChixiHit {
  ChixiType type{ChixiType::kNone};
  int32_t mult{0};
};

struct SeatSettle {
  int64_t uid{0};
  int64_t dun_delta[3]{0, 0, 0};
  int64_t chixi_delta{0};
  int64_t gross{0};
  int64_t rake{0};
  int64_t net{0};
  int place[3]{0, 0, 0};  // 1-based place per dun
  std::vector<ChixiHit> chixi;
};

struct SettlePlan {
  std::vector<SeatSettle> seats;
};

inline int32_t PlaceMult(int place) { return 2 * (place - 1); }

inline int64_t ApplyRake(int64_t gross, int rake_bp, int64_t* rake_out) {
  if (gross <= 0 || rake_bp <= 0) {
    if (rake_out) *rake_out = 0;
    return gross;
  }
  const int64_t rake = (gross * rake_bp) / 10000;
  if (rake_out) *rake_out = rake;
  return gross - rake;
}

// Detect chixi types for one seat (triple tier + optional color).
std::vector<ChixiHit> DetectChixi(const SeatArrange& arr, bool enable);

// n seats; arranges[i] for seat i; base_score B; rake_bp.
SettlePlan ComputeSettle(const std::vector<SeatArrange>& arranges,
                         const std::vector<int64_t>& uids, int64_t base_score, int rake_bp,
                         bool enable_chixi);

// Single-dun deltas only (for unit tests). Winner collects from others.
void SettleOneDun(const std::vector<DunCards>& duns, int64_t base_score,
                  std::vector<int64_t>* deltas, std::vector<int>* places);

}  // namespace biji
}  // namespace pandora
