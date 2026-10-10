#include "game/biji/settle.hpp"

#include <algorithm>

namespace pandora {
namespace biji {
namespace {

bool DunIsFlushFamily(DunType t) {
  return t == DunType::kFlush || t == DunType::kStraightFlush;
}
bool DunIsStraightFamily(DunType t) {
  return t == DunType::kStraight || t == DunType::kStraightFlush;
}

}  // namespace

std::vector<ChixiHit> DetectChixi(const SeatArrange& arr, bool enable) {
  std::vector<ChixiHit> out;
  if (!enable) return out;
  const DunType th = static_cast<DunType>(EvalDun(arr.head).type);
  const DunType tm = static_cast<DunType>(EvalDun(arr.mid).type);
  const DunType tt = static_cast<DunType>(EvalDun(arr.tail).type);

  ChixiHit triple{};
  if (th == DunType::kThree && tm == DunType::kThree && tt == DunType::kThree) {
    triple = {ChixiType::kTripleThree, 12};
  } else if (th == DunType::kStraightFlush && tm == DunType::kStraightFlush &&
             tt == DunType::kStraightFlush) {
    triple = {ChixiType::kTripleSf, 10};
  } else if (DunIsFlushFamily(th) && DunIsFlushFamily(tm) && DunIsFlushFamily(tt)) {
    triple = {ChixiType::kTripleFlush, 6};
  } else if (DunIsStraightFamily(th) && DunIsStraightFamily(tm) && DunIsStraightFamily(tt)) {
    triple = {ChixiType::kTripleStraight, 6};
  }
  if (triple.type != ChixiType::kNone) out.push_back(triple);

  bool all_red = true;
  bool all_black = true;
  const CardId* all[3] = {arr.head.data(), arr.mid.data(), arr.tail.data()};
  for (int d = 0; d < 3; ++d) {
    for (int i = 0; i < 3; ++i) {
      if (!IsRed(all[d][i])) all_red = false;
      if (!IsBlack(all[d][i])) all_black = false;
    }
  }
  if (all_red) out.push_back({ChixiType::kAllRed, 4});
  if (all_black) out.push_back({ChixiType::kAllBlack, 4});
  return out;
}

void SettleOneDun(const std::vector<DunCards>& duns, int64_t base_score,
                  std::vector<int64_t>* deltas, std::vector<int>* places) {
  const int n = static_cast<int>(duns.size());
  deltas->assign(n, 0);
  places->assign(n, 0);
  std::vector<int> order(n);
  for (int i = 0; i < n; ++i) order[i] = i;
  std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
    const int c = CompareDun(duns[a], duns[b]);
    if (c != 0) return c > 0;
    return a < b;
  });
  for (int p = 0; p < n; ++p) (*places)[order[p]] = p + 1;
  const int winner = order[0];
  for (int p = 1; p < n; ++p) {
    const int seat = order[p];
    const int64_t pay = static_cast<int64_t>(PlaceMult(p + 1)) * base_score;
    (*deltas)[seat] -= pay;
    (*deltas)[winner] += pay;
  }
}

SettlePlan ComputeSettle(const std::vector<SeatArrange>& arranges,
                         const std::vector<int64_t>& uids, int64_t base_score, int rake_bp,
                         bool enable_chixi) {
  SettlePlan plan;
  const int n = static_cast<int>(arranges.size());
  plan.seats.resize(n);
  for (int i = 0; i < n; ++i) {
    plan.seats[i].uid = i < static_cast<int>(uids.size()) ? uids[i] : 0;
  }

  for (int d = 0; d < 3; ++d) {
    std::vector<DunCards> duns(n);
    for (int i = 0; i < n; ++i) {
      if (d == 0) duns[i] = arranges[i].head;
      else if (d == 1) duns[i] = arranges[i].mid;
      else duns[i] = arranges[i].tail;
    }
    std::vector<int64_t> deltas;
    std::vector<int> places;
    SettleOneDun(duns, base_score, &deltas, &places);
    for (int i = 0; i < n; ++i) {
      plan.seats[i].dun_delta[d] = deltas[i];
      plan.seats[i].place[d] = places[i];
    }
  }

  for (int i = 0; i < n; ++i) {
    auto hits = DetectChixi(arranges[i], enable_chixi);
    plan.seats[i].chixi = hits;
    for (const auto& h : hits) {
      const int64_t pay = static_cast<int64_t>(h.mult) * base_score;
      for (int j = 0; j < n; ++j) {
        if (j == i) continue;
        plan.seats[j].chixi_delta -= pay;
        plan.seats[i].chixi_delta += pay;
      }
    }
  }

  for (int i = 0; i < n; ++i) {
    auto& s = plan.seats[i];
    s.gross = s.dun_delta[0] + s.dun_delta[1] + s.dun_delta[2] + s.chixi_delta;
    s.net = ApplyRake(s.gross, rake_bp, &s.rake);
  }
  return plan;
}

}  // namespace biji
}  // namespace pandora
