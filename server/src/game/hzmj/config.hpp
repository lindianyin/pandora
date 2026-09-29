#pragma once

#include "game/hzmj/hu.hpp"

namespace pandora {
namespace hzmj {

struct HzmjConfig {
  int base_score{100};
  bool sanlao_dianpao{true};
  bool xian_xian_dianpao{false};
  bool peng_counts_tan{true};
  bool start_as_sanlao{false};
  int max_piao{3};
  bool qiang_gang_hu{true};
  bool lou_hu{true};
  bool piao_block_an_gang{true};
};

// lian_zhuang: 1=平庄, 2=二连, >=3=三牢
int ComputeN(int lian_zhuang, bool start_as_sanlao);

// M from HuResult + piao_level (0..3) + gang_chain (0..4)
int ComputeM(const HuResult& hu, int piao_level, int gang_chain);

}  // namespace hzmj
}  // namespace pandora
