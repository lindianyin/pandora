#pragma once

#include <cstdint>

namespace pandora {
namespace fish {

// cost = M * B
inline int64_t FireCost(int32_t mult, int32_t base_score) {
  return static_cast<int64_t>(mult) * static_cast<int64_t>(base_score);
}

// rake = floor(cost * rake_bp / 10000)
inline int64_t FireRake(int64_t cost, int32_t rake_bp) {
  if (cost <= 0 || rake_bp <= 0) return 0;
  return (cost * static_cast<int64_t>(rake_bp)) / 10000;
}

// reward = min(S * B, max_catch_reward)
inline int64_t CatchReward(int32_t score, int32_t base_score, int64_t max_catch_reward) {
  const int64_t raw = static_cast<int64_t>(score) * static_cast<int64_t>(base_score);
  if (max_catch_reward > 0 && raw > max_catch_reward) return max_catch_reward;
  return raw;
}

// p = clamp(M / S, p_min, p_max); S<=0 -> p_min
inline double CatchProb(int32_t mult, int32_t score, double p_min, double p_max) {
  double p = p_min;
  if (score > 0) p = static_cast<double>(mult) / static_cast<double>(score);
  if (p < p_min) p = p_min;
  if (p > p_max) p = p_max;
  return p;
}

// dmg = max(1, M)
inline int32_t HpDamage(int32_t mult) {
  return mult < 1 ? 1 : mult;
}

}  // namespace fish
}  // namespace pandora
