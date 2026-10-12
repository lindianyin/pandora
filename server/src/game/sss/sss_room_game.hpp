#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <random>

#include "game/i_room_game.hpp"
#include "game/sss/table.hpp"

namespace pandora {

class SssRoomGame : public IRoomGame {
 public:
  explicit SssRoomGame(RoomCtx& ctx);

  void Start() override;
  void Tick(std::chrono::steady_clock::time_point now) override;
  bool Handle(int64_t uid, uint32_t msg_id, const uint8_t* body, size_t len) override;
  void OnDisconnect(int64_t uid) override;
  void OnReconnect(int64_t uid) override;
  bool InProgress() const override;
  const char* ShortName() const override { return "sss"; }

 private:
  void BroadcastStart();
  void BroadcastArrangeState();
  void ApplySettlementIfAny();
  void SendSnapshot(int64_t uid);
  int64_t NowMs() const;
  int64_t SeatGold(int64_t uid) const;

  RoomCtx& ctx_;
  std::unique_ptr<sss::SssTable> table_;
  int64_t round_id_{0};
  bool done_{false};
  std::mt19937 rng_{std::random_device{}()};
  sss::SettlePlan last_plan_{};
  bool has_last_compare_{false};
};

}  // namespace pandora
