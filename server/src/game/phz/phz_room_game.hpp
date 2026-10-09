#pragma once

#include <chrono>
#include <cstdint>
#include <memory>

#include "game/i_room_game.hpp"
#include "game/phz/table.hpp"

namespace pandora {

class PhzRoomGame : public IRoomGame {
 public:
  explicit PhzRoomGame(RoomCtx& ctx);

  void Start() override;
  void Tick(std::chrono::steady_clock::time_point now) override;
  bool Handle(int64_t uid, uint32_t msg_id, const uint8_t* body, size_t len) override;
  void OnDisconnect(int64_t uid) override;
  void OnReconnect(int64_t uid) override;
  bool InProgress() const override;
  const char* ShortName() const override { return "phz"; }

 private:
  void OnEvent(const phz::OutEvent& ev);
  void ApplySettle();
  void FinishRound();
  void ArmDeadline();

  RoomCtx& ctx_;
  std::unique_ptr<phz::PhzTable> table_;
  int64_t round_id_{0};
  std::chrono::steady_clock::time_point deadline_{};
  bool done_{false};
};

}  // namespace pandora
