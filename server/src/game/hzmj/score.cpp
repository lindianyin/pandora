#include "game/hzmj/config.hpp"

namespace pandora {
namespace hzmj {

int ComputeN(int lian_zhuang, bool start_as_sanlao) {
  if (start_as_sanlao) return 8;
  if (lian_zhuang <= 1) return 2;
  if (lian_zhuang == 2) return 4;
  return 8;
}

int ComputeM(const HuResult& hu, int piao_level, int gang_chain) {
  if (!hu.ok) return 0;
  int m = 1;

  // caishen series (mutually exclusive highest)
  // Qi-dui baotou (qi ke) is handled in the qi-dui series, not here.
  int caishen_m = 1;
  if (piao_level >= 3) caishen_m = 16;
  else if (piao_level == 2) caishen_m = 8;
  else if (piao_level == 1) caishen_m = 4;
  else if (hu.baotou && hu.kind != HuKind::kQiDui) caishen_m = 2;
  m *= caishen_m;

  // gang chain
  int gang_m = 1;
  if (gang_chain >= 4) gang_m = 16;
  else if (gang_chain == 3) gang_m = 8;
  else if (gang_chain == 2) gang_m = 4;
  else if (gang_chain == 1) gang_m = 2;
  m *= gang_m;

  // qi dui series
  if (hu.kind == HuKind::kQiDui) {
    int qd = 2;
    if (hu.qing_qi_dui) {
      qd = 4;
      if (hu.haohua >= 3) qd = 32;
      else if (hu.haohua == 2) qd = 16;
      else if (hu.haohua == 1) qd = 8;
    } else {
      if (hu.haohua >= 3) qd = 16;
      else if (hu.haohua == 2) qd = 8;
      else if (hu.haohua == 1) qd = 4;
    }
    // 七客：七对爆头
    if (hu.baotou && piao_level == 0) {
      const int qike = 4;
      if (qike > qd) qd = qike;
    }
    m *= qd;
  }

  return m;
}

}  // namespace hzmj
}  // namespace pandora
