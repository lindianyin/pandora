#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "common/async_worker.hpp"
#include "common/config.hpp"
#include "net/session_hub.hpp"
#include "store/mysql_client.hpp"
#include "store/redis_client.hpp"
#include "wallet/wallet_service.hpp"

namespace pandora {

struct SocialOpResult {
  bool ok{false};
  std::string error;
  int64_t balance{0};
  int currency{1};
};

class SocialService {
 public:
  SocialService(MysqlClient& mysql, RedisClient& redis, WalletService& wallet, SessionHub& hub, AsyncWorker& persist,
                SocialConfig cfg);

  void Bootstrap();

  // FR-SOC-01
  void OnRoundSettled(int64_t round_id, int template_id, const std::string& players_json, int base_score,
                      int multiplier);
  std::string SummaryJson(int64_t uid) const;
  std::string RecentJson(int64_t uid, int page, int page_size) const;

  // FR-SOC-02
  std::string FriendListJson(int64_t uid) const;
  std::string FriendPendingJson(int64_t uid) const;
  bool FriendRequest(int64_t from_uid, int64_t to_uid, std::string* err);
  bool FriendAccept(int64_t uid, int64_t from_uid, std::string* err);
  bool FriendReject(int64_t uid, int64_t from_uid, std::string* err);
  bool FriendRemove(int64_t uid, int64_t friend_uid, std::string* err);

  // FR-SOC-03
  std::string MailListJson(int64_t uid) const;
  bool MailRead(int64_t uid, int64_t mail_id, std::string* err);
  SocialOpResult MailClaim(int64_t uid, int64_t mail_id);
  bool MailDelete(int64_t uid, int64_t mail_id, std::string* err);
  bool AdminSendMail(int admin_id, const std::string& scope, const std::vector<int64_t>& uids,
                     const std::string& title, const std::string& body, const std::string& attach_json,
                     std::string* err);
  std::string AdminMailLogJson(int page, int page_size) const;

  // FR-SOC-04
  void OnGoldChanged(int64_t uid, int64_t gold);
  std::string RankJson(int64_t uid, const std::string& period, int limit) const;
  bool SnapshotRank(const std::string& period, std::string* err);
  std::string AdminRankSnapshotJson(const std::string& period, int page, int page_size) const;

 private:
  int FriendCount(int64_t uid) const;
  bool AreFriends(int64_t a, int64_t b) const;
  bool HasPending(int64_t from, int64_t to) const;
  std::string NicknameOf(int64_t uid) const;
  std::string DailyKey() const;
  std::string WeeklyKey() const;
  std::string RankRedisKey(const std::string& period) const;
  std::string PeriodKeyOf(const std::string& period) const;
  void PushMailNotify(int64_t uid, int64_t mail_id, const std::string& title, bool has_attach);
  void PushFriendNotify(int64_t uid, int kind, int64_t from_uid, const std::string& nickname);
  void InsertMailForUid(int64_t uid, const std::string& title, const std::string& body,
                        const std::string& attach_json);
  void ApplyStatsForPlayer(int64_t uid, int64_t delta, bool is_landlord);

  MysqlClient& mysql_;
  RedisClient& redis_;
  WalletService& wallet_;
  SessionHub& hub_;
  AsyncWorker& persist_;
  SocialConfig cfg_;
};

}  // namespace pandora
