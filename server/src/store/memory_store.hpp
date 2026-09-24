#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/async_worker.hpp"
#include "store/mysql_client.hpp"
#include "store/redis_client.hpp"

namespace pandora {

struct PlayerRecord {
  int64_t uid{0};
  std::string open_id;
  std::string nickname;
  int64_t gold{10000};
  int64_t diamond{0};
  int status{0};  // 0=ok 1=banned
};

struct SessionRecord {
  std::string token;
  int64_t uid{0};
};

struct LedgerEntry {
  int64_t id{0};
  int64_t uid{0};
  int currency{1};
  int64_t delta{0};
  int64_t balance_after{0};
  std::string biz_type;
  std::string idempotent_key;
  std::string ref_id;
};

enum class Currency : int { kGold = 1, kDiamond = 2 };

class MemoryStore {
 public:
  static constexpr std::size_t kShards = 64;

  MemoryStore(MysqlClient& mysql, RedisClient& redis, AsyncWorker& persist);

  // Find-or-create guest by device_id/open_id; persists to MySQL user + player_profile
  PlayerRecord CreateGuest(const std::string& device_id);
  std::optional<PlayerRecord> GetPlayer(int64_t uid);
  SessionRecord CreateSession(int64_t uid);
  std::optional<SessionRecord> GetSession(const std::string& token);
  void RevokeSession(const std::string& token);
  size_t SessionCount();
  size_t PlayerCount();
  void SetStatus(int64_t uid, int status);
  std::vector<PlayerRecord> ListPlayers(size_t limit = 200);

  // Hot path: memory + idempotency only; MySQL/Redis via AsyncWorker.
  std::optional<int64_t> Adjust(int64_t uid, Currency currency, int64_t delta, const std::string& biz_type,
                                const std::string& idem_key, const std::string& ref_id = {});
  std::optional<int64_t> AdjustGold(int64_t uid, int64_t delta, const std::string& idem_key) {
    return Adjust(uid, Currency::kGold, delta, "game_settle", idem_key);
  }
  std::vector<LedgerEntry> RecentLedgers(int64_t uid, size_t limit = 20);

 private:
  struct PlayerShard {
    std::mutex mu;
    std::unordered_map<int64_t, PlayerRecord> players;
    std::unordered_map<std::string, int64_t> idem_balance;
  };
  struct SessionShard {
    std::mutex mu;
    std::unordered_map<std::string, SessionRecord> sessions;
  };

  std::string EscapeSql(const std::string& s) const;
  void CachePlayer(const PlayerRecord& p);
  void WriteRedisProfile(const PlayerRecord& p);
  void EnqueuePersistAdjust(const PlayerRecord& snap, const LedgerEntry& e);
  void PersistAdjust(PlayerRecord snap, LedgerEntry e);
  std::optional<PlayerRecord> LoadFromMysqlByOpenId(const std::string& open_id);
  std::optional<PlayerRecord> LoadFromMysqlByUid(int64_t uid);
  bool InsertMysqlUser(PlayerRecord& p);

  static std::size_t UidShard(int64_t uid) {
    return static_cast<std::size_t>(uid >= 0 ? uid : -uid) % kShards;
  }
  static std::size_t TokenShard(const std::string& token) {
    return std::hash<std::string>{}(token) % kShards;
  }

  MysqlClient& mysql_;
  RedisClient& redis_;
  AsyncWorker& persist_;

  std::array<PlayerShard, kShards> player_shards_;
  std::array<SessionShard, kShards> session_shards_;

  std::mutex open_mu_;
  std::unordered_map<std::string, int64_t> open_id_index_;

  std::mutex meta_mu_;
  int64_t next_uid_{10001};
  int64_t next_ledger_id_{1};
  std::vector<LedgerEntry> ledgers_;
};

}  // namespace pandora
