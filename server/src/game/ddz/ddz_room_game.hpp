#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "common/config.hpp"
#include "game/ddz_cards.hpp"
#include "game/i_room_game.hpp"

namespace pandora {

class DdzClassicSimple {
 public:
  DdzClassicSimple(RoomCtx& ctx, std::array<int64_t, 3> uids);

  void Start();
  void OnBid(int seat, int score);
  void OnPlay(int seat, bool pass, const std::vector<int>& cards);
  void Tick(std::chrono::steady_clock::time_point now);

  bool Finished() const { return finished_; }
  int64_t RoundId() const { return round_id_; }
  const std::string& Phase() const { return phase_; }
  int CurrentSeat() const { return current_seat_; }
  int LandlordSeat() const { return landlord_; }
  int TimeoutLeftS(std::chrono::steady_clock::time_point now) const;
  std::vector<int> HandOf(int seat) const;
  void SendReconnectSnapshot(int64_t uid);

 private:
  void BroadcastTurn();
  void DealAndBid();
  void FinishBid();
  void DoSettle(bool landlord_win);
  void AutoActIfTrusted(int seat);
  std::vector<int64_t> AllUids() const;

  RoomCtx& ctx_;
  int64_t room_id_;
  std::array<int64_t, 3> uids_{};
  GameConfig cfg_;
  std::string phase_{"Deal"};
  std::array<std::vector<int>, 3> hands_{};
  std::vector<int> bottom_;
  int landlord_{-1};
  int bid_score_{0};
  int current_seat_{0};
  int bids_made_{0};
  int redeal_{0};
  int bomb_count_{0};
  bool spring_{true};
  int last_play_seat_{-1};
  ddz::Pattern last_pattern_{};
  std::vector<int> last_cards_;
  int passes_{0};
  int64_t round_id_{0};
  std::chrono::steady_clock::time_point deadline_;
  bool finished_{false};
  int play_count_non_landlord_{0};
};

class DdzRoomGame : public IRoomGame {
 public:
  explicit DdzRoomGame(RoomCtx& ctx);

  void Start() override;
  void Tick(std::chrono::steady_clock::time_point now) override;
  bool Handle(int64_t uid, uint32_t msg_id, const uint8_t* body, size_t len) override;
  void OnDisconnect(int64_t uid) override;
  void OnReconnect(int64_t uid) override;
  bool InProgress() const override;
  const char* ShortName() const override { return "ddz"; }

 private:
  int SeatOf(int64_t uid) const;

  RoomCtx& ctx_;
  std::unique_ptr<DdzClassicSimple> impl_;
};

}  // namespace pandora
