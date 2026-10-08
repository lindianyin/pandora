#pragma once

#include "game/phz/config.hpp"
#include "game/phz/hu.hpp"
#include "game/phz/meld.hpp"
#include "game/phz/settle.hpp"
#include "game/phz/tiles.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

namespace pandora {
namespace phz {

enum class Phase { kWaitReady, kDeal, kPlay, kSettle, kLiuJu };

enum class PlaySub {
  kDiscard,
  kClaimWindow,
  kDrawJudge,
};

enum class ActionKind { kPass, kChi, kPeng, kHu };

struct OutEvent {
  std::string type;
  int seat{-1};
  TileId tile{kTileInvalid};
  std::string detail;
  std::vector<TileId> tiles;
  int from_seat{-1};
  int meld_kind{0};
  int action{0};
};

struct SeatState {
  int64_t uid{0};
  HandCount hand{};
  std::vector<Meld> melds;
  int pao_ti_count{0};
  std::unordered_set<TileId> lou_chi;
  bool trusteeship{false};
};

class PhzTable {
 public:
  using Rng = std::function<int(int n)>;
  using Sink = std::function<void(const OutEvent&)>;

  PhzTable(PhzConfig cfg, std::array<int64_t, kSeats> uids, int banker_seat);

  void SetRng(Rng rng) { rng_ = std::move(rng); }
  void SetSink(Sink sink) { sink_ = std::move(sink); }
  void SetWallForTest(std::vector<TileId> wall);
  void SetHandForTest(int seat, HandCount hand);
  void SetMeldsForTest(int seat, std::vector<Meld> melds);
  void SetPaoTiCountForTest(int seat, int n);
  /** Lab deal: seat1 ting Yi(0); banker discards Shi(19) then seat1 zimo. */
  void ApplyDebugQuickHuDeal();

  void Start();

  bool OnDiscard(int seat, TileId tile);
  bool OnAction(int seat, ActionKind act, const ChiOption* chi = nullptr);
  void OnTimeout(int seat);

  Phase phase() const { return phase_; }
  PlaySub sub() const { return sub_; }
  int turn_seat() const { return turn_seat_; }
  int banker_seat() const { return banker_seat_; }
  int wall_remain() const { return static_cast<int>(wall_.size()); }
  bool can_hu() const { return can_hu_; }
  bool SeatNeedsClaimInput(int seat) const;
  /** True if seat may declare hu on the pending discard/reveal tile. */
  bool SeatCanHu(int seat) const;
  const SeatState& seat(int s) const { return seats_[static_cast<size_t>(s)]; }
  const SettlePlan& last_settle() const { return last_settle_; }
  TileId last_discard() const { return last_discard_; }
  int last_discard_seat() const { return last_discard_seat_; }
  TileId last_reveal() const { return last_reveal_; }
  int last_reveal_seat() const { return last_reveal_seat_; }
  bool has_pending_reveal() const { return last_reveal_ != kTileInvalid && sub_ == PlaySub::kClaimWindow; }
  int last_hu_tile() const { return last_hu_tile_; }
  bool last_hu_draw() const { return last_hu_draw_; }

 private:
  void Emit(OutEvent e);
  void Deal();
  void BeginDiscard(int seat);
  void BeginDraw(int seat);
  void OpenClaimFromDiscard(int seat, TileId tile);
  void OpenClaimFromReveal(int seat, TileId tile);
  void ResolveClaimPassAll();
  void DoWei(int seat, TileId tile, bool chou);
  void DoPao(int seat, TileId tile, int from);
  void DoTi(int seat, TileId tile);
  void FinishHu(int seat, TileId tile, HuFrom from);
  void FinishLiuJu();
  void AfterPaoOrTi(int seat);
  bool HasPaoOrTi(int seat) const;
  TileId SmallestHandTile(int seat) const;
  /** True if seat has empty hand, exactly 7 table melds, and hu_xi >= min. */
  bool EmptyHandSevenMeldsHu(int seat) const;
  int NextSeat(int s) const { return (s + 1) % kSeats; }

  PhzConfig cfg_;
  std::array<SeatState, kSeats> seats_{};
  int banker_seat_{0};
  Phase phase_{Phase::kWaitReady};
  PlaySub sub_{PlaySub::kDiscard};
  int turn_seat_{0};
  std::vector<TileId> wall_;
  Rng rng_;
  Sink sink_;
  bool started_{false};
  bool can_hu_{false};

  TileId last_discard_{kTileInvalid};
  int last_discard_seat_{-1};
  TileId last_reveal_{kTileInvalid};
  int last_reveal_seat_{-1};
  bool claim_from_reveal_{false};

  std::array<bool, kSeats> claim_responded_{};
  std::array<ActionKind, kSeats> claim_choice_{};
  std::array<ChiOption, kSeats> claim_chi_{};

  SettlePlan last_settle_{};
  int last_hu_tile_{kTileInvalid};
  bool last_hu_draw_{false};
};

}  // namespace phz
}  // namespace pandora
