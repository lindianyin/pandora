#pragma once

#include "game/sss/config.hpp"
#include "game/sss/hand.hpp"
#include "game/sss/settle.hpp"
#include "game/sss/special.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace pandora {
namespace sss {

enum class Phase { kWaitReady, kDeal, kArrange, kCompare, kSettle };

struct SeatState {
  int64_t uid{0};
  bool ready{false};
  bool online{true};
  bool trusteeship{false};
  bool locked{false};
  Hand13 hand{};
  HeadCards draft_head{};
  FiveCards draft_mid{};
  FiveCards draft_tail{};
  bool has_draft{false};
  bool report_special{false};
  SpecialType special{SpecialType::kNone};
  HeadCards final_head{};
  FiveCards final_mid{};
  FiveCards final_tail{};
};

struct Snapshot {
  Phase phase{Phase::kWaitReady};
  int n{0};
  int deal_start{0};
  int64_t remain_ms{0};
  Hand13 hand{};
  HeadCards draft_head{};
  FiveCards draft_mid{};
  FiveCards draft_tail{};
  bool has_draft{false};
  bool locked{false};
  bool report_special{false};
  SpecialType special{SpecialType::kNone};
  std::vector<bool> others_locked;
  std::vector<bool> others_reported;
};

class SssTable {
 public:
  using Rng = std::function<uint32_t()>;

  SssTable(SssConfig cfg, Rng rng);

  void SetSeatUid(int seat, int64_t uid);
  void OnReady(int seat, bool ready);
  bool TryStartDeal(int64_t now_ms);
  // Returns 0 ok, or Err code as int (7000001..)
  int SetArrange(int seat, const HeadCards& head, const FiveCards& mid, const FiveCards& tail,
                 bool report_special, bool confirm);
  void OnDisconnect(int seat);
  void OnReconnect(int seat);
  void OnArrangeDeadline(int64_t now_ms);
  Snapshot BuildSnapshot(int seat, int64_t now_ms) const;
  bool TakeSettlement(SettlePlan* out);

  Phase phase() const { return phase_; }
  const SssConfig& cfg() const { return cfg_; }
  int n() const { return n_; }
  int deal_start() const { return deal_start_; }
  int last_water_winner() const { return last_water_winner_; }
  int64_t arrange_deadline_ms() const { return arrange_deadline_ms_; }
  const SeatState& seat(int s) const { return seats_[static_cast<size_t>(s)]; }
  const SettlePlan& plan() const { return plan_; }
  bool AllLocked() const;
  bool settlement_pending() const { return settlement_pending_; }

 private:
  void DealCards();
  void FinishCompareAndSettle();
  bool HandContainsArrange(const Hand13& hand, const HeadCards& head, const FiveCards& mid,
                           const FiveCards& tail) const;

  SssConfig cfg_;
  Rng rng_;
  int n_{4};
  Phase phase_{Phase::kWaitReady};
  int deal_start_{0};
  int last_water_winner_{-1};
  int64_t arrange_deadline_ms_{0};
  std::array<SeatState, 4> seats_{};
  SettlePlan plan_{};
  bool settlement_pending_{false};
};

}  // namespace sss
}  // namespace pandora
