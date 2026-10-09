#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/config.hpp"
#include "game/game_ids.hpp"
#include "game/i_room_game.hpp"
#include "net/session_hub.hpp"
#include "store/memory_store.hpp"
#include "wallet/wallet_service.hpp"

namespace pandora {

class AdminService;
class ActivityService;
class SocialService;

class RoomManager {
 public:
  RoomManager(SessionHub& hub, MemoryStore& store, WalletService& wallet, GameConfig cfg);

  void SetAdmin(AdminService* admin) { admin_ = admin; }
  AdminService* Admin() { return admin_; }
  void SetActivity(ActivityService* activity) { activity_ = activity; }
  ActivityService* Activity() { return activity_; }
  void SetSocial(SocialService* social) { social_ = social; }
  SocialService* Social() { return social_; }

  int64_t CreateRoom(int32_t template_id, const std::vector<int64_t>& uids,
                     int32_t game_id = GameId::kDdz);
  int64_t CreateRoom(int32_t template_id, const std::array<int64_t, 3>& uids);

  bool SetReady(int64_t uid, bool ready);
  bool Leave(int64_t uid);
  void OnDisconnect(int64_t uid);
  void OnReconnect(int64_t uid);
  void HandleGameMsg(int64_t uid, uint32_t msg_id, const uint8_t* body, size_t len);
  void Tick();

  std::optional<int64_t> RoomOf(int64_t uid);
  void PushRoomState(int64_t room_id);
  void SendToUid(int64_t uid, uint32_t msg_id, const std::vector<uint8_t>& body);

  MemoryStore& Store() { return store_; }
  WalletService& Wallet() { return wallet_; }

  bool IsTrusteeship(int64_t room_id, int seat);
  void SetTrusteeship(int64_t room_id, int seat, bool on);
  void ClearTrusteeshipUid(int64_t uid);

  struct Seat {
    int64_t uid{0};
    std::string nickname;
    bool ready{false};
    bool online{true};
    bool trusteeship{false};
  };

  struct Room {
    int64_t room_id{0};
    int32_t template_id{1};
    int32_t game_id{GameId::kDdz};
    int seat_count{3};
    std::array<Seat, 4> seats{};
    std::string phase{"WaitReady"};
    RoomCtx game_ctx{};
    std::unique_ptr<IRoomGame> logic;
    int carry_banker{0};
    int carry_lian{1};
  };

  void ClearTrusteeshipInRoom(Room& r, int64_t uid);
  Room* FindRoomUnlocked(int64_t room_id);

  static constexpr std::size_t kShards = 64;

 private:
  struct RoomShard {
    std::recursive_mutex mu;
    std::unordered_map<int64_t, Room> rooms;
  };

  RoomCtx MakeCtx(Room& room);
  void MaybeStart(Room& room);
  bool GameInProgress(const Room& room) const;
  int SeatOfUid(const Room& room, int64_t uid) const;
  static std::size_t ShardOf(int64_t room_id) {
    return static_cast<std::size_t>(room_id >= 0 ? room_id : -room_id) % kShards;
  }
  RoomShard& Shard(int64_t room_id) { return shards_[ShardOf(room_id)]; }
  std::optional<int64_t> RoomIdOfUnlocked(int64_t uid) const;

  SessionHub& hub_;
  MemoryStore& store_;
  WalletService& wallet_;
  AdminService* admin_{nullptr};
  ActivityService* activity_{nullptr};
  SocialService* social_{nullptr};
  GameConfig cfg_;

  std::mutex index_mu_;
  int64_t next_room_id_{1};
  std::unordered_map<int64_t, int64_t> uid_to_room_;
  std::array<RoomShard, kShards> shards_{};
};

}  // namespace pandora
