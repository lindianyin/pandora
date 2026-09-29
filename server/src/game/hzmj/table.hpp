#pragma once

#include "game/hzmj/config.hpp"
#include "game/hzmj/hu.hpp"
#include "game/hzmj/meld.hpp"
#include "game/hzmj/settle.hpp"
#include "game/hzmj/tiles.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace pandora {
namespace hzmj {

enum class Phase { kWaitReady, kDeal, kPlay, kSettle, kLiuJu };

enum class PlaySub {
  kDiscard,      // current seat must discard (already has drawn / dealt)
  kClaimWindow,  // waiting responses to discard
  kPiaoLock,     // piao active: only piao seat will draw next
};

enum class ActionKind { kPass, kChi, kPeng, kGang, kHu };

struct OutEvent {
  std::string type;
  int seat{-1};
  TileId tile{kTileInvalid};
  std::string detail;
  std::vector<TileId> tiles;  // meld faces for Chi/Peng/Gang
  int from_seat{-1};
};

struct SeatState {
  int64_t uid{0};
  HandCount hand{};
  std::vector<Meld> melds;
  bool baotou_ting{false};
  int piao_level{0};
  std::array<int, 4> tan_count{};  // tan_count[from]
  std::vector<TileId> lou_hu;
  int gang_chain{0};
};

class HzmjTable {
 public:
  using Rng = std::function<int(int n)>;  // return [0,n)
  using Sink = std::function<void(const OutEvent&)>;

  HzmjTable(HzmjConfig cfg, std::array<int64_t, 4> uids, int banker_seat, int lian_zhuang);

  void SetRng(Rng rng) { rng_ = std::move(rng); }
  void SetSink(Sink sink) { sink_ = std::move(sink); }
  void SetWallForTest(std::vector<TileId> wall);
  void SetHandForTest(int seat, HandCount hand);
  // 调试局：seat0 听东；庄为 seat1，开局打出东供点炮；并开三牢 N=8
  void ApplyDebugDianpaoDeal();

  void Start();  // deal + enter play (banker discards first, no draw)

  bool OnDiscard(int seat, TileId tile);
  bool OnAction(int seat, ActionKind act, const ChiOption* chi = nullptr);
  bool OnAnGang(int seat, TileId tile);
  bool OnBuGang(int seat, TileId tile);
  void OnTimeout(int seat);

  Phase phase() const { return phase_; }
  PlaySub sub() const { return sub_; }
  int turn_seat() const { return turn_seat_; }
  int banker_seat() const { return banker_seat_; }
  int lian_zhuang() const { return lian_zhuang_; }
  int wall_remain() const { return static_cast<int>(wall_.size()); }
  int CurrentN() const { return ComputeN(lian_zhuang_, cfg_.start_as_sanlao); }
  bool SeatNeedsClaimInput(int seat) const;
  const SeatState& seat(int s) const { return seats_[static_cast<size_t>(s)]; }
  const SettlePlan& last_settle() const { return last_settle_; }
  bool piao_active() const { return piao_seat_ >= 0; }
  int piao_seat() const { return piao_seat_; }
  TileId last_discard() const { return last_discard_; }
  int last_discard_seat() const { return last_discard_seat_; }

 private:
  void Emit(OutEvent e);
  void ShuffleWall();
  void DealTiles();
  TileId DrawOne();
  void EnterDiscard(int seat, bool after_draw);
  void OpenClaimWindow();
  void ResolveClaims();
  void FinishHu(int winner, TileId win_tile, bool zimo, int shooter);
  void FinishLiuJu();
  void NextTurnAfterDiscard();
  bool IsCaishen(TileId t) const { return IsCaishenFixedBai(t); }

  HzmjConfig cfg_;
  std::array<SeatState, 4> seats_{};
  std::vector<TileId> wall_;
  Phase phase_{Phase::kWaitReady};
  PlaySub sub_{PlaySub::kDiscard};
  int banker_seat_{0};
  int lian_zhuang_{1};
  int turn_seat_{0};
  int round_draw_seat_{-1};
  TileId last_discard_{kTileInvalid};
  int last_discard_seat_{-1};
  int piao_seat_{-1};
  std::array<ActionKind, 4> claims_{};
  std::array<ChiOption, 4> claim_chi_{};
  bool claims_ready_[4]{};
  SettlePlan last_settle_{};
  Rng rng_;
  Sink sink_;
  int64_t round_id_{1};
};

}  // namespace hzmj
}  // namespace pandora
