#pragma once

#include <cstdint>

namespace pandora {
namespace biji {

struct BijiConfig {
  int64_t base_score{100};
  int players{4};  // 2..4
  int rake_bp{0};
  int arrange_timeout_s{45};
  bool enable_chixi{true};
};

inline BijiConfig DefaultConfig(int players = 4) {
  BijiConfig c;
  c.players = players;
  return c;
}

}  // namespace biji
}  // namespace pandora
