#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "common/config.hpp"
#include "lobby/lobby_service.hpp"
#include "net/session_hub.hpp"
#include "room/room_manager.hpp"
#include "store/memory_store.hpp"

namespace pandora {

class MatchService {
 public:
  MatchService(SessionHub& hub, MemoryStore& store, LobbyService& lobby, RoomManager& rooms, GameConfig cfg);

  void QuickMatch(int64_t uid, int32_t template_id);
  void CancelMatch(int64_t uid);
  void OnDisconnect(int64_t uid);
  void Tick();

 private:
  struct Entry {
    int64_t uid{0};
    int32_t template_id{1};
    std::chrono::steady_clock::time_point enqueue_at;
  };

  void TryMatch(int32_t template_id);
  void RemoveFromQueue(int64_t uid);

  SessionHub& hub_;
  MemoryStore& store_;
  LobbyService& lobby_;
  RoomManager& rooms_;
  GameConfig cfg_;
  std::mutex mu_;
  std::unordered_map<int32_t, std::vector<Entry>> queues_;
  std::unordered_map<int64_t, int32_t> uid_in_queue_;
};

}  // namespace pandora

