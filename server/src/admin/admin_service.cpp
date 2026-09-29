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

// CSV field escape (double quotes); not for SQL.
std::string EscapeCsv(const std::string& s) {
  std::string o;
  o.reserve(s.size() + 8);
  for (char c : s) {
    if (c == '"') o.push_back('"');
    o.push_back(c);
  }
  return o;
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

std::string AdminService::MakeToken() const {
  static thread_local std::mt19937_64 rng{std::random_device{}()};
  std::uniform_int_distribution<uint64_t> dist;
  std::ostringstream oss;
  oss << std::hex << dist(rng) << dist(rng);
  return "adm_" + oss.str();
}

void AdminService::Bootstrap() {
  const std::string hash = HashPassword("admin123");
  auto rows = mysql_.Query("SELECT id FROM admin_user WHERE username='admin' LIMIT 1");
  if (rows && rows->empty()) {
    mysql_.ExecBind("INSERT INTO admin_user(username,password_hash,role,enabled) VALUES('admin',?,'super',1)",
                    {Str(hash)});
    PLOG_INFO("seeded admin_user admin/admin123");
  }
  lobby_.ReloadFromDb(mysql_);
  pay_.ReloadFromDb(mysql_);
  

  auto v = redis_.Get("ops:maintain");
  if (!v) redis_.Set("ops:maintain", "0");
  

}

AdminLoginResult AdminService::Login(const std::string& username, const std::string& password) {
  AdminLoginResult r;
  const std::string hash = HashPassword(password);
  auto rows = mysql_.QueryBind("SELECT id,role,password_hash,enabled FROM admin_user WHERE username=? LIMIT 1",
                               {Str(username)});
  if (!rows) {
    r.error = mysql_.LastError().empty() ? "mysql error" : mysql_.LastError();
    return r;
  }
  if (rows->empty()) {
    r.error = "invalid credentials";
    return r;
  }
  const auto& row = rows->front();
  if (!row.Bool("enabled") || row.Str("password_hash") != hash) {
    r.error = "invalid credentials";
    return r;
  }
  AdminSession sess;
  sess.admin_id = row.Int("id");
  sess.username = username;
  sess.role = ParseRole(row.Str("role"));
  sess.token = MakeToken();
  {
    std::lock_guard<std::mutex> lk(mu_);
    admin_sessions_[sess.token] = sess;
  }
  // admin_id|role|username
  redis_.Set("admin:session:" + sess.token,
             std::to_string(sess.admin_id) + "|" + RoleName(sess.role) + "|" + username, 86400);
  

  r.ok = true;
  r.token = sess.token;
  r.username = username;
  r.role = RoleName(sess.role);
  return r;
}

std::optional<AdminSession> AdminService::LoadSessionFromRedis(const std::string& token) const {
  if (token.empty()) return std::nullopt;
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
  auto rows = mysql_.QueryBind("SELECT enabled,role,username FROM admin_user WHERE id=? LIMIT 1",
                               {I64(s->admin_id)});
  if (!rows || rows->empty() || !rows->front().Bool("enabled")) return std::nullopt;
  s->role = ParseRole(rows->front().Str("role"));
  s->username = rows->front().Str("username");
  

  std::lock_guard<std::mutex> lk(mu_);
  admin_sessions_[token] = *s;
  return s;
}

bool AdminService::RequireRole(const AdminSession& s, AdminRole min_role) const {
  return RoleRank(s.role) >= RoleRank(min_role);
}

bool AdminService::IsMaintain() const {
  auto v = redis_.Get("ops:maintain");
  if (v) return *v == "1";
  

  return false;
}

void AdminService::SetMaintain(bool on) {
  redis_.Set("ops:maintain", on ? "1" : "0");
}

bool AdminService::IsBanned(int64_t uid) const {
  auto v = redis_.Get("user:ban:" + std::to_string(uid));
  if (v && *v == "1") return true;
  if (v && *v == "0") return false;
  

  auto rows = mysql_.QueryBind("SELECT status FROM `user` WHERE uid=? LIMIT 1", {I64(uid)});
  if (rows && !rows->empty()) return rows->front().Int("status") == 1;
  

  auto p = store_.GetPlayer(uid);
  return p && p->status == 1;
}

void AdminService::SetBanned(int64_t uid, bool ban) {
  store_.SetStatus(uid, ban ? 1 : 0);
  redis_.Set("user:ban:" + std::to_string(uid), ban ? "1" : "0");
}

void AdminService::Audit(int admin_id, const std::string& action, const std::string& target, const std::string& before,
                         const std::string& after) {
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
  if (auto rows = mysql_.Query("SELECT COUNT(*) AS cnt FROM `user`")) {
    if (rows && !rows->empty()) players = rows->front().I64("cnt");
  }
  if (auto rows = mysql_.Query("SELECT COUNT(*) AS cnt FROM game_round")) {
    if (rows && !rows->empty()) rounds = rows->front().I64("cnt");
  }
  if (auto rows = mysql_.Query("SELECT COUNT(*) AS cnt FROM pay_order")) {
    if (rows && !rows->empty()) orders = rows->front().I64("cnt");
  }
  

  return json{{"ccu", hub_.OnlineCount()},
              {"players", players},
              {"rounds", rounds},
              {"orders", orders},
              {"maintain", IsMaintain()},
              {"mysql", mysql_.Ping()},
              {"redis", redis_.Ping()}}
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
  const char* base =
      "SELECT u.uid,u.open_id,u.status,p.nickname,p.gold,p.diamond,"
      "DATE_FORMAT(u.created_at,'%Y-%m-%d %H:%i:%s') AS created_at "
      "FROM `user` u INNER JOIN player_profile p ON p.uid=u.uid";
  std::optional<std::vector<MysqlRow>> rows;
  if (!q.empty()) {
    const std::string pat = "%" + q + "%";
    rows = mysql_.QueryBind(
        std::string(base) +
            " WHERE CAST(u.uid AS CHAR) LIKE ? OR p.nickname LIKE ? OR u.open_id LIKE ?"
            " ORDER BY u.uid DESC LIMIT 500",
        {Str(pat), Str(pat), Str(pat)});
  } else {
    rows = mysql_.Query(std::string(base) + " ORDER BY u.uid DESC LIMIT 500");
  }
  if (rows) {
    for (const auto& row : *rows) {
      Row r;
      r.p.uid = row.I64("uid");
      if (r.p.uid <= 0) continue;
      r.p.open_id = row.Str("open_id");
      r.p.status = row.Int("status");
      r.p.nickname = row.Str("nickname");
      r.p.gold = row.I64("gold");
      r.p.diamond = row.I64("diamond");
      r.created_at = row.Str("created_at");
      filtered.push_back(std::move(r));
    }
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
  const char* cols =
      "SELECT id,uid,currency,delta,balance_after,biz_type,idempotent_key,ref_id,"
      "DATE_FORMAT(created_at,'%Y-%m-%d %H:%i:%s') AS created_at FROM ledger";
  std::optional<std::vector<MysqlRow>> rows;
  if (uid > 0) {
    rows = mysql_.QueryBind(std::string(cols) + " WHERE uid=? ORDER BY id DESC LIMIT 500", {I64(uid)});
  } else {
    rows = mysql_.Query(std::string(cols) + " ORDER BY id DESC LIMIT 500");
  }
  if (rows) {
    for (const auto& row : *rows) {
      Row r;
      r.e.id = row.I64("id");
      if (r.e.id <= 0) continue;
      r.e.uid = row.I64("uid");
      r.e.currency = row.Int("currency");
      r.e.delta = row.I64("delta");
      r.e.balance_after = row.I64("balance_after");
      r.e.biz_type = row.Str("biz_type");
      r.e.idempotent_key = row.Str("idempotent_key");
      r.e.ref_id = row.Str("ref_id");
      r.created_at = row.Str("created_at");
      all.push_back(std::move(r));
    }
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
  std::optional<std::vector<MysqlRow>> rows;
  if (uid > 0) {
    rows = mysql_.QueryBind(
        "SELECT g.round_id,g.room_id,g.template_id,CAST(g.players_json AS CHAR) AS players_json,g.base_score,g.multiplier,"
        "DATE_FORMAT(g.started_at,'%Y-%m-%d %H:%i:%s') AS started_at,"
        "DATE_FORMAT(g.ended_at,'%Y-%m-%d %H:%i:%s') AS ended_at "
        "FROM game_round_player p INNER JOIN game_round g ON g.round_id=p.round_id "
        "WHERE p.uid=? ORDER BY p.ended_at DESC LIMIT 200",
        {I64(uid)});
  } else {
    rows = mysql_.Query(
        "SELECT round_id,room_id,template_id,CAST(players_json AS CHAR) AS players_json,base_score,multiplier,"
        "DATE_FORMAT(started_at,'%Y-%m-%d %H:%i:%s') AS started_at,"
        "DATE_FORMAT(ended_at,'%Y-%m-%d %H:%i:%s') AS ended_at FROM game_round "
        "ORDER BY ended_at DESC LIMIT 200");
  }
  if (rows) {
    for (const auto& row : *rows) {
      RoundRow m;
      m.round_id = row.I64("round_id");
      if (m.round_id <= 0) continue;
      m.room_id = row.I64("room_id");
      m.template_id = row.Int("template_id");
      m.players_json = row.Str("players_json");
      m.base_score = row.Int("base_score");
      m.multiplier = row.Int("multiplier");
      m.started_at = row.Str("started_at");
      m.ended_at = row.Str("ended_at");
      rounds.push_back(m);
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
  lobby_.ReloadFromDb(mysql_);
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
  const int r = mysql_.ExecBind(
      "INSERT INTO room_template(id,game_id,name,base_score,rake_bp,min_gold,max_gold,enabled) "
      "VALUES(?,1,?,?,?,?,?,?) "
      "ON DUPLICATE KEY UPDATE name=VALUES(name),base_score=VALUES(base_score),rake_bp=VALUES(rake_bp),"
      "min_gold=VALUES(min_gold),max_gold=VALUES(max_gold),enabled=VALUES(enabled)",
      {I64(id), Str(name), I64(base_score), I64(rake_bp), I64(min_gold), I64(max_gold), I64(enabled ? 1 : 0)});
  if (r < 0) {
    if (err) *err = mysql_.LastError();
    return false;
  }
  lobby_.ReloadFromDb(mysql_);
  Audit(admin.admin_id, "put_template", std::to_string(id), "", name);
  return true;
}

std::string AdminService::ListProductsJson() const {
  pay_.ReloadFromDb(mysql_);
  auto products = pay_.ListProducts(true);
  json items = json::array();
  for (const auto& p : products) {
    json gift_items = json::array();
    try {
      gift_items = json::parse(p.gift_items_json.empty() ? "[]" : p.gift_items_json, nullptr, false);
      if (!gift_items.is_array()) gift_items = json::array();
    } catch (...) {
      gift_items = json::array();
    }
    items.push_back({{"id", p.id},
                     {"amount_fen", p.amount_fen},
                     {"diamond", p.diamond},
                     {"gift_diamond", p.gift_diamond},
                     {"gift_items", gift_items},
                     {"enabled", p.enabled}});
  }
  return json{{"items", std::move(items)}}.dump();
}

bool AdminService::UpsertProduct(int id, int amount_fen, int diamond, int gift, const std::string& gift_items_json,
                                 bool enabled, const AdminSession& admin, std::string* err) {
  if (!RequireRole(admin, AdminRole::kOps)) {
    if (err) *err = "forbidden";
    return false;
  }
  std::string gifts = gift_items_json.empty() ? "[]" : gift_items_json;
  try {
    auto j = json::parse(gifts, nullptr, false);
    if (!j.is_array()) {
      if (err) *err = "gift_items must be array";
      return false;
    }
    gifts = j.dump();
  } catch (...) {
    if (err) *err = "bad gift_items json";
    return false;
  }
  int r = 0;
  if (id <= 0) {
    r = mysql_.ExecBind(
        "INSERT INTO pay_product(amount_fen,diamond,gift_diamond,gift_items_json,sort,enabled) VALUES(?,?,?,?,0,?)",
        {I64(amount_fen), I64(diamond), I64(gift), Str(gifts), I64(enabled ? 1 : 0)});
  } else {
    r = mysql_.ExecBind(
        "INSERT INTO pay_product(id,amount_fen,diamond,gift_diamond,gift_items_json,sort,enabled) VALUES(?,?,?,?,?,0,?) "
        "ON DUPLICATE KEY UPDATE amount_fen=VALUES(amount_fen),diamond=VALUES(diamond),"
        "gift_diamond=VALUES(gift_diamond),gift_items_json=VALUES(gift_items_json),enabled=VALUES(enabled)",
        {I64(id), I64(amount_fen), I64(diamond), I64(gift), Str(gifts), I64(enabled ? 1 : 0)});
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
  if (id <= 0) {
    if (err) *err = "id required";
    return false;
  }
  const int r = mysql_.ExecBind("UPDATE pay_product SET enabled=? WHERE id=?",
                               {I64(enabled ? 1 : 0), I64(id)});
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
  redis_.Set("ops:announce:last", message, 86400);
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
  const char* base =
      "SELECT admin_id,action,target,CAST(before_json AS CHAR) AS before_json,"
      "CAST(after_json AS CHAR) AS after_json,"
      "DATE_FORMAT(created_at,'%Y-%m-%d %H:%i:%s') AS created_at "
      "FROM admin_audit";
  std::optional<std::vector<MysqlRow>> rows;
  if (!q.empty()) {
    const std::string pat = "%" + q + "%";
    rows = mysql_.QueryBind(std::string(base) + " WHERE action LIKE ? OR target LIKE ? ORDER BY id DESC LIMIT 200",
                            {Str(pat), Str(pat)});
  } else {
    rows = mysql_.Query(std::string(base) + " ORDER BY id DESC LIMIT 200");
  }
  if (rows) {
    for (const auto& row : *rows) {
      AuditRow a;
      a.admin_id = row.Int("admin_id");
      a.action = row.Str("action");
      a.target = row.Str("target");
      a.before = row.Str("before_json");
      a.after = row.Str("after_json");
      a.created_at = row.Str("created_at");
      audits.push_back(a);
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
  if (limit < 1) limit = 1;
  if (limit > 100000) limit = 100000;
  std::ostringstream oss;
  oss << "id,uid,currency,delta,balance_after,biz_type,idempotent_key,ref_id\n";
  auto rows = mysql_.QueryBind(
      "SELECT id,uid,currency,delta,balance_after,biz_type,idempotent_key,ref_id FROM ledger ORDER BY id "
      "DESC LIMIT ?",
      {I64(limit)});
  if (!rows) return oss.str();
  for (const auto& row : *rows) {
    oss << row.I64("id") << ',' << row.I64("uid") << ',' << row.Int("currency") << ',' << row.I64("delta") << ','
        << row.I64("balance_after") << ",\"" << EscapeCsv(row.Str("biz_type")) << "\",\""
        << EscapeCsv(row.Str("idempotent_key")) << "\",\"" << EscapeCsv(row.Str("ref_id")) << "\"\n";
  }
  return oss.str();
}

std::string AdminService::ExportRoundsCsv(int limit) const {
  if (limit < 1) limit = 1;
  if (limit > 100000) limit = 100000;
  std::ostringstream oss;
  oss << "round_id,room_id,template_id,base_score,multiplier,players_json\n";
  auto rows = mysql_.QueryBind(
      "SELECT round_id,room_id,template_id,base_score,multiplier,"
      "CAST(players_json AS CHAR) AS players_json FROM game_round ORDER BY "
      "ended_at DESC LIMIT ?",
      {I64(limit)});
  if (!rows) return oss.str();
  for (const auto& row : *rows) {
    oss << row.I64("round_id") << ',' << row.I64("room_id") << ',' << row.Int("template_id") << ','
        << row.Int("base_score") << ',' << row.Int("multiplier") << ",\"" << EscapeCsv(row.Str("players_json"))
        << "\"\n";
  }
  return oss.str();
}

std::string AdminService::ExportClaimsCsv(int limit) const {
  if (limit < 1) limit = 1;
  if (limit > 100000) limit = 100000;
  std::ostringstream oss;
  oss << "id,activity_id,uid,reward_key,created_at\n";
  auto rows = mysql_.QueryBind(
      "SELECT id,activity_id,uid,reward_key,"
      "DATE_FORMAT(created_at,'%Y-%m-%d %H:%i:%s') AS created_at FROM activity_claim ORDER BY "
      "id DESC LIMIT ?",
      {I64(limit)});
  if (!rows) return oss.str();
  for (const auto& row : *rows) {
    oss << row.I64("id") << ',' << row.Int("activity_id") << ',' << row.I64("uid") << ",\""
        << EscapeCsv(row.Str("reward_key")) << "\"," << row.Str("created_at") << "\n";
  }
  return oss.str();
}

void AdminService::RecordRound(int64_t round_id, int64_t room_id, int template_id, const std::string& players_json,
                               int base_score, int multiplier) {
  auto job = [this, round_id, room_id, template_id, players_json, base_score, multiplier]() {
    mysql_.ExecBind(
        "INSERT INTO game_round(round_id,room_id,game_id,template_id,players_json,base_score,multiplier,"
        "started_at,ended_at) VALUES(?,?,1,?,?,?,?,NOW(3),NOW(3)) "
        "ON DUPLICATE KEY UPDATE ended_at=NOW(3),multiplier=VALUES(multiplier),"
        "players_json=VALUES(players_json)",
        {I64(round_id), I64(room_id), I64(template_id), Str(players_json), I64(base_score), I64(multiplier)});

    mysql_.ExecBind("DELETE FROM game_round_player WHERE round_id=?", {I64(round_id)});
    try {
      const auto arr = json::parse(players_json, nullptr, false);
      if (!arr.is_array()) return;
      for (const auto& p : arr) {
        const int64_t puid = p.value("uid", static_cast<int64_t>(0));
        if (puid <= 0) continue;
        mysql_.ExecBind(
            "INSERT INTO game_round_player(round_id,uid,ended_at) VALUES(?,?,NOW(3)) "
            "ON DUPLICATE KEY UPDATE ended_at=VALUES(ended_at)",
            {I64(round_id), I64(puid)});
      }
    } catch (const std::exception& e) {
      PLOG_WARN("RecordRound index players err: " << e.what());
    }
  };
  if (!persist_.Post(job)) job();
}

}  // namespace pandora
