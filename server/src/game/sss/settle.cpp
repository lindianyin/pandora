#include "game/sss/settle.hpp"

#include <algorithm>

namespace pandora {
namespace sss {

int32_t DunWaterHead(int8_t level) {
  if (level == static_cast<int8_t>(FiveLevel::kThree)) return 3;
  return 1;
}

int32_t DunWaterMid(int8_t level) {
  if (level == static_cast<int8_t>(FiveLevel::kStraightFlush)) return 10;
  if (level == static_cast<int8_t>(FiveLevel::kFour)) return 8;
  if (level == static_cast<int8_t>(FiveLevel::kFullHouse)) return 2;
  return 1;
}

int32_t DunWaterTail(int8_t level) {
  if (level == static_cast<int8_t>(FiveLevel::kStraightFlush)) return 5;
  if (level == static_cast<int8_t>(FiveLevel::kFour)) return 4;
  return 1;
}

SettlePlan ComputeSettle(const std::vector<SeatArrange>& arranges, const std::vector<int64_t>& uids,
                         const SssConfig& cfg) {
  SettlePlan plan;
  const int n = static_cast<int>(arranges.size());
  plan.seats.resize(n);
  std::vector<int32_t> water(n, 0);
  std::vector<bool> is_special(n, false);
  std::vector<SpecialType> stype(n, SpecialType::kNone);

  for (int i = 0; i < n; ++i) {
    plan.seats[i].uid = i < static_cast<int>(uids.size()) ? uids[i] : 0;
    if (arranges[i].dao_shui) {
      is_special[i] = false;
      stype[i] = SpecialType::kNone;
      continue;
    }
    if (arranges[i].report_special && arranges[i].special != SpecialType::kNone &&
        cfg.enable_special) {
      is_special[i] = true;
      stype[i] = arranges[i].special;
      plan.seats[i].special = stype[i];
    }
  }

  // Dao shui pay
  for (int i = 0; i < n; ++i) {
    if (!arranges[i].dao_shui) continue;
    const int32_t shoot_mult = cfg.enable_shoot ? 2 : 1;
    const int32_t pay = (3 + 10 + 5) * shoot_mult;
    for (int j = 0; j < n; ++j) {
      if (j == i) continue;
      water[i] -= pay;
      water[j] += pay;
    }
  }

  // Special vs special / special vs ordinary
  for (int i = 0; i < n; ++i) {
    if (!is_special[i]) continue;
    for (int j = i + 1; j < n; ++j) {
      if (!is_special[j]) continue;
      if (static_cast<int>(stype[i]) > static_cast<int>(stype[j])) {
        const int32_t w = SpecialWater(stype[i]);
        water[i] += w;
        water[j] -= w;
      } else if (static_cast<int>(stype[i]) < static_cast<int>(stype[j])) {
        const int32_t w = SpecialWater(stype[j]);
        water[j] += w;
        water[i] -= w;
      }
    }
  }
  for (int i = 0; i < n; ++i) {
    if (!is_special[i]) continue;
    const int32_t w = SpecialWater(stype[i]);
    for (int j = 0; j < n; ++j) {
      if (is_special[j] || arranges[j].dao_shui) continue;
      water[i] += w;
      water[j] -= w;
    }
  }

  // Ordinary pairwise
  std::vector<std::vector<int32_t>> pair_water(n, std::vector<int32_t>(n, 0));
  std::vector<std::vector<bool>> shoot(n, std::vector<bool>(n, false));

  for (int i = 0; i < n; ++i) {
    if (is_special[i] || arranges[i].dao_shui) continue;
    for (int j = i + 1; j < n; ++j) {
      if (is_special[j] || arranges[j].dao_shui) continue;
      int wins_i = 0;
      int wins_j = 0;
      int32_t sum_i = 0;

      auto apply_dun = [&](int cmp, int32_t w) {
        if (cmp == 0) return;
        if (cmp > 0) {
          sum_i += w;
          ++wins_i;
        } else {
          sum_i -= w;
          ++wins_j;
        }
      };

      const int ch = CompareHead(arranges[i].head, arranges[j].head);
      if (ch > 0)
        apply_dun(1, DunWaterHead(EvalHead(arranges[i].head).level));
      else if (ch < 0)
        apply_dun(-1, DunWaterHead(EvalHead(arranges[j].head).level));

      const int cm = CompareFive(arranges[i].mid, arranges[j].mid);
      if (cm > 0)
        apply_dun(1, DunWaterMid(EvalFive(arranges[i].mid).level));
      else if (cm < 0)
        apply_dun(-1, DunWaterMid(EvalFive(arranges[j].mid).level));

      const int ct = CompareFive(arranges[i].tail, arranges[j].tail);
      if (ct > 0)
        apply_dun(1, DunWaterTail(EvalFive(arranges[i].tail).level));
      else if (ct < 0)
        apply_dun(-1, DunWaterTail(EvalFive(arranges[j].tail).level));

      if (cfg.enable_shoot && wins_i == 3) {
        sum_i *= 2;
        shoot[i][j] = true;
        plan.shoots.push_back({i, j});
      } else if (cfg.enable_shoot && wins_j == 3) {
        sum_i *= 2;  // sum_i is negative from j's perspective wins
        shoot[j][i] = true;
        plan.shoots.push_back({j, i});
      }

      pair_water[i][j] = sum_i;
      pair_water[j][i] = -sum_i;
    }
  }

  // Home run
  if (cfg.enable_home_run && cfg.enable_shoot && n >= 3) {
    for (int i = 0; i < n; ++i) {
      if (is_special[i] || arranges[i].dao_shui) continue;
      int ordinary_opp = 0;
      int shoot_all = 0;
      for (int j = 0; j < n; ++j) {
        if (j == i || is_special[j] || arranges[j].dao_shui) continue;
        ++ordinary_opp;
        if (shoot[i][j]) ++shoot_all;
      }
      if (ordinary_opp >= 1 && shoot_all == ordinary_opp) {
        plan.seats[i].home_run = true;
        for (int j = 0; j < n; ++j) {
          if (j == i || is_special[j] || arranges[j].dao_shui) continue;
          pair_water[i][j] *= 2;
          pair_water[j][i] *= 2;
        }
      }
    }
  }

  for (int i = 0; i < n; ++i) {
    for (int j = i + 1; j < n; ++j) {
      if (is_special[i] || is_special[j] || arranges[i].dao_shui || arranges[j].dao_shui)
        continue;
      water[i] += pair_water[i][j];
      water[j] += pair_water[j][i];
    }
  }

  for (int i = 0; i < n; ++i) {
    plan.seats[i].gross_water = water[i];
    plan.seats[i].gross_gold = static_cast<int64_t>(water[i]) * cfg.base_score;
    plan.seats[i].net = ApplyRake(plan.seats[i].gross_gold, cfg.rake_bp, &plan.seats[i].rake);
  }
  return plan;
}

}  // namespace sss
}  // namespace pandora
