#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/async_worker.hpp"
#include "common/proto_wire.hpp"
#include "net/session_hub.hpp"
#include "pay/pay_service.hpp"
#include "store/memory_store.hpp"
#include "store/mysql_client.hpp"
#include "store/redis_client.hpp"
#include "wallet/wallet_service.hpp"

namespace pandora {

class LobbyService;

enum class AdminRole { kCs = 1, kOps = 2, kSuper = 3 };

inline int RoleRank(AdminRole r) { return static_cast<int>(r); }

inline AdminRole ParseRole(const std::string& s) {
  if (s == "super") return AdminRole::kSuper;
  if (s == "ops") return AdminRole::kOps;
  return AdminRole::kCs;
}

inline const char* RoleName(AdminRole r) {
  switch (r) {
    case AdminRole::kSuper:
      return "super";
    case AdminRole::kOps:
      return "ops";
    default:
      return "cs";
  }
}

struct AdminSession {
  int admin_id{0};
  std::string username;
  AdminRole role{AdminRole::kCs};
  std::string token;
};

struct AdminLoginResult {
  bool ok{false};
  std::string token;
  std::string username;
  std::string role;
  std::string error;
};

class AdminService {
 public:
  AdminService(MysqlClient& mysql, RedisClient& redis, MemoryStore& store, WalletService& wallet, PayService& pay,
               LobbyService& lobby, SessionHub& hub, AsyncWorker& persist);

  void Bootstrap();

  AdminLoginResult Login(const std::string& username, const std::string& password);
  std::optional<AdminSession> Validate(const std::string& token);
  bool RequireRole(const AdminSession& s, AdminRole min_role) const;

  bool IsMaintain() const;
  void SetMaintain(bool on);
  bool IsBanned(int64_t uid) const;
  void SetBanned(int64_t uid, bool ban);

  std::string DashboardJson() const;
  std::string ListPlayersJson(const std::string& q, int page, int page_size) const;
  bool Kick(int64_t uid, const AdminSession& admin, std::string* err);
  bool Ban(int64_t uid, bool ban, const AdminSession& admin, std::string* err);
  bool WalletAdjust(int64_t uid, int currency, int64_t delta, const std::string& idem, const AdminSession& admin,
                    std::string* err, int64_t* balance_out);
  std::string ListLedgersJson(int64_t uid, int page, int page_size) const;
  std::string ListRoundsJson(int64_t uid, int page, int page_size) const;
  std::string ListTemplatesJson() const;
  bool PutTemplate(int id, int32_t game_id, const std::string& name, int base_score, int rake_bp, int64_t min_gold,
                   int64_t max_gold, bool enabled, const AdminSession& admin, std::string* err);
  std::string ListProductsJson() const;
  bool UpsertProduct(int id, int amount_fen, int diamond, int gift, const std::string& gift_items_json, bool enabled,
                     const AdminSession& admin, std::string* err);
  bool SetProductEnabled(int id, bool enabled, const AdminSession& admin, std::string* err);
  bool DeleteProduct(int id, const AdminSession& admin, std::string* err);
  std::string ListOrdersJson(int64_t uid, int status, int page, int page_size) const;
  bool Announce(const std::string& message, const AdminSession& admin, std::string* err);
  bool SetMaintainOp(bool on, const AdminSession& admin, std::string* err);
  std::string ListAuditJson(const std::string& q, int page, int page_size) const;

  std::string ExportLedgersCsv(int limit) const;
  std::string ExportRoundsCsv(int limit) const;
  std::string ExportClaimsCsv(int limit) const;

  void RecordRound(int64_t round_id, int64_t room_id, int template_id, const std::string& players_json, int base_score,
                   int multiplier, int game_id = 2000);

  void Audit(int admin_id, const std::string& action, const std::string& target, const std::string& before,
             const std::string& after);

 private:
  std::string HashPassword(const std::string& password) const;
  std::string MakeToken() const;
  std::optional<AdminSession> LoadSessionFromRedis(const std::string& token) const;

  MysqlClient& mysql_;
  RedisClient& redis_;
  MemoryStore& store_;
  WalletService& wallet_;
  PayService& pay_;
  LobbyService& lobby_;
  SessionHub& hub_;
  AsyncWorker& persist_;

  mutable std::mutex mu_;
  std::unordered_map<std::string, AdminSession> admin_sessions_;  // hot cache; source of truth Redis
};

}  // namespace pandora
