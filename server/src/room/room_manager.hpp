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

#include "game/ddz_cards.hpp"

#include "net/session_hub.hpp"

#include "store/memory_store.hpp"

#include "wallet/wallet_service.hpp"

namespace pandora {

class AdminService;
class ActivityService;
class SocialService;

class RoomManager;



class DdzClassicSimple {

 public:

  DdzClassicSimple(RoomManager& rooms, int64_t room_id, std::array<int64_t, 3> uids, GameConfig cfg);



  void Start();

  void OnBid(int seat, int score);

  void OnPlay(int seat, bool pass, const std::vector<int>& cards);

  void Tick(std::chrono::steady_clock::time_point now);

  bool Finished() const { return finished_; }

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



  RoomManager& rooms_;

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



class RoomManager {

 public:

  RoomManager(SessionHub& hub, MemoryStore& store, WalletService& wallet, GameConfig cfg);

  void SetAdmin(AdminService* admin) { admin_ = admin; }
  AdminService* Admin() { return admin_; }
  void SetActivity(ActivityService* activity) { activity_ = activity; }
  ActivityService* Activity() { return activity_; }
  void SetSocial(SocialService* social) { social_ = social; }
  SocialService* Social() { return social_; }



  int64_t CreateRoom(int32_t template_id, const std::array<int64_t, 3>& uids);

  bool SetReady(int64_t uid, bool ready);

  bool Leave(int64_t uid);

  void OnDisconnect(int64_t uid);

  void OnReconnect(int64_t uid);

  void OnBid(int64_t uid, int score);

  void OnPlay(int64_t uid, bool pass, const std::vector<int>& cards);

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

    std::array<Seat, 3> seats{};

    std::string phase{"WaitReady"};

    std::unique_ptr<DdzClassicSimple> game;

  };

  void ClearTrusteeshipInRoom(Room& r, int64_t uid);

  Room* FindRoomUnlocked(int64_t room_id);

  static constexpr std::size_t kShards = 64;

 private:
  struct RoomShard {
    std::recursive_mutex mu;
    std::unordered_map<int64_t, Room> rooms;
  };

  void MaybeStart(Room& room);
  static std::size_t ShardOf(int64_t room_id) {
    return static_cast<std::size_t>(room_id >= 0 ? room_id : -room_id) % kShards;
  }
  RoomShard& Shard(int64_t room_id) { return shards_[ShardOf(room_id)]; }
  // Lookup room_id under index_mu_; empty if not seated.
  std::optional<int64_t> RoomIdOfUnlocked(int64_t uid) const;

  SessionHub& hub_;
  MemoryStore& store_;
  WalletService& wallet_;
  AdminService* admin_{nullptr};
  ActivityService* activity_{nullptr};
  SocialService* social_{nullptr};
  GameConfig cfg_;

  // Lock order when both needed: index_mu_ then shard.mu
  std::mutex index_mu_;
  int64_t next_room_id_{1};
  std::unordered_map<int64_t, int64_t> uid_to_room_;
  std::array<RoomShard, kShards> shards_{};

  friend class DdzClassicSimple;

};



}  // namespace pandora


