#include "admin/admin_service.hpp"

#include <random>
#include <sstream>

#include <nlohmann/json.hpp>

#include "common/errors.hpp"
#include "common/log.hpp"
#include "common/sha1.hpp"
#include "lobby/lobby_service.hpp"

namespace pandora {
namespace {

using json = nlohmann::json;

json ParseJsonOr(const std::string& s, json fallback) {
  if (s.empty()) return fallback;
  auto j = json::parse(s, nullptr, false);
  if (j.is_discarded()) return fallback;
  return j;
}

}  // namespace

AdminService::AdminService(MysqlClient& mysql, RedisClient& redis, MemoryStore& store, WalletService& wallet,
                           PayService& pay, LobbyService& lobby, SessionHub& hub, AsyncWorker& persist)
    : mysql_(mysql),
      redis_(redis),
      store_(store),
      wallet_(wallet),
      pay_(pay),
      lobby_(lobby),
      hub_(hub),
      persist_(persist) {}

std::string AdminService::HashPassword(const std::string& password) const {
  return crypto::Sha1Hex("pandora:" + password);
}

std::string AdminService::Escape(const std::string& s) const {
  std::string o;
  o.reserve(s.size());
  for (char c : s) {
    if (c == '\'' || c == '\\') o.push_back('\\');
    o.push_back(c);
  }
  return o;
}

std::string AdminService::MakeToken() const {
  static thread_local std::mt19937_64 rng{std::random_device{}()};
  std::uniform_int_distribution<uint64_t> dist;
  std::ostringstream oss;
  oss << std::hex << dist(rng) << dist(rng);
  return "adm_" + oss.str();
}

void AdminService::Bootstrap() {
  const std::string hash = HashPassword("admin123");
  if (mysql_.Available()) {
    auto rows = mysql_.Query("SELECT id FROM admin_user WHERE username='admin' LIMIT 1");
    if (rows && rows->empty()) {
      mysql_.ExecBind("INSERT INTO admin_user(username,password_hash,role,enabled) VALUES('admin',?,'super',1)",
                      {Str(hash)});
      PLOG_INFO("seeded admin_user admin/admin123");
    }
    lobby_.ReloadFromDb(mysql_);
    pay_.ReloadFromDb(mysql_);
  } else {
    PLOG_WARN("mysql down: admin bootstrap limited");
  }
  if (redis_.Available()) {
    auto v = redis_.Get("ops:maintain");
    if (!v) redis_.Set("ops:maintain", "0");
  }
}

AdminLoginResult AdminService::Login(const std::string& username, const std::string& password) {
  AdminLoginResult r;
  if (!mysql_.Available()) {
    r.error = "mysql unavailable";
    return r;
  }
  const std::string hash = HashPassword(password);
  auto rows = mysql_.QueryBind("SELECT id,role,password_hash,enabled FROM admin_user WHERE username=? LIMIT 1",
                               {Str(username)});
  if (!rows || rows->empty() || rows->front().cols.size() < 4) {
    r.error = "invalid credentials";
    return r;
  }
  const auto& c = rows->front().cols;
  if (c[3] != "1" || c[2] != hash) {
    r.error = "invalid credentials";
    return r;
  }
  AdminSession sess;
  sess.admin_id = std::stoi(c[0]);
  sess.username = username;
  sess.role = ParseRole(c[1]);
  sess.token = MakeToken();
  {
    std::lock_guard<std::mutex> lk(mu_);
    admin_sessions_[sess.token] = sess;
  }
  if (redis_.Available()) {
    // admin_id|role|username
    redis_.Set("admin:session:" + sess.token,
               std::to_string(sess.admin_id) + "|" + RoleName(sess.role) + "|" + username, 86400);
  }
  r.ok = true;
  r.token = sess.token;
  r.username = username;
  r.role = RoleName(sess.role);
  return r;
}

std::optional<AdminSession> AdminService::LoadSessionFromRedis(const std::string& token) const {
  if (!redis_.Available() || token.empty()) return std::nullopt;
  auto v = redis_.Get("admin:session:" + token);
  if (!v || v->empty()) return std::nullopt;
  // format: id|role|username
  const auto a = v->find('|');
  if (a == std::string::npos) return std::nullopt;
  const auto b = v->find('|', a + 1);
  AdminSession s;
  s.token = token;
  try {
    s.admin_id = std::stoi(v->substr(0, a));
  } catch (...) {
    return std::nullopt;
  }
  if (b == std::string::npos) {
    s.role = ParseRole(v->substr(a + 1));
    s.username = "admin";
  } else {
    s.role = ParseRole(v->substr(a + 1, b - a - 1));
    s.username = v->substr(b + 1);
  }
  return s;
}

std::optional<AdminSession> AdminService::Validate(const std::string& token) {
  {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = admin_sessions_.find(token);
    if (it != admin_sessions_.end()) return it->second;
  }
  auto s = LoadSessionFromRedis(token);
  if (!s) return std::nullopt;
  // optional: confirm admin still enabled in MySQL
  if (mysql_.Available()) {
    auto rows = mysql_.QueryBind("SELECT enabled,role,username FROM admin_user WHERE id=? LIMIT 1",
                                 {I64(s->admin_id)});
    if (!rows || rows->empty() || rows->front().cols[0] != "1") return std::nullopt;
    if (rows->front().cols.size() >= 3) {
      s->role = ParseRole(rows->front().cols[1]);
      s->username = rows->front().cols[2];
    }
  }
  std::lock_guard<std::mutex> lk(mu_);
  admin_sessions_[token] = *s;
  return s;
}

bool AdminService::RequireRole(const AdminSession& s, AdminRole min_role) const {
  return RoleRank(s.role) >= RoleRank(min_role);
}

bool AdminService::IsMaintain() const {
  if (redis_.Available()) {
    auto v = redis_.Get("ops:maintain");
    if (v) return *v == "1";
  }
  return false;
}

void AdminService::SetMaintain(bool on) {
  if (redis_.Available()) redis_.Set("ops:maintain", on ? "1" : "0");
}

bool AdminService::IsBanned(int64_t uid) const {
  if (redis_.Available()) {
    auto v = redis_.Get("user:ban:" + std::to_string(uid));
    if (v && *v == "1") return true;
    if (v && *v == "0") return false;
  }
  if (mysql_.Available()) {
    auto rows = mysql_.QueryBind("SELECT status FROM `user` WHERE uid=? LIMIT 1", {I64(uid)});
    if (rows && !rows->empty() && !rows->front().cols.empty()) return rows->front().cols[0] == "1";
  }
  auto p = store_.GetPlayer(uid);
  return p && p->status == 1;
}

void AdminService::SetBanned(int64_t uid, bool ban) {
  store_.SetStatus(uid, ban ? 1 : 0);
  if (redis_.Available()) redis_.Set("user:ban:" + std::to_string(uid), ban ? "1" : "0");
}

void AdminService::Audit(int admin_id, const std::string& action, const std::string& target, const std::string& before,
                         const std::string& after) {
  if (!mysql_.Available()) {
    PLOG_WARN("audit skipped: mysql unavailable action=" << action);
    return;
  }
  auto to_json_col = [](const std::string& s) -> std::string {
    if (s.empty()) return "{}";
    if ((s.front() == '{' && s.back() == '}') || (s.front() == '[' && s.back() == ']')) return s;
    std::string o = "\"";
    for (char c : s) {
      if (c == '\\' || c == '"') o.push_back('\\');
      o.push_back(c);
    }
    o.push_back('"');
    return o;
  };
  const std::string bj = to_json_col(before);
  const std::string aj = to_json_col(after);
  const int r = mysql_.ExecBind(
      "INSERT INTO admin_audit(admin_id,action,target,before_json,after_json,ip) VALUES(?,?,?,?,?,'')",
      {I64(admin_id), Str(action), Str(target), Str(bj), Str(aj)});
  if (r < 0) PLOG_WARN("audit insert fail: " << mysql_.LastError() << " action=" << action);
}

std::string AdminService::DashboardJson() const {
  int64_t players = 0;
  int64_t rounds = 0;
  int64_t orders = 0;
  if (mysql_.Available()) {
    if (auto rows = mysql_.Query("SELECT COUNT(*) FROM `user`")) {
      if (rows && !rows->empty())
        try {
          players = std::stoll(rows->front().cols[0]);
        } catch (...) {
        }
    }
    if (auto rows = mysql_.Query("SELECT COUNT(*) FROM game_round")) {
      if (rows && !rows->empty())
        try {
          rounds = std::stoll(rows->front().cols[0]);
        } catch (...) {
        }
    }
    if (auto rows = mysql_.Query("SELECT COUNT(*) FROM pay_order")) {
      if (rows && !rows->empty())
        try {
          orders = std::stoll(rows->front().cols[0]);
        } catch (...) {
        }
    }
  }
  return json{{"ccu", hub_.OnlineCount()},
              {"players", players},
              {"rounds", rounds},
              {"orders", orders},
              {"maintain", IsMaintain()},
              {"mysql", mysql_.Available()},
              {"redis", redis_.Available()}}
      .dump();
}

std::string AdminService::ListPlayersJson(const std::string& q, int page, int page_size) const {
  if (page < 1) page = 1;
  if (page_size < 1) page_size = 20;
  if (page_size > 100) page_size = 100;
  struct Row {
    PlayerRecord p;
    std::string created_at;
  };
  std::vector<Row> filtered;
  if (mysql_.Available()) {
    std::string sql =
        "SELECT u.uid,u.open_id,u.status,p.nickname,p.gold,p.diamond,"
        "DATE_FORMAT(u.created_at,'%Y-%m-%d %H:%i:%s') "
        "FROM `user` u INNER JOIN player_profile p ON p.uid=u.uid";
    if (!q.empty()) {
      sql += " WHERE CAST(u.uid AS CHAR) LIKE '%" + Escape(q) + "%' OR p.nickname LIKE '%" + Escape(q) +
             "%' OR u.open_id LIKE '%" + Escape(q) + "%'";
    }
    sql += " ORDER BY u.uid DESC LIMIT 500";
    auto rows = mysql_.Query(sql);
    if (rows) {
      for (const auto& row : *rows) {
        if (row.cols.size() < 7) continue;
        Row r;
        try {
          r.p.uid = std::stoll(row.cols[0]);
          r.p.open_id = row.cols[1];
          r.p.status = std::stoi(row.cols[2]);
          r.p.nickname = row.cols[3];
          r.p.gold = std::stoll(row.cols[4]);
          r.p.diamond = std::stoll(row.cols[5]);
          r.created_at = row.cols[6];
        } catch (...) {
          continue;
        }
        filtered.push_back(std::move(r));
      }
    }
  } else {
    for (auto& p : store_.ListPlayers(200)) filtered.push_back(Row{std::move(p), {}});
  }
  const int total = static_cast<int>(filtered.size());
  const int start = (page - 1) * page_size;
  json items = json::array();
  for (int i = start; i < total && i < start + page_size; ++i) {
    const auto& r = filtered[static_cast<size_t>(i)];
    items.push_back({{"uid", r.p.uid},
                     {"nickname", r.p.nickname},
                     {"gold", r.p.gold},
                     {"diamond", r.p.diamond},
                     {"status", r.p.status},
                     {"online", hub_.IsOnline(r.p.uid)},
                     {"created_at", r.created_at}});
  }
  return json{{"total", total}, {"items", std::move(items)}}.dump();
}

bool AdminService::Kick(int64_t uid, const AdminSession& admin, std::string* err) {
  if (!RequireRole(admin, AdminRole::kOps)) {
    if (err) *err = "forbidden";
    return false;
  }
  hub_.Kick(uid, 2, "kicked by admin");
  Audit(admin.admin_id, "kick", std::to_string(uid), "", "");
  return true;
}

bool AdminService::Ban(int64_t uid, bool ban, const AdminSession& admin, std::string* err) {
  if (!RequireRole(admin, AdminRole::kOps)) {
    if (err) *err = "forbidden";
    return false;
  }
  SetBanned(uid, ban);
  if (ban) hub_.Kick(uid, 3, "banned");
  Audit(admin.admin_id, ban ? "ban" : "unban", std::to_string(uid), "", ban ? "1" : "0");
  return true;
}

bool AdminService::WalletAdjust(int64_t uid, int currency, int64_t delta, const std::string& idem,
                                const AdminSession& admin, std::string* err, int64_t* balance_out) {
  if (!RequireRole(admin, AdminRole::kOps)) {
    if (err) *err = "forbidden";
    return false;
  }
  if (idem.empty()) {
    if (err) *err = "idempotent_key required";
    return false;
  }
  auto r = wallet_.Adjust(uid, currency == 2 ? Currency::kDiamond : Currency::kGold, delta, "admin_adjust",
                          "admin:" + idem, idem);
  if (!r.ok) {
    if (err) *err = r.error.empty() ? "adjust failed" : r.error;
    return false;
  }
  if (balance_out) *balance_out = r.balance;
  Audit(admin.admin_id, "wallet_adjust", std::to_string(uid), "",
        json{{"currency", currency}, {"delta", delta}}.dump());
  return true;
}

std::string AdminService::ListLedgersJson(int64_t uid, int page, int page_size) const {
  if (page < 1) page = 1;
  if (page_size < 1) page_size = 20;
  if (page_size > 100) page_size = 100;
  struct Row {
    LedgerEntry e;
    std::string created_at;
  };
  std::vector<Row> all;
  if (mysql_.Available()) {
    std::string sql =
        "SELECT id,uid,currency,delta,balance_after,biz_type,idempotent_key,ref_id,"
        "DATE_FORMAT(created_at,'%Y-%m-%d %H:%i:%s') FROM ledger";
    if (uid > 0) sql += " WHERE uid=" + std::to_string(uid);
    sql += " ORDER BY id DESC LIMIT 500";
    auto rows = mysql_.Query(sql);
    if (rows) {
      for (const auto& row : *rows) {
        if (row.cols.size() < 9) continue;
        Row r;
        try {
          r.e.id = std::stoll(row.cols[0]);
          r.e.uid = std::stoll(row.cols[1]);
          r.e.currency = std::stoi(row.cols[2]);
          r.e.delta = std::stoll(row.cols[3]);
          r.e.balance_after = std::stoll(row.cols[4]);
          r.e.biz_type = row.cols[5];
          r.e.idempotent_key = row.cols[6];
          r.e.ref_id = row.cols[7];
          r.created_at = row.cols[8];
        } catch (...) {
          continue;
        }
        all.push_back(std::move(r));
      }
    }
  } else {
    for (auto& e : store_.RecentLedgers(uid, 500)) all.push_back(Row{std::move(e), {}});
  }
  const int total = static_cast<int>(all.size());
  const int start = (page - 1) * page_size;
  json items = json::array();
  for (int i = start; i < total && i < start + page_size; ++i) {
    const auto& r = all[static_cast<size_t>(i)];
    items.push_back({{"id", r.e.id},
                     {"uid", r.e.uid},
                     {"currency", r.e.currency},
                     {"delta", r.e.delta},
                     {"balance_after", r.e.balance_after},
                     {"biz_type", r.e.biz_type},
                     {"idempotent_key", r.e.idempotent_key},
                     {"created_at", r.created_at}});
  }
  return json{{"total", total}, {"items", std::move(items)}}.dump();
}

std::string AdminService::ListRoundsJson(int64_t uid, int page, int page_size) const {
  struct RoundRow {
    int64_t round_id{0};
    int64_t room_id{0};
    int template_id{0};
    std::string players_json;
    int base_score{0};
    int multiplier{0};
    std::string started_at;
    std::string ended_at;
  };
  std::vector<RoundRow> rounds;
  if (mysql_.Available()) {
    std::string sql =
        "SELECT round_id,room_id,template_id,CAST(players_json AS CHAR),base_score,multiplier,"
        "DATE_FORMAT(started_at,'%Y-%m-%d %H:%i:%s'),DATE_FORMAT(ended_at,'%Y-%m-%d %H:%i:%s') FROM game_round";
    if (uid > 0) sql += " WHERE CAST(players_json AS CHAR) LIKE '%" + std::to_string(uid) + "%'";
    sql += " ORDER BY round_id DESC LIMIT 200";
    auto rows = mysql_.Query(sql);
    if (rows) {
      for (const auto& row : *rows) {
        if (row.cols.size() < 8) continue;
        RoundRow m;
        try {
          m.round_id = std::stoll(row.cols[0]);
          m.room_id = std::stoll(row.cols[1]);
          m.template_id = std::stoi(row.cols[2]);
          m.players_json = row.cols[3];
          m.base_score = std::stoi(row.cols[4]);
          m.multiplier = std::stoi(row.cols[5]);
          m.started_at = row.cols[6];
          m.ended_at = row.cols[7];
        } catch (...) {
          continue;
        }
        rounds.push_back(m);
      }
    }
  }
  if (page < 1) page = 1;
  if (page_size < 1) page_size = 20;
  if (page_size > 100) page_size = 100;
  const int total = static_cast<int>(rounds.size());
  const int start = (page - 1) * page_size;
  json items = json::array();
  for (int i = start; i < total && i < start + page_size; ++i) {
    const auto& m = rounds[static_cast<size_t>(i)];
    items.push_back({{"round_id", m.round_id},
                     {"room_id", m.room_id},
                     {"template_id", m.template_id},
                     {"players_json", ParseJsonOr(m.players_json, json::array())},
                     {"base_score", m.base_score},
                     {"multiplier", m.multiplier},
                     {"started_at", m.started_at},
                     {"ended_at", m.ended_at}});
  }
  return json{{"total", total}, {"items", std::move(items)}}.dump();
}

std::string AdminService::ListTemplatesJson() const {
  if (mysql_.Available()) lobby_.ReloadFromDb(mysql_);
  const auto& ts = lobby_.Templates();
  json items = json::array();
  for (const auto& t : ts) {
    items.push_back({{"id", t.id},
                     {"name", t.name},
                     {"base_score", t.base_score},
                     {"min_gold", t.min_gold},
                     {"max_gold", t.max_gold},
                     {"enabled", t.enabled},
                     {"rake_bp", lobby_.RakeBp(t.id)}});
  }
  return json{{"items", std::move(items)}}.dump();
}

bool AdminService::PutTemplate(int id, const std::string& name, int base_score, int rake_bp, int64_t min_gold,
                               int64_t max_gold, bool enabled, const AdminSession& admin, std::string* err) {
  if (!RequireRole(admin, AdminRole::kOps)) {
    if (err) *err = "forbidden";
    return false;
  }
  if (!mysql_.Available()) {
    if (err) *err = "mysql unavailable";
    return false;
  }
  const int r = mysql_.Exec(
      "INSERT INTO room_template(id,game_id,name,base_score,rake_bp,min_gold,max_gold,enabled) VALUES(" +
      std::to_string(id) + ",1,'" + Escape(name) + "'," + std::to_string(base_score) + "," + std::to_string(rake_bp) +
      "," + std::to_string(min_gold) + "," + std::to_string(max_gold) + "," + (enabled ? "1" : "0") +
      ") ON DUPLICATE KEY UPDATE name=VALUES(name),base_score=VALUES(base_score),rake_bp=VALUES(rake_bp),"
      "min_gold=VALUES(min_gold),max_gold=VALUES(max_gold),enabled=VALUES(enabled)");
  if (r < 0) {
    if (err) *err = mysql_.LastError();
    return false;
  }
  lobby_.ReloadFromDb(mysql_);
  Audit(admin.admin_id, "put_template", std::to_string(id), "", name);
  return true;
}

std::string AdminService::ListProductsJson() const {
  if (mysql_.Available()) pay_.ReloadFromDb(mysql_);
  auto products = pay_.ListProducts(true);
  json items = json::array();
  for (const auto& p : products) {
    items.push_back({{"id", p.id},
                     {"amount_fen", p.amount_fen},
                     {"diamond", p.diamond},
                     {"gift_diamond", p.gift_diamond},
                     {"enabled", p.enabled}});
  }
  return json{{"items", std::move(items)}}.dump();
}

bool AdminService::UpsertProduct(int id, int amount_fen, int diamond, int gift, bool enabled, const AdminSession& admin,
                                 std::string* err) {
  if (!RequireRole(admin, AdminRole::kOps)) {
    if (err) *err = "forbidden";
    return false;
  }
  if (!mysql_.Available()) {
    if (err) *err = "mysql unavailable";
    return false;
  }
  int r = 0;
  if (id <= 0) {
    r = mysql_.Exec("INSERT INTO pay_product(amount_fen,diamond,gift_diamond,sort,enabled) VALUES(" +
                    std::to_string(amount_fen) + "," + std::to_string(diamond) + "," + std::to_string(gift) + ",0," +
                    (enabled ? "1" : "0") + ")");
  } else {
    r = mysql_.Exec("INSERT INTO pay_product(id,amount_fen,diamond,gift_diamond,sort,enabled) VALUES(" +
                    std::to_string(id) + "," + std::to_string(amount_fen) + "," + std::to_string(diamond) + "," +
                    std::to_string(gift) + ",0," + (enabled ? "1" : "0") +
                    ") ON DUPLICATE KEY UPDATE amount_fen=VALUES(amount_fen),diamond=VALUES(diamond),"
                    "gift_diamond=VALUES(gift_diamond),enabled=VALUES(enabled)");
  }
  if (r < 0) {
    if (err) *err = mysql_.LastError();
    return false;
  }
  pay_.ReloadFromDb(mysql_);
  Audit(admin.admin_id, "upsert_product", std::to_string(id), "", std::to_string(amount_fen));
  return true;
}

bool AdminService::DeleteProduct(int id, const AdminSession& admin, std::string* err) {
  return SetProductEnabled(id, false, admin, err);
}

bool AdminService::SetProductEnabled(int id, bool enabled, const AdminSession& admin, std::string* err) {
  if (!RequireRole(admin, AdminRole::kOps)) {
    if (err) *err = "forbidden";
    return false;
  }
  if (!mysql_.Available()) {
    if (err) *err = "mysql unavailable";
    return false;
  }
  if (id <= 0) {
    if (err) *err = "id required";
    return false;
  }
  const int r = mysql_.Exec("UPDATE pay_product SET enabled=" + std::string(enabled ? "1" : "0") +
                            " WHERE id=" + std::to_string(id));
  if (r < 0) {
    if (err) *err = mysql_.LastError();
    return false;
  }
  pay_.ReloadFromDb(mysql_);
  Audit(admin.admin_id, enabled ? "enable_product" : "disable_product", std::to_string(id), "", enabled ? "1" : "0");
  return true;
}

std::string AdminService::ListOrdersJson(int64_t uid, int status, int page, int page_size) const {
  if (page < 1) page = 1;
  if (page_size < 1) page_size = 20;
  if (page_size > 100) page_size = 100;
  auto orders = pay_.ListOrders(500);
  std::vector<PayOrder> filtered;
  filtered.reserve(orders.size());
  for (const auto& o : orders) {
    if (uid > 0 && o.uid != uid) continue;
    if (status >= 0 && o.status != status) continue;
    filtered.push_back(o);
  }
  const int total = static_cast<int>(filtered.size());
  const int start = (page - 1) * page_size;
  json items = json::array();
  for (int i = start; i < total && i < start + page_size; ++i) {
    const auto& o = filtered[static_cast<size_t>(i)];
    items.push_back({{"order_id", o.order_id},
                     {"uid", o.uid},
                     {"product_id", o.product_id},
                     {"amount_fen", o.amount_fen},
                     {"diamond", o.diamond},
                     {"status", o.status},
                     {"created_at", o.created_at}});
  }
  return json{{"total", total}, {"items", std::move(items)}}.dump();
}

bool AdminService::Announce(const std::string& message, const AdminSession& admin, std::string* err) {
  if (!RequireRole(admin, AdminRole::kOps)) {
    if (err) *err = "forbidden";
    return false;
  }
  hub_.BroadcastAll(MsgId::kS2C_Error, proto_wire::EncodeS2C_Error(0, message, 0));
  if (redis_.Available()) redis_.Set("ops:announce:last", message, 86400);
  Audit(admin.admin_id, "announce", "", "", message);
  return true;
}

bool AdminService::SetMaintainOp(bool on, const AdminSession& admin, std::string* err) {
  if (!RequireRole(admin, AdminRole::kSuper)) {
    if (err) *err = "forbidden";
    return false;
  }
  SetMaintain(on);
  Audit(admin.admin_id, "maintain", "", "", on ? "1" : "0");
  return true;
}

std::string AdminService::ListAuditJson(const std::string& q, int page, int page_size) const {
  if (page < 1) page = 1;
  if (page_size < 1) page_size = 20;
  if (page_size > 100) page_size = 100;
  struct AuditRow {
    int admin_id{0};
    std::string action;
    std::string target;
    std::string before;
    std::string after;
    std::string created_at;
  };
  std::vector<AuditRow> audits;
  if (mysql_.Available()) {
    std::string sql =
        "SELECT admin_id,action,target,CAST(before_json AS CHAR),CAST(after_json AS CHAR),"
        "DATE_FORMAT(created_at,'%Y-%m-%d %H:%i:%s') "
        "FROM admin_audit";
    if (!q.empty()) {
      sql += " WHERE action LIKE '%" + Escape(q) + "%' OR target LIKE '%" + Escape(q) + "%'";
    }
    sql += " ORDER BY id DESC LIMIT 200";
    auto rows = mysql_.Query(sql);
    if (rows) {
      for (const auto& row : *rows) {
        if (row.cols.size() < 6) continue;
        AuditRow a;
        try {
          a.admin_id = std::stoi(row.cols[0]);
        } catch (...) {
          continue;
        }
        a.action = row.cols[1];
        a.target = row.cols[2];
        a.before = row.cols[3];
        a.after = row.cols[4];
        a.created_at = row.cols[5];
        audits.push_back(a);
      }
    }
  }
  const int total = static_cast<int>(audits.size());
  const int start = (page - 1) * page_size;
  json items = json::array();
  for (int i = start; i < total && i < start + page_size; ++i) {
    const auto& a = audits[static_cast<size_t>(i)];
    items.push_back({{"admin_id", a.admin_id},
                     {"action", a.action},
                     {"target", a.target},
                     {"before", a.before},
                     {"after", a.after},
                     {"created_at", a.created_at}});
  }
  return json{{"total", total}, {"items", std::move(items)}}.dump();
}

std::string AdminService::ExportLedgersCsv(int limit) const {
  std::ostringstream oss;
  oss << "id,uid,currency,delta,balance_after,biz_type,idempotent_key,ref_id\n";
  if (!mysql_.Available()) return oss.str();
  auto rows = mysql_.Query(
      "SELECT id,uid,currency,delta,balance_after,biz_type,idempotent_key,ref_id FROM ledger ORDER BY id "
      "DESC LIMIT " +
      std::to_string(limit));
  if (!rows) return oss.str();
  for (const auto& row : *rows) {
    if (row.cols.size() < 8) continue;
    oss << row.cols[0] << ',' << row.cols[1] << ',' << row.cols[2] << ',' << row.cols[3] << ',' << row.cols[4] << ",\""
        << Escape(row.cols[5]) << "\",\"" << Escape(row.cols[6]) << "\",\"" << Escape(row.cols[7]) << "\"\n";
  }
  return oss.str();
}

std::string AdminService::ExportRoundsCsv(int limit) const {
  std::ostringstream oss;
  oss << "round_id,room_id,template_id,base_score,multiplier,players_json\n";
  if (!mysql_.Available()) return oss.str();
  auto rows = mysql_.Query(
      "SELECT round_id,room_id,template_id,base_score,multiplier,CAST(players_json AS CHAR) FROM game_round ORDER BY "
      "round_id DESC LIMIT " +
      std::to_string(limit));
  if (!rows) return oss.str();
  for (const auto& row : *rows) {
    if (row.cols.size() < 6) continue;
    std::string pj = row.cols[5];
    for (char& c : pj)
      if (c == '"') c = '\'';
    oss << row.cols[0] << ',' << row.cols[1] << ',' << row.cols[2] << ',' << row.cols[3] << ',' << row.cols[4] << ",\""
        << pj << "\"\n";
  }
  return oss.str();
}

std::string AdminService::ExportClaimsCsv(int limit) const {
  std::ostringstream oss;
  oss << "id,activity_id,uid,reward_key,created_at\n";
  if (!mysql_.Available()) return oss.str();
  auto rows = mysql_.Query(
      "SELECT id,activity_id,uid,reward_key,DATE_FORMAT(created_at,'%Y-%m-%d %H:%i:%s') FROM activity_claim ORDER BY "
      "id DESC LIMIT " +
      std::to_string(limit));
  if (!rows) return oss.str();
  for (const auto& row : *rows) {
    if (row.cols.size() < 5) continue;
    oss << row.cols[0] << ',' << row.cols[1] << ',' << row.cols[2] << ",\"" << Escape(row.cols[3]) << "\","
        << row.cols[4] << "\n";
  }
  return oss.str();
}

void AdminService::RecordRound(int64_t round_id, int64_t room_id, int template_id, const std::string& players_json,
                               int base_score, int multiplier) {
  auto job = [this, round_id, room_id, template_id, players_json, base_score, multiplier]() {
    if (!mysql_.Available()) {
      PLOG_WARN("RecordRound skipped: mysql unavailable round=" << round_id);
      return;
    }
    mysql_.ExecBind(
        "INSERT INTO game_round(round_id,room_id,game_id,template_id,players_json,base_score,multiplier,"
        "started_at,ended_at) VALUES(?,?,1,?,?,?,?,NOW(3),NOW(3)) "
        "ON DUPLICATE KEY UPDATE ended_at=NOW(3),multiplier=VALUES(multiplier),"
        "players_json=VALUES(players_json)",
        {I64(round_id), I64(room_id), I64(template_id), Str(players_json), I64(base_score), I64(multiplier)});
  };
  if (!persist_.Post(job)) job();
}

}  // namespace pandora
