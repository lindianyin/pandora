#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace pandora {
namespace fish {

enum class FishKind : int32_t { kOdds = 0, kHp = 1 };

struct FishTypeDef {
  int32_t type_id{0};
  std::string name;
  int32_t score{10};
  FishKind kind{FishKind::kOdds};
  int32_t hp{0};
  int32_t weight{1};
  float radius{40.f};
  std::string special{"none"};
  bool enabled{true};
};

struct FishWaveDef {
  int32_t id{0};
  std::string name;
  int32_t duration_ms{60000};
  int32_t spawn_interval_ms{800};
  int32_t max_alive{30};
  int32_t boss_type_id{0};
  int32_t weight{1};
  bool enabled{true};
};

struct FishConfig {
  int32_t base_score{100};
  int32_t rake_bp{500};
  std::vector<int32_t> cannon_mults{1, 2, 5, 10, 20, 50, 100};
  int32_t fire_rate_hz{8};
  double p_min{0.01};
  double p_max{0.95};
  int64_t max_catch_reward{1000000};
  float bullet_speed{1200.f};
  float bullet_radius{8.f};
  int32_t disconnect_kick_ms{120000};
  int32_t tick_ms{50};
  int32_t scene_w{1920};
  int32_t scene_h{1080};
  int32_t hard_max_alive{80};
  int32_t default_ttl_ms{20000};
  std::vector<FishTypeDef> types;
  std::vector<FishWaveDef> waves;
};

inline FishConfig DefaultFishConfig() {
  FishConfig c;
  c.types = {
      {1, "small", 10, FishKind::kOdds, 0, 50, 30.f, "none", true},
      {2, "mid", 50, FishKind::kOdds, 0, 20, 45.f, "none", true},
      {3, "tank", 200, FishKind::kHp, 100, 5, 60.f, "none", true},
  };
  c.waves = {{1, "normal", 60000, 500, 20, 0, 1, true}};
  return c;
}

}  // namespace fish
}  // namespace pandora
