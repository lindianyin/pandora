#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <random>

#include "game/fish/table.hpp"
#include "game/i_room_game.hpp"

namespace pandora {

class FishRoomGame : public IRoomGame {
 public:
  explicit FishRoomGame(RoomCtx& ctx);

  void Start() override;
  void Tick(std::chrono::steady_clock::time_point now) override;
  bool Handle(int64_t uid, uint32_t msg_id, const uint8_t* body, size_t len) override;
  void OnDisconnect(int64_t uid) override;
  void OnReconnect(int64_t uid) override;
  bool InProgress() const override;
  const char* ShortName() const override { return "fish"; }

 private:
  void OnEvent(const fish::OutEvent& ev);
  void ApplyPendingRewards();
  void SendSnapshot(int64_t uid);
  void BroadcastStart();
  void FinishIfEmpty();
  int64_t SeatGold(int64_t uid) const;

  RoomCtx& ctx_;
  std::unique_ptr<fish::FishTable> table_;
  int64_t round_id_{0};
  std::chrono::steady_clock::time_point last_tick_{};
  bool started_{false};
  bool done_{false};
  std::mt19937 rng_{std::random_device{}()};
};

}  // namespace pandora
