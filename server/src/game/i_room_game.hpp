#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "common/config.hpp"

namespace pandora {

class AdminService;
class ActivityService;
class SocialService;
class WalletService;
class MemoryStore;

// Platform capabilities exposed to a running room game (no RoomManager dependency).
struct RoomCtx {
  int64_t room_id{0};
  int32_t template_id{1};
  int32_t game_id{0};
  int seat_count{3};
  GameConfig cfg{};

  WalletService* wallet{nullptr};
  AdminService* admin{nullptr};
  ActivityService* activity{nullptr};
  SocialService* social{nullptr};
  MemoryStore* store{nullptr};

  std::function<int64_t(int seat)> uid_of;
  std::function<int(int64_t uid)> seat_of;
  std::function<void(std::string phase)> set_phase;
  std::function<bool(int seat)> is_trusteeship;
  std::function<void(int seat, bool on)> set_trusteeship;
  std::function<void(int64_t uid, uint32_t msg_id, const std::vector<uint8_t>& body)> send;
  std::function<void()> push_room_state;
  // Called when the round ends; RoomManager clears logic and returns to WaitReady.
  std::function<void()> on_round_finished;

  // Cross-round carry (hzmj banker/lian, phz banker); owned by Room.
  int* carry_banker{nullptr};
  int* carry_lian{nullptr};
};

struct IRoomGame {
  virtual ~IRoomGame() = default;
  virtual void Start() = 0;
  virtual void Tick(std::chrono::steady_clock::time_point now) = 0;
  virtual bool Handle(int64_t uid, uint32_t msg_id, const uint8_t* body, size_t len) = 0;
  virtual void OnDisconnect(int64_t uid) = 0;
  virtual void OnReconnect(int64_t uid) = 0;
  virtual bool InProgress() const = 0;
  virtual const char* ShortName() const = 0;
};

struct GameMeta {
  int32_t game_id{0};
  const char* short_name{"game"};
  int default_seats{3};
  std::function<std::unique_ptr<IRoomGame>(RoomCtx&)> factory;
};

}  // namespace pandora
