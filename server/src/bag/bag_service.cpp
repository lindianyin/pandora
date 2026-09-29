#include "bag/bag_service.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

#include <nlohmann/json.hpp>

#include "common/errors.hpp"
#include "common/log.hpp"
#include "common/proto_wire.hpp"

namespace pandora {
namespace {

using json = nlohmann::json;

int ParseInt(const std::string& s, int def = 0) {
  try {
    return std::stoi(s);
  } catch (...) {
    return def;
  }
}

int64_t ParseI64(const std::string& s, int64_t def = 0) {
  try {
    return std::stoll(s);
  } catch (...) {
    return def;
  }
}

std::string TrimExpireDisplay(std::string s) {
  if (s.size() > 19) s = s.substr(0, 19);
  if (!s.empty() && s.back() == '.') s.pop_back();
  return s;
}

std::string NowPlusSec(int sec) {
  using namespace std::chrono;
  const auto tp = system_clock::now() + seconds(sec);
  const std::time_t t = system_clock::to_time_t(tp);
  std::tm tm{};
#ifdef _WIN32
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  std::ostringstream oss;
  oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << ".000";
  return oss.str();
}

}  // namespace

BagService::BagService(MysqlClient& mysql, RedisClient& redis, SessionHub& hub, AsyncWorker& persist, BagConfig cfg)
    : mysql_(mysql), redis_(redis), hub_(hub), persist_(persist), cfg_(std::move(cfg)) {}

void BagService::Bootstrap() {
  // Ensure seed defs exist (migrate may already have inserted)
  mysql_.Exec(
      "INSERT INTO item_define(id,name,icon,kind,stackable,default_expire_sec,tag,enabled) VALUES"
      "(1001,'改名卡','','qty',1,0,'rename',1),"
      "(1002,'限时加倍券','','qty_ttl',1,86400,'ticket',1),"
      "(2001,'周卡体验','','ttl',1,604800,'pass',1) "
      "ON DUPLICATE KEY UPDATE name=VALUES(name)");
  ReloadDefs();
}

void BagService::ReloadDefs() {
  std::vector<ItemDef> loaded;
  auto rows = mysql_.Query(
      "SELECT id,name,icon,kind,stackable,default_expire_sec,tag,enabled FROM item_define ORDER BY id");
  if (rows) {
    for (const auto& row : *rows) {
      if (row.cols.size() < 8) continue;
      ItemDef d;
      d.id = ParseInt(row.cols[0]);
      d.name = row.cols[1];
      d.icon = row.cols[2];
      d.kind = row.cols[3];
      d.stackable = row.cols[4] == "1";
      d.default_expire_sec = ParseInt(row.cols[5]);
      d.tag = row.cols[6];
      d.enabled = row.cols[7] == "1";
      loaded.push_back(std::move(d));
    }
  }
  std::lock_guard<std::mutex> lk(mu_);
  defs_ = std::move(loaded);
  PLOG_INFO("bag defs loaded count=" << defs_.size());
}

std::vector<ItemDef> BagService::ListDefs(bool include_disabled) const {
  std::lock_guard<std::mutex> lk(mu_);
  std::vector<ItemDef> out;
  for (const auto& d : defs_)
    if (include_disabled || d.enabled) out.push_back(d);
  return out;
}

std::optional<ItemDef> BagService::GetDef(int item_id) const {
  std::lock_guard<std::mutex> lk(mu_);
  for (const auto& d : defs_)
    if (d.id == item_id) return d;
  return std::nullopt;
}

bool BagService::UpsertDef(const ItemDef& def, std::string* err) {
  if (def.kind != "qty" && def.kind != "qty_ttl" && def.kind != "ttl") {
    if (err) *err = "bad kind";
    return false;
  }
  ItemDef d = def;
  if (d.kind == "qty") {
    d.default_expire_sec = 0;
    d.stackable = true;
  } else if (d.kind == "ttl") {
    // 时效 buff：同 item 仅一行，stackable 语义固定
    d.stackable = true;
    if (d.default_expire_sec <= 0) d.default_expire_sec = 86400;
  } else if (d.kind == "qty_ttl") {
    if (d.default_expire_sec <= 0) d.default_expire_sec = 86400;
  }
  int r = 0;
  if (d.id <= 0) {
    r = mysql_.ExecBind(
        "INSERT INTO item_define(name,icon,kind,stackable,default_expire_sec,tag,enabled) VALUES(?,?,?,?,?,?,?)",
        {Str(d.name), Str(d.icon), Str(d.kind), I64(d.stackable ? 1 : 0), I64(d.default_expire_sec), Str(d.tag),
         I64(d.enabled ? 1 : 0)});
  } else {
    r = mysql_.ExecBind(
        "INSERT INTO item_define(id,name,icon,kind,stackable,default_expire_sec,tag,enabled) VALUES(?,?,?,?,?,?,?,?) "
        "ON DUPLICATE KEY UPDATE name=VALUES(name),icon=VALUES(icon),kind=VALUES(kind),"
        "stackable=VALUES(stackable),default_expire_sec=VALUES(default_expire_sec),tag=VALUES(tag),"
        "enabled=VALUES(enabled)",
        {I64(d.id), Str(d.name), Str(d.icon), Str(d.kind), I64(d.stackable ? 1 : 0), I64(d.default_expire_sec),
         Str(d.tag), I64(d.enabled ? 1 : 0)});
  }
  if (r < 0) {
    if (err) *err = mysql_.LastError();
    return false;
  }
  ReloadDefs();
  return true;
}

bool BagService::SetDefEnabled(int id, bool enabled, std::string* err) {
  auto def = GetDef(id);
  if (!def) {
    if (err) *err = "not found";
    return false;
  }
  def->enabled = enabled;
  return UpsertDef(*def, err);
}

std::string BagService::ResolveExpireAt(const ItemDef& def, const ExpirePolicy& policy) const {
  if (def.kind == "qty") return kNeverExpire;
  if (policy.mode == ExpirePolicy::Mode::kAbsolute && !policy.absolute.empty()) {
    std::string a = policy.absolute;
    if (a.size() == 19) a += ".000";
    return a;
  }
  return NowPlusSec(ResolveDurationSec(def, policy));
}

int BagService::ResolveDurationSec(const ItemDef& def, const ExpirePolicy& policy) const {
  if (policy.mode == ExpirePolicy::Mode::kDurationSec && policy.duration_sec > 0)
    return policy.duration_sec;
  if (def.default_expire_sec > 0) return def.default_expire_sec;
  return 86400;
}

bool BagService::LedgerExists(const std::string& idem) const {
  if (idem.empty()) return false;
  auto rows = mysql_.QueryBind("SELECT id FROM item_ledger WHERE idempotent_key=? LIMIT 1", {Str(idem)});
  return rows && !rows->empty();
}

void BagService::PushUpdate(int64_t uid, int item_id, int64_t qty, const std::string& expire_at, int reason) {
  hub_.Send(uid, MsgId::kS2C_BagUpdate,
            proto_wire::EncodeS2C_BagUpdate(item_id, qty, TrimExpireDisplay(expire_at), reason));
}

std::string BagService::ListDefsJson(bool include_disabled) const {
  json items = json::array();
  for (const auto& d : ListDefs(include_disabled)) {
    items.push_back({{"id", d.id},
                     {"name", d.name},
                     {"icon", d.icon},
                     {"kind", d.kind},
                     {"stackable", d.stackable},
                     {"default_expire_sec", d.default_expire_sec},
                     {"tag", d.tag},
                     {"enabled", d.enabled}});
  }
  return json{{"items", items}, {"total", items.size()}}.dump();
}

std::string BagService::ListBagJson(int64_t uid, bool include_expired) const {
  json items = json::array();
  std::string sql =
      "SELECT b.item_id,b.quantity,DATE_FORMAT(b.expire_at,'%Y-%m-%d %H:%i:%s'),"
      "d.name,d.kind,d.icon,d.tag FROM bag_item b "
      "INNER JOIN item_define d ON d.id=b.item_id WHERE b.uid=? AND b.quantity>0";
  if (!include_expired) sql += " AND b.expire_at>NOW(3)";
  sql += " ORDER BY b.item_id ASC, b.expire_at ASC";
  auto rows = mysql_.QueryBind(sql, {I64(uid)});
  if (rows) {
    for (const auto& row : *rows) {
      if (row.cols.size() < 7) continue;
      items.push_back({{"item_id", ParseInt(row.cols[0])},
                       {"quantity", ParseI64(row.cols[1])},
                       {"expire_at", TrimExpireDisplay(row.cols[2])},
                       {"name", row.cols[3]},
                       {"kind", row.cols[4]},
                       {"icon", row.cols[5]},
                       {"tag", row.cols[6]}});
    }
  }
  return json{{"items", items}}.dump();
}

std::string BagService::ListLedgersJson(int64_t uid, int item_id, int page, int page_size) const {
  if (page < 1) page = 1;
  if (page_size < 1) page_size = 20;
  if (page_size > 100) page_size = 100;
  json items = json::array();
  int total = 0;

  std::string where = " WHERE 1=1";
  std::vector<SqlArg> binds;
  if (uid > 0) {
    where += " AND uid=?";
    binds.push_back(I64(uid));
  }
  if (item_id > 0) {
    where += " AND item_id=?";
    binds.push_back(I64(item_id));
  }
  auto cnt = mysql_.QueryBind("SELECT COUNT(*) FROM item_ledger" + where, binds);
  if (cnt && !cnt->empty()) total = ParseInt(cnt->front().cols[0]);

  const int64_t offset = static_cast<int64_t>(page - 1) * page_size;
  binds.push_back(I64(page_size));
  binds.push_back(I64(offset));
  auto rows = mysql_.QueryBind(
      "SELECT id,uid,item_id,delta,quantity_after,DATE_FORMAT(expire_at,'%Y-%m-%d %H:%i:%s'),"
      "biz_type,idempotent_key,ref_id,DATE_FORMAT(created_at,'%Y-%m-%d %H:%i:%s') FROM item_ledger" +
          where + " ORDER BY id DESC LIMIT ? OFFSET ?",
      binds);
  if (rows) {
    for (const auto& row : *rows) {
      if (row.cols.size() < 10) continue;
      items.push_back({{"id", ParseI64(row.cols[0])},
                       {"uid", ParseI64(row.cols[1])},
                       {"item_id", ParseInt(row.cols[2])},
                       {"delta", ParseI64(row.cols[3])},
                       {"quantity_after", ParseI64(row.cols[4])},
                       {"expire_at", TrimExpireDisplay(row.cols[5])},
                       {"biz_type", row.cols[6]},
                       {"idempotent_key", row.cols[7]},
                       {"ref_id", row.cols[8]},
                       {"created_at", TrimExpireDisplay(row.cols[9])}});
    }
  }
  return json{{"items", items}, {"total", total}}.dump();
}

BagOpResult BagService::GrantTtl(int64_t uid, int item_id, int64_t quantity, const ExpirePolicy& policy,
                                 const std::string& idempotent_key, const std::string& biz_type,
                                 const std::string& ref_id, const ItemDef& def) {
  BagOpResult r;
  // ttl：持续有效的 buff；同 item_id 仅一行；再次获得则过期时间累加
  const int64_t units = quantity > 0 ? quantity : 1;
  int64_t keep_id = 0;

  auto any = mysql_.QueryBind(
      "SELECT id FROM bag_item WHERE uid=? AND item_id=? ORDER BY "
      "(expire_at>NOW(3)) DESC, expire_at DESC LIMIT 1",
      {I64(uid), I64(item_id)});
  if (any && !any->empty()) keep_id = ParseI64(any->front().cols[0]);

  int up = -1;
  if (policy.mode == ExpirePolicy::Mode::kAbsolute && !policy.absolute.empty()) {
    std::string abs = policy.absolute;
    if (abs.size() == 19) abs += ".000";
    if (keep_id > 0) {
      // max(未过期的 expire_at, absolute, NOW) —— 用 SQL 保证不回退
      up = mysql_.ExecBind(
          "UPDATE bag_item SET quantity=1, "
          "expire_at=GREATEST(CASE WHEN expire_at>NOW(3) THEN expire_at ELSE NOW(3) END, ?), "
          "updated_at=NOW(3) WHERE id=?",
          {Str(abs), I64(keep_id)});
    } else {
      up = mysql_.ExecBind(
          "INSERT INTO bag_item(uid,item_id,quantity,expire_at,updated_at) VALUES(?,?,1,?,NOW(3))",
          {I64(uid), I64(item_id), Str(abs)});
    }
  } else {
    const int unit_sec = ResolveDurationSec(def, policy);
    int64_t add_sec = static_cast<int64_t>(unit_sec) * units;
    if (add_sec <= 0) add_sec = 86400;
    if (add_sec > 86400LL * 3650) add_sec = 86400LL * 3650;

    if (keep_id > 0) {
      // 未过期：在现有 expire 上累加；已过期：从 NOW 累加
      up = mysql_.ExecBind(
          "UPDATE bag_item SET quantity=1, "
          "expire_at=DATE_ADD(GREATEST(expire_at, NOW(3)), INTERVAL ? SECOND), "
          "updated_at=NOW(3) WHERE id=?",
          {I64(add_sec), I64(keep_id)});
    } else {
      up = mysql_.ExecBind(
          "INSERT INTO bag_item(uid,item_id,quantity,expire_at,updated_at) "
          "VALUES(?,?,1,DATE_ADD(NOW(3), INTERVAL ? SECOND),NOW(3))",
          {I64(uid), I64(item_id), I64(add_sec)});
    }
  }
  if (up < 0) {
    r.error = mysql_.LastError();
    r.err_code = static_cast<int>(Err::kInternal);
    return r;
  }

  auto keep = mysql_.QueryBind(
      "SELECT id, DATE_FORMAT(expire_at,'%Y-%m-%d %H:%i:%s.%f') FROM bag_item "
      "WHERE uid=? AND item_id=? ORDER BY expire_at DESC LIMIT 1",
      {I64(uid), I64(item_id)});
  if (!keep || keep->empty() || keep->front().cols.size() < 2) {
    r.error = "ttl grant missing row";
    r.err_code = static_cast<int>(Err::kInternal);
    return r;
  }
  keep_id = ParseI64(keep->front().cols[0]);
  const std::string new_expire = keep->front().cols[1];
  mysql_.ExecBind("DELETE FROM bag_item WHERE uid=? AND item_id=? AND id<>?",
                  {I64(uid), I64(item_id), I64(keep_id)});

  const int lr = mysql_.ExecBind(
      "INSERT INTO item_ledger(uid,item_id,delta,quantity_after,expire_at,biz_type,idempotent_key,ref_id,created_at) "
      "VALUES(?,?,?,?,?,?,?,?,NOW(3))",
      {I64(uid), I64(item_id), I64(units), I64(1), Str(new_expire), Str(biz_type), Str(idempotent_key),
       Str(ref_id)});
  if (lr < 0) {
    if (LedgerExists(idempotent_key)) {
      r.ok = true;
      r.quantity_after = GetQuantity(uid, item_id);
      r.expire_at = TrimExpireDisplay(new_expire);
      return r;
    }
    r.error = mysql_.LastError();
    r.err_code = static_cast<int>(Err::kInternal);
    return r;
  }
  r.ok = true;
  r.quantity_after = 1;
  r.expire_at = TrimExpireDisplay(new_expire);
  PushUpdate(uid, item_id, 1, new_expire, 1);
  return r;
}

BagOpResult BagService::Grant(int64_t uid, int item_id, int64_t quantity, const ExpirePolicy& policy,
                              const std::string& idempotent_key, const std::string& biz_type,
                              const std::string& ref_id) {
  BagOpResult r;
  if (uid <= 0 || item_id <= 0 || quantity <= 0 || idempotent_key.empty()) {
    r.error = "bad param";
    r.err_code = static_cast<int>(Err::kBadParam);
    return r;
  }
  if (LedgerExists(idempotent_key)) {
    r.ok = true;
    r.quantity_after = GetQuantity(uid, item_id);
    return r;
  }
  auto def = GetDef(item_id);
  if (!def || !def->enabled) {
    r.error = "item unavailable";
    r.err_code = static_cast<int>(Err::kItemUnavailable);
    return r;
  }

  if (def->kind == "ttl") {
    return GrantTtl(uid, item_id, quantity, policy, idempotent_key, biz_type, ref_id, *def);
  }

  const std::string expire_at = ResolveExpireAt(*def, policy);

  // Cap rows per uid (soft)
  if (cfg_.max_rows_per_uid > 0) {
    auto cnt = mysql_.QueryBind("SELECT COUNT(*) FROM bag_item WHERE uid=? AND quantity>0", {I64(uid)});
    if (cnt && !cnt->empty() && ParseInt(cnt->front().cols[0]) >= cfg_.max_rows_per_uid) {
      // still allow stacking onto existing rows
      auto exist = mysql_.QueryBind(
          "SELECT id FROM bag_item WHERE uid=? AND item_id=? AND expire_at=? LIMIT 1",
          {I64(uid), I64(item_id), Str(expire_at)});
      if (!exist || exist->empty()) {
        r.error = "bag full";
        r.err_code = static_cast<int>(Err::kBadParam);
        return r;
      }
    }
  }

  const int up = mysql_.ExecBind(
      "INSERT INTO bag_item(uid,item_id,quantity,expire_at,updated_at) VALUES(?,?,?,?,NOW(3)) "
      "ON DUPLICATE KEY UPDATE quantity=quantity+VALUES(quantity), updated_at=NOW(3)",
      {I64(uid), I64(item_id), I64(quantity), Str(expire_at)});
  if (up < 0) {
    r.error = mysql_.LastError();
    r.err_code = static_cast<int>(Err::kInternal);
    return r;
  }
  auto qrows = mysql_.QueryBind(
      "SELECT quantity FROM bag_item WHERE uid=? AND item_id=? AND expire_at=? LIMIT 1",
      {I64(uid), I64(item_id), Str(expire_at)});
  int64_t after = quantity;
  if (qrows && !qrows->empty()) after = ParseI64(qrows->front().cols[0]);

  const int lr = mysql_.ExecBind(
      "INSERT INTO item_ledger(uid,item_id,delta,quantity_after,expire_at,biz_type,idempotent_key,ref_id,created_at) "
      "VALUES(?,?,?,?,?,?,?,?,NOW(3))",
      {I64(uid), I64(item_id), I64(quantity), I64(after), Str(expire_at), Str(biz_type), Str(idempotent_key),
       Str(ref_id)});
  if (lr < 0) {
    // unique conflict = idempotent race
    if (LedgerExists(idempotent_key)) {
      r.ok = true;
      r.quantity_after = GetQuantity(uid, item_id);
      return r;
    }
    r.error = mysql_.LastError();
    r.err_code = static_cast<int>(Err::kInternal);
    return r;
  }
  r.ok = true;
  r.quantity_after = after;
  r.expire_at = TrimExpireDisplay(expire_at);
  PushUpdate(uid, item_id, after, expire_at, 1);
  return r;
}

BagOpResult BagService::Consume(int64_t uid, int item_id, int64_t quantity, const std::string& prefer_expire_at,
                                const std::string& idempotent_key, const std::string& biz_type,
                                const std::string& ref_id) {
  BagOpResult r;
  if (uid <= 0 || item_id <= 0 || quantity <= 0 || idempotent_key.empty()) {
    r.error = "bad param";
    r.err_code = static_cast<int>(Err::kBadParam);
    return r;
  }
  if (LedgerExists(idempotent_key)) {
    r.ok = true;
    r.quantity_after = GetQuantity(uid, item_id);
    return r;
  }
  auto def = GetDef(item_id);
  if (!def) {
    r.error = "item unavailable";
    r.err_code = static_cast<int>(Err::kItemUnavailable);
    return r;
  }

  // FIFO: earliest expire first among non-expired
  std::string sql =
      "SELECT id,quantity,DATE_FORMAT(expire_at,'%Y-%m-%d %H:%i:%s.%f') FROM bag_item "
      "WHERE uid=? AND item_id=? AND quantity>0 AND expire_at>NOW(3)";
  std::vector<SqlArg> binds{I64(uid), I64(item_id)};
  if (!prefer_expire_at.empty()) {
    std::string pref = prefer_expire_at;
    if (pref.size() > 19) pref = pref.substr(0, 19);
    sql += " AND DATE_FORMAT(expire_at,'%Y-%m-%d %H:%i:%s')=?";
    binds.push_back(Str(pref));
  }
  sql += " ORDER BY expire_at ASC";
  auto rows = mysql_.QueryBind(sql, binds);
  if (!rows || rows->empty()) {
    // distinguish expired-only vs none
    auto any = mysql_.QueryBind(
        "SELECT SUM(quantity) FROM bag_item WHERE uid=? AND item_id=? AND quantity>0", {I64(uid), I64(item_id)});
    const int64_t total = (any && !any->empty()) ? ParseI64(any->front().cols[0]) : 0;
    if (total > 0) {
      r.error = "item expired";
      r.err_code = static_cast<int>(Err::kItemExpired);
    } else {
      r.error = "insufficient item";
      r.err_code = static_cast<int>(Err::kItemInsufficient);
    }
    return r;
  }
  int64_t usable = 0;
  for (const auto& row : *rows) {
    if (row.cols.size() >= 2) usable += ParseI64(row.cols[1]);
  }
  if (usable < quantity) {
    r.error = "insufficient item";
    r.err_code = static_cast<int>(Err::kItemInsufficient);
    return r;
  }

  int64_t need = quantity;
  int64_t last_after = 0;
  std::string last_expire;
  for (const auto& row : *rows) {
    if (need <= 0) break;
    if (row.cols.size() < 3) continue;
    const int64_t row_id = ParseI64(row.cols[0]);
    const int64_t have = ParseI64(row.cols[1]);
    const std::string exp = row.cols[2];
    const int64_t take = have < need ? have : need;
    const int64_t left = have - take;
    if (left <= 0) {
      mysql_.ExecBind("DELETE FROM bag_item WHERE id=?", {I64(row_id)});
      last_after = 0;
    } else {
      mysql_.ExecBind("UPDATE bag_item SET quantity=?, updated_at=NOW(3) WHERE id=?", {I64(left), I64(row_id)});
      last_after = left;
    }
    last_expire = exp;
    const int lr = mysql_.ExecBind(
        "INSERT INTO item_ledger(uid,item_id,delta,quantity_after,expire_at,biz_type,idempotent_key,ref_id,created_at) "
        "VALUES(?,?,?,?,?,?,?,?,NOW(3))",
        {I64(uid), I64(item_id), I64(-take), I64(left), Str(exp), Str(biz_type),
         Str(need == quantity ? idempotent_key : idempotent_key + ":" + std::to_string(row_id)), Str(ref_id)});
    if (lr < 0 && need == quantity) {
      if (LedgerExists(idempotent_key)) {
        r.ok = true;
        r.quantity_after = GetQuantity(uid, item_id);
        return r;
      }
      r.error = mysql_.LastError();
      r.err_code = static_cast<int>(Err::kInternal);
      return r;
    }
    need -= take;
  }
  if (need > 0) {
    r.error = "insufficient item";
    r.err_code = static_cast<int>(Err::kItemInsufficient);
    return r;
  }
  r.ok = true;
  r.quantity_after = GetQuantity(uid, item_id);
  r.expire_at = TrimExpireDisplay(last_expire);
  PushUpdate(uid, item_id, r.quantity_after, last_expire, 2);
  return r;
}

int64_t BagService::GetQuantity(int64_t uid, int item_id) const {
  auto rows = mysql_.QueryBind(
      "SELECT COALESCE(SUM(quantity),0) FROM bag_item WHERE uid=? AND item_id=? AND quantity>0 AND expire_at>NOW(3)",
      {I64(uid), I64(item_id)});
  if (!rows || rows->empty()) return 0;
  return ParseI64(rows->front().cols[0]);
}

bool BagService::GrantItemsFromJson(int64_t uid, const std::string& items_json_array, const std::string& parent_idem,
                                    const std::string& biz_type, const std::string& ref_id, std::string* err) {
  json arr = json::array();
  try {
    arr = json::parse(items_json_array.empty() ? "[]" : items_json_array, nullptr, false);
  } catch (...) {
    arr = json::array();
  }
  if (!arr.is_array()) {
    if (err) *err = "items must be array";
    return false;
  }
  int i = 0;
  for (const auto& it : arr) {
    if (!it.is_object()) continue;
    const int item_id = it.value("item_id", 0);
    const int64_t quantity = it.value("quantity", static_cast<int64_t>(0));
    if (item_id <= 0 || quantity <= 0) continue;
    ExpirePolicy pol;
    const int expire_sec = it.value("expire_sec", 0);
    auto def = GetDef(item_id);
    if (!def) {
      if (err) *err = "item " + std::to_string(item_id) + " not found";
      return false;
    }
    if (def->kind == "qty") {
      pol.mode = ExpirePolicy::Mode::kNone;
    } else if (expire_sec > 0) {
      pol.mode = ExpirePolicy::Mode::kDurationSec;
      pol.duration_sec = expire_sec;
    } else {
      pol.mode = ExpirePolicy::Mode::kDurationSec;
      pol.duration_sec = def->default_expire_sec > 0 ? def->default_expire_sec : 86400;
    }
    const std::string idem = parent_idem + ":" + std::to_string(item_id) + ":" + std::to_string(i);
    auto gr = Grant(uid, item_id, quantity, pol, idem, biz_type, ref_id);
    if (!gr.ok) {
      if (err) *err = gr.error;
      return false;
    }
    ++i;
  }
  return true;
}

}  // namespace pandora
