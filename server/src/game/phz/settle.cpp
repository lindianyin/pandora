#include "game/phz/settle.hpp"

#include <algorithm>
#include <sstream>

namespace pandora {
namespace phz {

int ComputeTun(int xi, bool is_draw, bool is_tian, const PhzConfig& cfg) {
  int tun = 1;
  if (is_tian) {
    tun = (xi * 2 - 12) / 3;
  } else {
    tun = (xi - 15) / 3 + 1;
  }
  if ((cfg.zimo_mode == "tun_plus_1" || cfg.zimo_mode == "both") && is_draw) tun += 1;
  return std::max(tun, 1);
}

SettlePlan BuildSettle(int winner, const HuResult& hu, const PhzConfig& cfg, int banker_seat) {
  SettlePlan p;
  p.winner_seat = winner;
  p.hu_tile = kTileInvalid;
  p.is_draw_win = hu.is_draw_win;
  p.hu_xi = hu.xi;
  p.ming_tang_mask = hu.ming_tang_mask;
  p.tun = ComputeTun(hu.xi, hu.is_draw_win, false, cfg);
  int fan = hu.fan > 0 ? hu.fan : ComputeFan(hu, cfg);
  if ((cfg.zimo_mode == "fan_x2" || cfg.zimo_mode == "both") && hu.is_draw_win) fan *= 2;
  p.fan = fan;
  const int64_t stake = static_cast<int64_t>(p.tun) * static_cast<int64_t>(fan) * static_cast<int64_t>(cfg.base_score);
  for (int s = 0; s < kSeats; ++s) {
    if (s == winner) continue;
    int64_t pay = stake;
    if (cfg.banker_double && (winner == banker_seat || s == banker_seat)) pay *= 2;
    p.deltas[static_cast<size_t>(s)] -= pay;
    p.deltas[static_cast<size_t>(winner)] += pay;
  }
  return p;
}

std::string IdemSettleKey(int64_t round_id, int64_t uid) {
  std::ostringstream os;
  os << "phz:settle:" << round_id << ":" << uid;
  return os.str();
}

}  // namespace phz
}  // namespace pandora
