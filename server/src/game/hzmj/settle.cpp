#include "game/hzmj/settle.hpp"

#include <sstream>

namespace pandora {
namespace hzmj {

bool CanDianpao(const HzmjConfig& cfg, int N, int shooter_seat, int winner_seat, int banker_seat) {
  if (!cfg.sanlao_dianpao || N < 8) return false;
  if (shooter_seat < 0 || winner_seat < 0) return false;
  const bool shooter_banker = shooter_seat == banker_seat;
  const bool winner_banker = winner_seat == banker_seat;
  if (!shooter_banker && !winner_banker && !cfg.xian_xian_dianpao) return false;
  return true;
}

int ResolveContractor(const SettleInput& in) {
  const int w = in.winner_seat;
  // Prefer: someone ate winner 3 times
  for (int s = 0; s < 4; ++s) {
    if (s == w) continue;
    if (in.tan_count[static_cast<size_t>(s)][static_cast<size_t>(w)] >= 3) return s;
  }
  // Winner ate someone 3 times -> that someone contracts
  for (int s = 0; s < 4; ++s) {
    if (s == w) continue;
    if (in.tan_count[static_cast<size_t>(w)][static_cast<size_t>(s)] >= 3) return s;
  }
  return -1;
}

static int64_t PayFactor(int payer, int winner, int banker, int N) {
  if (winner == banker) return N;  // each xian pays N
  if (payer == banker) return N;   // banker pays N to xian winner
  return 1;
}

SettlePlan BuildSettle(const SettleInput& in) {
  SettlePlan p;
  p.stake = in.base_score * in.M;
  p.deltas.fill(0);
  p.contractor_seat = ResolveContractor(in);

  auto apply_zimo_shares = [&](std::array<int64_t, 4>& due_from) {
    due_from.fill(0);
    for (int s = 0; s < 4; ++s) {
      if (s == in.winner_seat) continue;
      const int64_t pay = static_cast<int64_t>(p.stake) * PayFactor(s, in.winner_seat, in.banker_seat, in.N);
      due_from[static_cast<size_t>(s)] = pay;
    }
  };

  std::array<int64_t, 4> due{};
  if (in.is_zimo || in.shooter_seat < 0) {
    apply_zimo_shares(due);
  } else {
    // dianpao: shooter pays sum of zimo shares
    apply_zimo_shares(due);
    int64_t total = 0;
    for (int s = 0; s < 4; ++s) total += due[static_cast<size_t>(s)];
    due.fill(0);
    due[static_cast<size_t>(in.shooter_seat)] = total;
  }

  if (p.contractor_seat >= 0) {
    int64_t total = 0;
    for (int s = 0; s < 4; ++s) total += due[static_cast<size_t>(s)];
    due.fill(0);
    due[static_cast<size_t>(p.contractor_seat)] = total;
  }

  for (int s = 0; s < 4; ++s) {
    p.deltas[static_cast<size_t>(s)] -= due[static_cast<size_t>(s)];
    p.deltas[static_cast<size_t>(in.winner_seat)] += due[static_cast<size_t>(s)];
  }
  return p;
}

std::string IdemSettleKey(int64_t round_id, int64_t uid) {
  std::ostringstream oss;
  oss << "hzmj:settle:" << round_id << ":" << uid;
  return oss.str();
}

}  // namespace hzmj
}  // namespace pandora
