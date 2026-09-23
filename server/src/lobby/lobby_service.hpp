#pragma once
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include "common/config.hpp"
#include "common/proto_wire.hpp"
#include "net/session_hub.hpp"
#include "store/memory_store.hpp"
#include "store/mysql_client.hpp"

namespace pandora {
class LobbyService {
 public:
  LobbyService(SessionHub& hub, MemoryStore& store, GameConfig cfg);
  void HandleGetLobby(int64_t uid);
  std::vector<proto_wire::LobbyTemplate> Templates() const;
  std::optional<proto_wire::LobbyTemplate> FindTemplate(int32_t id) const;
  int RakeBp(int32_t id) const;
  void ReloadFromDb(MysqlClient& mysql);
  void UpsertTemplate(int id, const std::string& name, int base_score, int rake_bp, int64_t min_gold, int64_t max_gold,
                      bool enabled);

 private:
  SessionHub& hub_;
  MemoryStore& store_;
  GameConfig cfg_;
  mutable std::mutex mu_;
  std::vector<proto_wire::LobbyTemplate> templates_;
  std::unordered_map<int32_t, int> rake_bp_;
};
}  // namespace pandora

