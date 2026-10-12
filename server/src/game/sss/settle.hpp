#pragma once

#include "game/sss/config.hpp"
#include "game/sss/hand.hpp"
#include "game/sss/special.hpp"

#include <cstdint>
#include <vector>

namespace pandora {
namespace sss {

struct SeatArrange {
  HeadCards head{};
  FiveCards mid{};
  FiveCards tail{};
  bool report_special{false};
  SpecialType special{SpecialType::kNone};
  bool dao_shui{false};  // defensive fallback
};

struct SeatSettle {
  int64_t uid{0};
  int32_t gross_water{0};
  int64_t gross_gold{0};
  int64_t rake{0};
  int64_t net{0};
  SpecialType special{SpecialType::kNone};
  bool home_run{false};
};

struct SettlePlan {
  std::vector<SeatSettle> seats;
  // shoot pairs: from -> to
  std::vector<std::pair<int, int>> shoots;
};

inline int64_t ApplyRake(int64_t gross, int rake_bp, int64_t* rake_out) {
  if (gross <= 0 || rake_bp <= 0) {
    if (rake_out) *rake_out = 0;
    return gross;
  }
  const int64_t rake = (gross * rake_bp) / 10000;
  if (rake_out) *rake_out = rake;
  return gross - rake;
}

int32_t DunWaterHead(int8_t level);
int32_t DunWaterMid(int8_t level);
int32_t DunWaterTail(int8_t level);

SettlePlan ComputeSettle(const std::vector<SeatArrange>& arranges, const std::vector<int64_t>& uids,
                         const SssConfig& cfg);

}  // namespace sss
}  // namespace pandora
