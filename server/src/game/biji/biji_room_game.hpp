#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <random>

#include "game/biji/table.hpp"
#include "game/i_room_game.hpp"

namespace pandora {

class BijiRoomGame : public IRoomGame {
 public:
  explicit BijiRoomGame(RoomCtx& ctx);

  void Start() override;
  void Tick(std::chrono::steady_clock::time_point now) override;
  bool Handle(int64_t uid, uint32_t msg_id, const uint8_t* body, size_t len) override;
  void OnDisconnect(int64_t uid) override;
  void OnReconnect(int64_t uid) override;
  bool InProgress() const override;
  const char* ShortName() const override { return "biji"; }

 private:
  void BroadcastStart();
  void BroadcastArrangeState();
  void ApplySettlementIfAny();
  void SendSnapshot(int64_t uid);
  int64_t NowMs() const;
  int64_t SeatGold(int64_t uid) const;

  RoomCtx& ctx_;
  std::unique_ptr<biji::BijiTable> table_;
  int64_t round_id_{0};
  bool done_{false};
  std::mt19937 rng_{std::random_device{}()};
  biji::SettlePlan last_plan_{};
  bool has_last_compare_{false};
};

}  // namespace pandora
