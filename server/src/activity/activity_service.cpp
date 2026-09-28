#include "activity/activity_service.hpp"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <sstream>

#include <nlohmann/json.hpp>

#include "common/errors.hpp"
#include "common/log.hpp"
#include "common/proto_wire.hpp"

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

ActivityService::ActivityService(MysqlClient& mysql, RedisClient& redis, WalletService& wallet, BagService& bag,
                                 SessionHub& hub, AsyncWorker& persist)
    : mysql_(mysql), redis_(redis), wallet_(wallet), bag_(bag), hub_(hub), persist_(persist) {}

std::string ActivityService::EscapeSql(const std::string& s) const {
  std::string o;
  for (char c : s) {
    if (c == '\'' || c == '\\') o.push_back('\\');
    o.push_back(c);
  }
  return o;
}

int ActivityService::ExtractInt(const std::string& json, const std::string& key, int def) const {
  const std::string pat = "\"" + key + "\"";
  auto p = json.find(pat);
  if (p == std::string::npos) return def;
  p = json.find(':', p);
  if (p == std::string::npos) return def;
  ++p;
  while (p < json.size() && (json[p] == ' ' || json[p] == '\t')) ++p;
  try {
    return std::stoi(json.substr(p));
  } catch (...) {
    return def;
  }
}

std::string ActivityService::ExtractStr(const std::string& json, const std::string& key, const std::string& def) const {
  const std::string pat = "\"" + key + "\"";
  auto p = json.find(pat);
  if (p == std::string::npos) return def;
  p = json.find(':', p);
  if (p == std::string::npos) return def;
  p = json.find('"', p);
  if (p == std::string::npos) return def;
  ++p;
  auto e = json.find('"', p);
  if (e == std::string::npos) return def;
  return json.substr(p, e - p);
}

std::string ActivityService::TodayKey() const {
  const auto now = std::chrono::system_clock::now();
  const std::time_t t = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
#ifdef _WIN32
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
  return buf;
}

bool ActivityService::InWindow(const ActivityDef& def) const {
  (void)def;
  return def.enabled;
}

void ActivityService::EnsureStockSchema() {
  if (!mysql_.Available()) return;
  mysql_.Exec(
      "CREATE TABLE IF NOT EXISTS `activity_stock` ("
      "`activity_id` INT NOT NULL,"
      "`remain` INT NOT NULL,"
      "`updated_at` DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3) ON UPDATE CURRENT_TIMESTAMP(3),"
      "PRIMARY KEY (`activity_id`)"
      ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4");
}

void ActivityService::InitGiftStock(const ActivityDef& d) {
  if (d.type != "gift") return;
  const int stock = ExtractInt(d.rules_json, "stock", 0);
  const std::string rkey = "act:stock:" + std::to_string(d.id);
  if (mysql_.Available()) {
    mysql_.ExecBind("INSERT INTO activity_stock(activity_id,remain) VALUES(?,?) ON DUPLICATE KEY UPDATE activity_id=activity_id",
                    {I64(d.id), I64(stock)});
  }
  if (redis_.Available()) {
    auto cur = redis_.Get(rkey);
    if (!cur || cur->empty()) redis_.Set(rkey, std::to_string(stock));
  }
}

void ActivityService::EnsureSeed() {
  auto existing = ListDefs(true);
  if (!existing.empty()) return;

  const std::vector<ActivityDef> seeds = {
      {0, "sign", "每日签到",
       "{\"reward_key\":\"daily\",\"reward\":{\"currency\":1,\"amount\":500}}", true, "",
       ""},
      {0, "task", "对局任务", "{\"target_games\":3,\"reward_key\":\"games_3\",\"reward\":{\"currency\":2,\"amount\":10}}",
       true, "", ""},
      {0, "gift", "新手礼包",
       "{\"stock\":100,\"price\":{\"currency\":2,\"amount\":0},\"reward_key\":\"gift1\",\"reward\":{\"currency\":1,"
       "\"amount\":2000}}",
       true, "", ""},
  };
  for (auto s : seeds) {
    std::string err;
    UpsertDef(s, &err);
  }
  PLOG_INFO("activity seeds ensured");
}

void ActivityService::Bootstrap() {
  EnsureStockSchema();
  Reload();
  EnsureSeed();
  Reload();
  for (const auto& d : ListDefs(true)) InitGiftStock(d);
}

void ActivityService::Reload() {
  std::vector<ActivityDef> loaded;
  if (!mysql_.Available()) {
    PLOG_WARN("activity Reload skipped: mysql unavailable");
    return;
  }
  auto rows = mysql_.Query(
      "SELECT id,type,title,CAST(rules_json AS CHAR),DATE_FORMAT(start_at,'%Y-%m-%d %H:%i:%s.%f'),"
      "DATE_FORMAT(end_at,'%Y-%m-%d %H:%i:%s.%f'),enabled FROM activity_define ORDER BY id");
  if (rows) {
    for (const auto& row : *rows) {
      if (row.cols.size() < 7) continue;
      ActivityDef d;
      d.id = std::stoi(row.cols[0]);
      d.type = row.cols[1];
      d.title = row.cols[2];
      d.rules_json = row.cols[3];
      d.start_at = row.cols[4];
      d.end_at = row.cols[5];
      // 哨兵时间对外视为空窗
      if (d.start_at.rfind("1970-01-01", 0) == 0) d.start_at.clear();
      if (d.end_at.rfind("9999-12-31", 0) == 0) d.end_at.clear();
      d.enabled = row.cols[6] == "1";
      loaded.push_back(d);
    }
  }
  std::lock_guard<std::mutex> lk(mu_);
  defs_ = std::move(loaded);
  PLOG_INFO("activity defs loaded count=" << defs_.size());
}

std::vector<ActivityDef> ActivityService::ListDefs(bool include_disabled) const {
  std::lock_guard<std::mutex> lk(mu_);
  std::vector<ActivityDef> out;
  for (const auto& d : defs_)
    if (include_disabled || d.enabled) out.push_back(d);
  return out;
}

std::optional<ActivityDef> ActivityService::GetDef(int id) const {
  std::lock_guard<std::mutex> lk(mu_);
  for (const auto& d : defs_)
    if (d.id == id) return d;
  return std::nullopt;
}

bool ActivityService::UpsertDef(const ActivityDef& def, std::string* err) {
  if (def.type != "sign" && def.type != "task" && def.type != "gift") {
    if (err) *err = "bad type";
    return false;
  }
  if (!mysql_.Available()) {
    if (err) *err = "mysql unavailable";
    return false;
  }
  if (def.id <= 0) {
    const std::string start = def.start_at.empty() ? "1970-01-01 00:00:00.000" : def.start_at;
    const std::string end = def.end_at.empty() ? "9999-12-31 23:59:59.999" : def.end_at;
    const int r = mysql_.ExecBind(
        "INSERT INTO activity_define(type,title,rules_json,start_at,end_at,enabled) VALUES(?,?,?,?,?,?)",
        {Str(def.type), Str(def.title), Str(def.rules_json), Str(start), Str(end), I64(def.enabled ? 1 : 0)});
    if (r < 0) {
      if (err) *err = mysql_.LastError();
      return false;
    }
  } else {
    const std::string start = def.start_at.empty() ? "1970-01-01 00:00:00.000" : def.start_at;
    const std::string end = def.end_at.empty() ? "9999-12-31 23:59:59.999" : def.end_at;
    const int r = mysql_.ExecBind(
        "INSERT INTO activity_define(id,type,title,rules_json,start_at,end_at,enabled) VALUES(?,?,?,?,?,?,?) "
        "ON DUPLICATE KEY UPDATE type=VALUES(type),title=VALUES(title),"
        "rules_json=VALUES(rules_json),start_at=VALUES(start_at),end_at=VALUES(end_at),enabled=VALUES(enabled)",
        {I64(def.id), Str(def.type), Str(def.title), Str(def.rules_json), Str(start), Str(end),
         I64(def.enabled ? 1 : 0)});
    if (r < 0) {
      if (err) *err = mysql_.LastError();
      return false;
    }
  }
  Reload();
  if (def.type == "gift") {
    auto id = def.id;
    if (id <= 0) {
      for (const auto& d : ListDefs(true)) {
        if (d.type == "gift" && d.title == def.title) id = d.id;
      }
    }
    if (id > 0) {
      ActivityDef g = def;
      g.id = id;
      InitGiftStock(g);
    }
  }
  return true;
}

bool ActivityService::SetEnabled(int id, bool enabled, std::string* err) {
  auto def = GetDef(id);
  if (!def) {
    if (err) *err = "not found";
    return false;
  }
  def->enabled = enabled;
  return UpsertDef(*def, err);
}

int ActivityService::ClaimCount(int activity_id) const {
  if (!mysql_.Available()) return 0;
  auto rows = mysql_.QueryBind("SELECT COUNT(*) FROM activity_claim WHERE activity_id=?", {I64(activity_id)});
  if (rows && !rows->empty() && !rows->front().cols.empty()) {
    try {
      return std::stoi(rows->front().cols[0]);
    } catch (...) {
    }
  }
  return 0;
}

std::string ActivityService::LoadProgress(int64_t uid, int aid) {
  const std::string mk = std::to_string(aid) + ":" + std::to_string(uid);
  if (redis_.Available()) {
    auto v = redis_.Get("act:prog:" + mk);
    if (v && !v->empty()) return *v;
  }
  if (mysql_.Available()) {
    auto rows = mysql_.QueryBind(
        "SELECT CAST(progress_json AS CHAR) FROM activity_progress WHERE activity_id=? AND uid=? LIMIT 1",
        {I64(aid), I64(uid)});
    if (rows && !rows->empty() && !rows->front().cols.empty()) {
      const auto& json = rows->front().cols[0];
      if (redis_.Available()) redis_.Set("act:prog:" + mk, json, 86400);
      return json;
    }
  }
  return "{}";
}

void ActivityService::SaveProgress(int64_t uid, int aid, const std::string& json) {
  const std::string mk = std::to_string(aid) + ":" + std::to_string(uid);
  // Hot path: refresh Redis immediately; MySQL offloaded.
  if (redis_.Available()) {
    if (!redis_.Set("act:prog:" + mk, json, 86400)) PLOG_WARN("SaveProgress redis fail: " << redis_.LastError());
  }
  auto job = [this, uid, aid, json]() {
    if (!mysql_.Available()) {
      PLOG_WARN("SaveProgress skipped: mysql unavailable");
      return;
    }
    const int r = mysql_.ExecBind(
        "INSERT INTO activity_progress(activity_id,uid,progress_json) VALUES(?,?,?) "
        "ON DUPLICATE KEY UPDATE progress_json=VALUES(progress_json)",
        {I64(aid), I64(uid), Str(json)});
    if (r < 0) PLOG_WARN("SaveProgress mysql fail: " << mysql_.LastError());
  };
  if (!persist_.Post(job)) job();
}

bool ActivityService::HasClaimed(int64_t uid, int aid, const std::string& reward_key) {
  const std::string ck = "act:claim:" + std::to_string(aid) + ":" + std::to_string(uid) + ":" + reward_key;
  if (redis_.Available()) {
    auto v = redis_.Get(ck);
    if (v && !v->empty()) return true;
  }
  if (mysql_.Available()) {
    auto rows = mysql_.QueryBind(
        "SELECT id FROM activity_claim WHERE activity_id=? AND uid=? AND reward_key=? LIMIT 1",
        {I64(aid), I64(uid), Str(reward_key)});
    if (rows && !rows->empty()) {
      if (redis_.Available()) redis_.Set(ck, "1");
      return true;
    }
  }
  return false;
}

void ActivityService::MarkClaimed(int64_t uid, int aid, const std::string& reward_key) {
  const std::string ck = "act:claim:" + std::to_string(aid) + ":" + std::to_string(uid) + ":" + reward_key;
  if (mysql_.Available()) {
    const int r = mysql_.ExecBind("INSERT IGNORE INTO activity_claim(activity_id,uid,reward_key) VALUES(?,?,?)",
                                  {I64(aid), I64(uid), Str(reward_key)});
    if (r < 0) PLOG_WARN("MarkClaimed mysql fail: " << mysql_.LastError());
  } else {
    PLOG_WARN("MarkClaimed skipped: mysql unavailable");
  }
  if (redis_.Available()) redis_.Set(ck, "1");
}

int ActivityService::ReadStock(int aid) const {
  const std::string rkey = "act:stock:" + std::to_string(aid);
  if (redis_.Available()) {
    auto v = redis_.Get(rkey);
    if (v && !v->empty()) {
      try {
        return std::stoi(*v);
      } catch (...) {
      }
    }
  }
  if (mysql_.Available()) {
    auto rows = mysql_.QueryBind("SELECT remain FROM activity_stock WHERE activity_id=? LIMIT 1", {I64(aid)});
    if (rows && !rows->empty() && !rows->front().cols.empty()) {
      try {
        const int n = std::stoi(rows->front().cols[0]);
        if (redis_.Available()) redis_.Set(rkey, std::to_string(n));
        return n;
      } catch (...) {
      }
    }
  }
  return 0;
}

bool ActivityService::DecrStock(int aid, int& remain) {
  const std::string rkey = "act:stock:" + std::to_string(aid);

  // MySQL CAS first (source of truth), then sync Redis
  if (mysql_.Available()) {
    const int r = mysql_.ExecBind("UPDATE activity_stock SET remain=remain-1 WHERE activity_id=? AND remain>0",
                                  {I64(aid)});
    if (r <= 0) {
      remain = ReadStock(aid);
      return false;
    }
    auto rows = mysql_.QueryBind("SELECT remain FROM activity_stock WHERE activity_id=? LIMIT 1", {I64(aid)});
    remain = 0;
    if (rows && !rows->empty() && !rows->front().cols.empty()) {
      try {
        remain = std::stoi(rows->front().cols[0]);
      } catch (...) {
        remain = 0;
      }
    }
    if (redis_.Available()) redis_.Set(rkey, std::to_string(remain));
    return true;
  }

  // Redis-only fallback if MySQL down (still no in-process memory)
  if (redis_.Available()) {
    auto cur = redis_.Get(rkey);
    int n = 0;
    try {
      n = cur && !cur->empty() ? std::stoi(*cur) : 0;
    } catch (...) {
      n = 0;
    }
    if (n <= 0) {
      remain = 0;
      return false;
    }
    auto after = redis_.Decr(rkey);
    if (!after) {
      remain = 0;
      return false;
    }
    if (*after < 0) {
      redis_.Set(rkey, "0");
      remain = 0;
      return false;
    }
    remain = static_cast<int>(*after);
    return true;
  }

  remain = 0;
  return false;
}

void ActivityService::PushUpdate(int64_t uid, const ActivityDef& def, const std::string& progress_json, bool claimable) {
  try {
    hub_.Send(uid, MsgId::kS2C_ActivityUpdate,
              proto_wire::EncodeS2C_ActivityUpdate(def.id, def.type, progress_json, claimable));
  } catch (...) {
    PLOG_WARN("ActivityUpdate push failed uid=" << uid);
  }
}

void ActivityService::OnLogin(int64_t uid) {
  try {
    for (const auto& d : ListDefs(false)) {
      if (d.type != "sign") continue;
      auto prog = LoadProgress(uid, d.id);
      const std::string today = TodayKey();
      const std::string last = ExtractStr(prog, "last_sign_day", "");
      const bool signed_today = (last == today);
      const json progress = {{"last_sign_day", last}, {"signed_today", signed_today}};
      const std::string progress_s = progress.dump();
      if (prog.find("last_sign_day") == std::string::npos) SaveProgress(uid, d.id, progress_s);
      const std::string rk = ExtractStr(d.rules_json, "reward_key", "daily");
      const bool claimable = !signed_today && !HasClaimed(uid, d.id, rk + ":" + today);
      PushUpdate(uid, d, progress_s, claimable);
    }
  } catch (const std::exception& e) {
    PLOG_WARN("OnLogin activity err: " << e.what());
  } catch (...) {
    PLOG_WARN("OnLogin activity err");
  }
}

void ActivityService::OnGameSettled(int64_t uid, int template_id) {
  (void)template_id;
  try {
    for (const auto& d : ListDefs(false)) {
      if (d.type != "task") continue;
      const int target = ExtractInt(d.rules_json, "target_games", 3);
      auto prog = LoadProgress(uid, d.id);
      int games = ExtractInt(prog, "games", 0) + 1;
      const json progress = {{"games", games}, {"target", target}};
      const std::string progress_s = progress.dump();
      SaveProgress(uid, d.id, progress_s);
      const std::string rk = ExtractStr(d.rules_json, "reward_key", "games_3");
      const bool claimable = games >= target && !HasClaimed(uid, d.id, rk);
      PushUpdate(uid, d, progress_s, claimable);
    }
  } catch (const std::exception& e) {
    PLOG_WARN("OnGameSettled activity err: " << e.what());
  } catch (...) {
    PLOG_WARN("OnGameSettled activity err");
  }
}

std::string ActivityService::ListForPlayerJson(int64_t uid) {
  json items = json::array();
  for (const auto& d : ListDefs(false)) {
    if (!InWindow(d)) continue;
    auto prog = LoadProgress(uid, d.id);
    const std::string rk = ExtractStr(d.rules_json, "reward_key", "reward");
    bool claimable = false;
    bool claimed = false;
    if (d.type == "sign") {
      const std::string today = TodayKey();
      const std::string claim_key = rk + ":" + today;
      claimed = HasClaimed(uid, d.id, claim_key) || ExtractStr(prog, "last_sign_day", "") == today;
      claimable = !claimed;
    } else if (d.type == "task") {
      claimed = HasClaimed(uid, d.id, rk);
      const int games = ExtractInt(prog, "games", 0);
      const int target = ExtractInt(d.rules_json, "target_games", 3);
      claimable = !claimed && games >= target;
    } else if (d.type == "gift") {
      claimed = HasClaimed(uid, d.id, rk);
      const int stock = ReadStock(d.id);
      claimable = !claimed && stock > 0;
      if (prog == "{}") {
        prog = json{{"stock_left", stock}}.dump();
      }
    }
    items.push_back({{"id", d.id},
                     {"type", d.type},
                     {"title", d.title},
                     {"rules_json", ParseJsonOr(d.rules_json, json::object())},
                     {"progress_json", ParseJsonOr(prog.empty() ? "{}" : prog, json::object())},
                     {"claimable", claimable},
                     {"claimed", claimed},
                     {"reward_key", rk}});
  }
  return json{{"items", std::move(items)}}.dump();
}

std::string ActivityService::ProgressJson(int64_t uid, int activity_id) {
  auto def = GetDef(activity_id);
  if (!def) return json{{"error", "not found"}}.dump();
  auto prog = LoadProgress(uid, activity_id);
  return json{{"activity_id", activity_id},
              {"type", def->type},
              {"progress_json", ParseJsonOr(prog.empty() ? "{}" : prog, json::object())}}
      .dump();
}

ClaimResult ActivityService::Claim(int64_t uid, int activity_id, const std::string& reward_key_in) {
  ClaimResult r;
  try {
    auto def = GetDef(activity_id);
    if (!def || !def->enabled) {
      r.error = "activity unavailable";
      return r;
    }
    std::string rk = reward_key_in.empty() ? ExtractStr(def->rules_json, "reward_key", "reward") : reward_key_in;
    std::string claim_key = rk;
    auto prog = LoadProgress(uid, activity_id);

    if (def->type == "sign") {
      const std::string today = TodayKey();
      claim_key = rk + ":" + today;
      if (HasClaimed(uid, activity_id, claim_key) || ExtractStr(prog, "last_sign_day", "") == today) {
        r.error = "already claimed";
        return r;
      }
    } else if (def->type == "task") {
      if (HasClaimed(uid, activity_id, claim_key)) {
        r.error = "already claimed";
        return r;
      }
      const int games = ExtractInt(prog, "games", 0);
      const int target = ExtractInt(def->rules_json, "target_games", 3);
      if (games < target) {
        r.error = "progress not enough";
        return r;
      }
    } else if (def->type == "gift") {
      if (HasClaimed(uid, activity_id, claim_key)) {
        r.error = "already claimed";
        return r;
      }
      int remain = 0;
      if (!DecrStock(activity_id, remain)) {
        r.error = "out of stock";
        return r;
      }
    } else {
      r.error = "bad type";
      return r;
    }

    int currency = 1;
    int64_t amount = 0;
    json items = json::array();
    try {
      const auto rules = json::parse(def->rules_json.empty() ? "{}" : def->rules_json, nullptr, false);
      if (rules.is_object() && rules.contains("reward") && rules["reward"].is_object()) {
        const auto& reward = rules["reward"];
        currency = reward.value("currency", 1);
        amount = reward.value("amount", static_cast<int64_t>(0));
        if (reward.contains("items") && reward["items"].is_array()) items = reward["items"];
      }
    } catch (...) {
    }
    if (amount <= 0 && items.empty()) {
      r.error = "bad reward";
      return r;
    }

    const std::string idem = "act:" + std::to_string(activity_id) + ":" + std::to_string(uid) + ":" + claim_key;
    if (amount > 0) {
      auto adj = wallet_.Adjust(uid, currency == 2 ? Currency::kDiamond : Currency::kGold, amount, "activity_reward",
                                idem, claim_key);
      if (!adj.ok) {
        r.error = adj.error.empty() ? "adjust failed" : adj.error;
        return r;
      }
      r.balance = adj.balance;
      r.currency = currency;
    } else {
      auto p = wallet_.Profile(uid);
      r.balance = p ? (currency == 2 ? p->diamond : p->gold) : 0;
      r.currency = currency;
    }
    if (!items.empty()) {
      std::string ierr;
      if (!bag_.GrantItemsFromJson(uid, items.dump(), idem, "activity_reward", claim_key, &ierr)) {
        r.error = ierr.empty() ? "bag grant failed" : ierr;
        return r;
      }
    }
    MarkClaimed(uid, activity_id, claim_key);
    if (def->type == "sign") {
      SaveProgress(uid, activity_id, json{{"last_sign_day", TodayKey()}, {"signed_today", true}}.dump());
    }
    r.ok = true;
    PushUpdate(uid, *def, LoadProgress(uid, activity_id), false);
    return r;
  } catch (const std::exception& e) {
    r.error = e.what();
    return r;
  } catch (...) {
    r.error = "internal";
    return r;
  }
}

}  // namespace pandora
