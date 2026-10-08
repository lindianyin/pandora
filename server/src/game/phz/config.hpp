#pragma once

#include <string>

namespace pandora {
namespace phz {

struct PhzConfig {
  int base_score{100};
  int players{3};
  int min_hu_xi{15};
  bool dian_pao{false};
  bool force_wei{true};
  bool force_pao{true};
  bool force_ti{true};
  bool first_pao_ti_discard{true};
  std::string tun_formula{"xi15"};
  std::string zimo_mode{"tun_plus_1"};  // tun_plus_1 | fan_x2 | both
  std::string ming_tang_combine{"max_plus_zimo"};
  int fan_dian_hu{3};
  int fan_xiao_hong{2};
  int fan_da_hong{4};
  int fan_wu_hu{5};
  int fan_shi_ba_xiao{6};
  int fan_dui_dui{5};
  bool hong_hu_progressive{false};
  bool san_ti_wu_kan{true};
  bool cha_jiao{false};
  bool banker_double{false};
  bool lou_chi{true};
  int action_timeout_s{15};
  int rake_bp{0};
  bool auto_hu_on_draw{true};  // server auto-hu when draw can hu
};

// Bit flags for ming_tang_mask
constexpr int kMtDianHu = 1 << 0;
constexpr int kMtXiaoHong = 1 << 1;
constexpr int kMtDaHong = 1 << 2;
constexpr int kMtWuHu = 1 << 3;
constexpr int kMtShiBaXiao = 1 << 4;
constexpr int kMtDuiDui = 1 << 5;
constexpr int kMtSanTiWuKan = 1 << 6;

std::string ConfigSnapshotJson(const PhzConfig& c);

}  // namespace phz
}  // namespace pandora
