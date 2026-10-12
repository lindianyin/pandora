#pragma once

#include <cstdint>

namespace pandora {
namespace sss {

struct SssConfig {
  int64_t base_score{100};
  int players{4};  // 2..4
  int rake_bp{0};
  int arrange_timeout_s{60};
  bool enable_special{true};
  bool enable_shoot{true};
  bool enable_home_run{true};
};

inline SssConfig DefaultConfig(int players = 4) {
  SssConfig c;
  c.players = players;
  return c;
}

}  // namespace sss
}  // namespace pandora
