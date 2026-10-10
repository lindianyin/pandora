#pragma once

#include "game/biji/config.hpp"
#include "game/biji/hand.hpp"
#include "game/biji/settle.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace pandora {
namespace biji {

enum class Phase { kWaitReady, kDeal, kArrange, kCompare, kSettle };

struct SeatState {
  int64_t uid{0};
  bool ready{false};
  bool online{true};
  bool trusteeship{false};
  bool locked{false};
  Hand9 hand{};
  DunCards draft_head{};
  DunCards draft_mid{};
  DunCards draft_tail{};
  bool has_draft{false};
  DunCards final_head{};
  DunCards final_mid{};
  DunCards final_tail{};
};

struct Snapshot {
  Phase phase{Phase::kWaitReady};
  int n{0};
  int deal_start{0};
  int64_t remain_ms{0};
  Hand9 hand{};
  DunCards draft_head{};
  DunCards draft_mid{};
  DunCards draft_tail{};
  bool has_draft{false};
  bool locked{false};
  std::vector<bool> others_locked;
};

class BijiTable {
 public:
  using Rng = std::function<uint32_t()>;

  BijiTable(BijiConfig cfg, Rng rng);

  void SetSeatUid(int seat, int64_t uid);
  void OnReady(int seat, bool ready);
  bool TryStartDeal(int64_t now_ms);
  // Returns 0 ok, or Err code as int (6000001..)
  int SetArrange(int seat, const DunCards& head, const DunCards& mid, const DunCards& tail,
                 bool confirm);
  void OnDisconnect(int seat);
  void OnReconnect(int seat);
  void OnArrangeDeadline(int64_t now_ms);
  Snapshot BuildSnapshot(int seat, int64_t now_ms) const;
  // If a settlement was just produced, copy it and clear the pending flag.
  bool TakeSettlement(SettlePlan* out);

  Phase phase() const { return phase_; }
  const BijiConfig& cfg() const { return cfg_; }
  int n() const { return n_; }
  int deal_start() const { return deal_start_; }
  int last_tail_winner() const { return last_tail_winner_; }
  int64_t arrange_deadline_ms() const { return arrange_deadline_ms_; }
  const SeatState& seat(int s) const { return seats_[static_cast<size_t>(s)]; }
  const SettlePlan& plan() const { return plan_; }
  bool AllLocked() const;
  bool settlement_pending() const { return settlement_pending_; }

 private:
  void DealCards();
  void FinishCompareAndSettle();
  bool HandContainsArrange(const Hand9& hand, const DunCards& head, const DunCards& mid,
                           const DunCards& tail) const;

  BijiConfig cfg_;
  Rng rng_;
  int n_{4};
  Phase phase_{Phase::kWaitReady};
  int deal_start_{0};
  int last_tail_winner_{-1};
  int64_t arrange_deadline_ms_{0};
  std::array<SeatState, 4> seats_{};
  SettlePlan plan_{};
  bool settlement_pending_{false};
};

}  // namespace biji
}  // namespace pandora
