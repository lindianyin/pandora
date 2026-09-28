#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "bag/bag_service.hpp"
#include "common/async_worker.hpp"
#include "net/session_hub.hpp"
#include "store/mysql_client.hpp"
#include "store/redis_client.hpp"
#include "wallet/wallet_service.hpp"

namespace pandora {

struct ActivityDef {
  int id{0};
  std::string type;  // sign / task / gift
  std::string title;
  std::string rules_json;
  bool enabled{true};
  std::string start_at;
  std::string end_at;
};

struct ActivityProgressView {
  int activity_id{0};
  std::string type;
  std::string title;
  std::string progress_json;
  bool claimable{false};
  bool claimed{false};
  std::string reward_key;
};

struct ClaimResult {
  bool ok{false};
  std::string error;
  int64_t balance{0};
  int currency{1};
};

class ActivityService {
 public:
  ActivityService(MysqlClient& mysql, RedisClient& redis, WalletService& wallet, BagService& bag, SessionHub& hub,
                  AsyncWorker& persist);

  void Bootstrap();
  void Reload();

  std::vector<ActivityDef> ListDefs(bool include_disabled = false) const;
  std::optional<ActivityDef> GetDef(int id) const;
  bool UpsertDef(const ActivityDef& def, std::string* err);
  bool SetEnabled(int id, bool enabled, std::string* err);
  int ClaimCount(int activity_id) const;

  std::string ListForPlayerJson(int64_t uid);
  std::string ProgressJson(int64_t uid, int activity_id);
  ClaimResult Claim(int64_t uid, int activity_id, const std::string& reward_key);

  void OnLogin(int64_t uid);
  void OnGameSettled(int64_t uid, int template_id);

 private:
  void PushUpdate(int64_t uid, const ActivityDef& def, const std::string& progress_json, bool claimable);
  std::string LoadProgress(int64_t uid, int aid);
  void SaveProgress(int64_t uid, int aid, const std::string& json);
  bool HasClaimed(int64_t uid, int aid, const std::string& reward_key);
  void MarkClaimed(int64_t uid, int aid, const std::string& reward_key);
  bool InWindow(const ActivityDef& def) const;
  std::string TodayKey() const;
  int ExtractInt(const std::string& json, const std::string& key, int def) const;
  std::string ExtractStr(const std::string& json, const std::string& key, const std::string& def = {}) const;
  std::string EscapeSql(const std::string& s) const;
  void EnsureStockSchema();
  void InitGiftStock(const ActivityDef& d);
  int ReadStock(int aid) const;
  bool DecrStock(int aid, int& remain);
  void EnsureSeed();

  MysqlClient& mysql_;
  RedisClient& redis_;
  WalletService& wallet_;
  BagService& bag_;
  SessionHub& hub_;
  AsyncWorker& persist_;

  mutable std::mutex mu_;
  std::vector<ActivityDef> defs_;  // hot cache from MySQL only
};

}  // namespace pandora
