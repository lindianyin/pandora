#include "social/social_service.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <sstream>

#include <nlohmann/json.hpp>

#include "common/errors.hpp"
#include "common/log.hpp"
#include "common/proto_wire.hpp"
#include "store/memory_store.hpp"

namespace pandora {
namespace {

using json = nlohmann::json;

int64_t ParseI64(const std::string& s, int64_t def = 0) {
  try {
    return std::stoll(s);
  } catch (...) {
    return def;
  }
}

int ParseInt(const std::string& s, int def = 0) {
  try {
    return std::stoi(s);
  } catch (...) {
    return def;
  }
}

}  // namespace

SocialService::SocialService(MysqlClient& mysql, RedisClient& redis, WalletService& wallet, BagService& bag,
                             SessionHub& hub, AsyncWorker& persist, SocialConfig cfg)
    : mysql_(mysql), redis_(redis), wallet_(wallet), bag_(bag), hub_(hub), persist_(persist), cfg_(std::move(cfg)) {}

void SocialService::Bootstrap() {
  PLOG_INFO("social bootstrap friend_max=" << cfg_.friend_max << " rank_mode=" << cfg_.rank_score_mode);
}

std::string SocialService::DailyKey() const {
  const auto now = std::chrono::system_clock::now();
  const std::time_t t = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
#ifdef _WIN32
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%04d%02d%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
  return buf;
}

std::string SocialService::WeeklyKey() const {
  // ISO week: Monday-based; period_key like 2026W39
  const auto now = std::chrono::system_clock::now();
  const std::time_t t = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
#ifdef _WIN32
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  // ISO-8601 week number
  char buf[16];
#if defined(_MSC_VER)
  // %G %V not always available on MSVC; approximate via day-of-year
  const int yday = tm.tm_yday + 1;
  const int wday = tm.tm_wday == 0 ? 7 : tm.tm_wday;  // Mon=1..Sun=7
  int week = (yday - wday + 10) / 7;
  int year = tm.tm_year + 1900;
  if (week < 1) {
    year -= 1;
    week = 52;
  } else if (week > 52) {
    week = 1;
    year += 1;
  }
  std::snprintf(buf, sizeof(buf), "%04dW%02d", year, week);
#else
  std::strftime(buf, sizeof(buf), "%GW%V", &tm);
#endif
  return buf;
}

std::string SocialService::PeriodKeyOf(const std::string& period) const {
  if (period == "weekly") return WeeklyKey();
  return DailyKey();
}

std::string SocialService::RankRedisKey(const std::string& period) const {
  if (period == "weekly") return "rank:gold:weekly:" + WeeklyKey();
  return "rank:gold:daily:" + DailyKey();
}

std::string SocialService::NicknameOf(int64_t uid) const {
  auto p = wallet_.Profile(uid);
  if (p) return p->nickname.empty() ? ("Player" + std::to_string(uid)) : p->nickname;
  if (mysql_.Available()) {
    auto rows = mysql_.QueryBind("SELECT nickname FROM player_profile WHERE uid=? LIMIT 1", {I64(uid)});
    if (rows && !rows->empty() && !rows->front().cols.empty() && !rows->front().cols[0].empty()) {
      return rows->front().cols[0];
    }
  }
  return "Player" + std::to_string(uid);
}

void SocialService::ApplyStatsForPlayer(int64_t uid, int64_t delta, bool is_landlord) {
  const int win = delta > 0 ? 1 : 0;
  const int lose = delta < 0 ? 1 : 0;
  const int landlord = is_landlord ? 1 : 0;
  const int64_t win_sum = delta > 0 ? delta : 0;
  const int64_t lose_sum = delta < 0 ? -delta : 0;
  mysql_.ExecBind(
      "INSERT INTO player_stats(uid,total_rounds,win_rounds,lose_rounds,landlord_rounds,gold_win_sum,gold_lose_sum,"
      "updated_at) VALUES(?,?,?,?,?,?,?,NOW(3)) "
      "ON DUPLICATE KEY UPDATE total_rounds=total_rounds+1, win_rounds=win_rounds+VALUES(win_rounds), "
      "lose_rounds=lose_rounds+VALUES(lose_rounds), landlord_rounds=landlord_rounds+VALUES(landlord_rounds), "
      "gold_win_sum=gold_win_sum+VALUES(gold_win_sum), gold_lose_sum=gold_lose_sum+VALUES(gold_lose_sum), "
      "updated_at=NOW(3)",
      {I64(uid), I64(1), I64(win), I64(lose), I64(landlord), I64(win_sum), I64(lose_sum)});
}

void SocialService::OnRoundSettled(int64_t round_id, int /*template_id*/, const std::string& players_json,
                                   int /*base_score*/, int /*multiplier*/) {
  auto job = [this, round_id, players_json]() {
    if (!mysql_.Available()) {
      PLOG_WARN("OnRoundSettled skipped: mysql unavailable round=" << round_id);
      return;
    }
    // Idempotent: one stats update per round_id
    if (redis_.Available()) {
      if (!redis_.SetNx("soc:stats:" + std::to_string(round_id), "1", 86400 * 7)) {
        PLOG_INFO("OnRoundSettled skip duplicate round=" << round_id);
        return;
      }
    }
    try {
      const auto arr = json::parse(players_json, nullptr, false);
      if (!arr.is_array()) return;
      for (const auto& p : arr) {
        const int64_t uid = p.value("uid", static_cast<int64_t>(0));
        if (uid <= 0) continue;
        const int64_t delta = p.value("delta", static_cast<int64_t>(0));
        const bool is_landlord = p.value("is_landlord", false);
        ApplyStatsForPlayer(uid, delta, is_landlord);
      }
    } catch (const std::exception& e) {
      PLOG_WARN("OnRoundSettled parse err: " << e.what());
    }
  };
  if (!persist_.Post(job)) job();
}

std::string SocialService::SummaryJson(int64_t uid) const {
  json data = {{"total_rounds", 0},
               {"win_rounds", 0},
               {"lose_rounds", 0},
               {"win_rate_bp", 0},
               {"landlord_rounds", 0},
               {"gold_win_sum", 0},
               {"gold_lose_sum", 0}};
  if (!mysql_.Available()) return data.dump();
  auto rows = mysql_.QueryBind(
      "SELECT total_rounds,win_rounds,lose_rounds,landlord_rounds,gold_win_sum,gold_lose_sum "
      "FROM player_stats WHERE uid=? LIMIT 1",
      {I64(uid)});
  if (rows && !rows->empty() && rows->front().cols.size() >= 6) {
    const auto& c = rows->front().cols;
    const int total = ParseInt(c[0]);
    const int win = ParseInt(c[1]);
    data["total_rounds"] = total;
    data["win_rounds"] = win;
    data["lose_rounds"] = ParseInt(c[2]);
    data["landlord_rounds"] = ParseInt(c[3]);
    data["gold_win_sum"] = ParseI64(c[4]);
    data["gold_lose_sum"] = ParseI64(c[5]);
    data["win_rate_bp"] = total > 0 ? (win * 10000 / total) : 0;
  }
  return data.dump();
}

std::string SocialService::RecentJson(int64_t uid, int page, int page_size) const {
  if (page < 1) page = 1;
  if (page_size < 1) page_size = 20;
  if (page_size > 100) page_size = 100;
  json items = json::array();
  int total = 0;
  if (!mysql_.Available()) return json{{"items", items}, {"total", 0}, {"page", page}, {"page_size", page_size}}.dump();

  const std::string like1 = "%\"uid\":" + std::to_string(uid) + "%";
  const std::string like2 = "%\"uid\": " + std::to_string(uid) + "%";
  auto cnt = mysql_.QueryBind("SELECT COUNT(*) FROM game_round WHERE players_json LIKE ? OR players_json LIKE ?",
                              {Str(like1), Str(like2)});
  if (cnt && !cnt->empty() && !cnt->front().cols.empty()) total = ParseInt(cnt->front().cols[0]);

  const int64_t offset = static_cast<int64_t>(page - 1) * page_size;
  auto rows = mysql_.QueryBind(
      "SELECT round_id,template_id,players_json,base_score,multiplier,ended_at FROM game_round "
      "WHERE players_json LIKE ? OR players_json LIKE ? ORDER BY ended_at DESC LIMIT ? OFFSET ?",
      {Str(like1), Str(like2), I64(page_size), I64(offset)});
  if (rows) {
    for (const auto& row : *rows) {
      if (row.cols.size() < 6) continue;
      try {
        const auto arr = json::parse(row.cols[2], nullptr, false);
        if (!arr.is_array()) continue;
        for (const auto& p : arr) {
          if (p.value("uid", static_cast<int64_t>(0)) != uid) continue;
          const int64_t delta = p.value("delta", static_cast<int64_t>(0));
          std::string result = "draw";
          if (delta > 0) result = "win";
          else if (delta < 0) result = "lose";
          items.push_back({{"round_id", ParseI64(row.cols[0])},
                           {"template_id", ParseInt(row.cols[1])},
                           {"ended_at", row.cols[5]},
                           {"base_score", ParseInt(row.cols[3])},
                           {"multiplier", ParseInt(row.cols[4])},
                           {"delta_gold", delta},
                           {"is_landlord", p.value("is_landlord", false)},
                           {"result", result}});
          break;
        }
      } catch (...) {
      }
    }
  }
  return json{{"items", items}, {"total", total}, {"page", page}, {"page_size", page_size}}.dump();
}

int SocialService::FriendCount(int64_t uid) const {
  if (!mysql_.Available()) return 0;
  auto rows = mysql_.QueryBind("SELECT COUNT(*) FROM friendship WHERE uid_low=? OR uid_high=?", {I64(uid), I64(uid)});
  if (!rows || rows->empty() || rows->front().cols.empty()) return 0;
  return ParseInt(rows->front().cols[0]);
}

bool SocialService::AreFriends(int64_t a, int64_t b) const {
  if (!mysql_.Available() || a == b) return false;
  const int64_t lo = (std::min)(a, b);
  const int64_t hi = (std::max)(a, b);
  auto rows = mysql_.QueryBind("SELECT uid_low FROM friendship WHERE uid_low=? AND uid_high=? LIMIT 1",
                               {I64(lo), I64(hi)});
  return rows && !rows->empty();
}

bool SocialService::HasPending(int64_t from, int64_t to) const {
  if (!mysql_.Available()) return false;
  auto rows = mysql_.QueryBind(
      "SELECT id FROM friend_request WHERE from_uid=? AND to_uid=? AND status=0 LIMIT 1", {I64(from), I64(to)});
  return rows && !rows->empty();
}

std::string SocialService::FriendListJson(int64_t uid) const {
  json items = json::array();
  if (!mysql_.Available()) return json{{"items", items}}.dump();
  auto rows = mysql_.QueryBind(
      "SELECT CASE WHEN uid_low=? THEN uid_high ELSE uid_low END AS fuid FROM friendship "
      "WHERE uid_low=? OR uid_high=?",
      {I64(uid), I64(uid), I64(uid)});
  if (rows) {
    for (const auto& row : *rows) {
      if (row.cols.empty()) continue;
      const int64_t fuid = ParseI64(row.cols[0]);
      items.push_back({{"uid", fuid}, {"nickname", NicknameOf(fuid)}, {"online", hub_.IsOnline(fuid)}});
    }
  }
  return json{{"items", items}}.dump();
}

std::string SocialService::FriendPendingJson(int64_t uid) const {
  json incoming = json::array();
  json outgoing = json::array();
  if (!mysql_.Available()) return json{{"incoming", incoming}, {"outgoing", outgoing}}.dump();
  auto in_rows = mysql_.QueryBind(
      "SELECT id,from_uid,created_at FROM friend_request WHERE to_uid=? AND status=0 ORDER BY id DESC LIMIT 100",
      {I64(uid)});
  if (in_rows) {
    for (const auto& row : *in_rows) {
      if (row.cols.size() < 3) continue;
      const int64_t from = ParseI64(row.cols[1]);
      incoming.push_back({{"id", ParseI64(row.cols[0])},
                          {"from_uid", from},
                          {"nickname", NicknameOf(from)},
                          {"created_at", row.cols[2]}});
    }
  }
  auto out_rows = mysql_.QueryBind(
      "SELECT id,to_uid,created_at FROM friend_request WHERE from_uid=? AND status=0 ORDER BY id DESC LIMIT 100",
      {I64(uid)});
  if (out_rows) {
    for (const auto& row : *out_rows) {
      if (row.cols.size() < 3) continue;
      const int64_t to = ParseI64(row.cols[1]);
      outgoing.push_back({{"id", ParseI64(row.cols[0])},
                          {"to_uid", to},
                          {"nickname", NicknameOf(to)},
                          {"created_at", row.cols[2]}});
    }
  }
  return json{{"incoming", incoming}, {"outgoing", outgoing}}.dump();
}

bool SocialService::FriendRequest(int64_t from_uid, int64_t to_uid, std::string* err) {
  if (from_uid <= 0 || to_uid <= 0 || from_uid == to_uid) {
    if (err) *err = "invalid uid";
    return false;
  }
  if (!mysql_.Available()) {
    if (err) *err = "mysql unavailable";
    return false;
  }
  if (AreFriends(from_uid, to_uid) || HasPending(from_uid, to_uid) || HasPending(to_uid, from_uid)) {
    if (err) *err = "already friends or pending";
    return false;
  }
  if (FriendCount(from_uid) >= cfg_.friend_max || FriendCount(to_uid) >= cfg_.friend_max) {
    if (err) *err = "friend limit reached";
    return false;
  }
  // Reuse unique (from,to): upsert pending
  const int r = mysql_.ExecBind(
      "INSERT INTO friend_request(from_uid,to_uid,status,created_at,updated_at) VALUES(?,?,0,NOW(3),NOW(3)) "
      "ON DUPLICATE KEY UPDATE status=0, updated_at=NOW(3)",
      {I64(from_uid), I64(to_uid)});
  if (r < 0) {
    if (err) *err = mysql_.LastError();
    return false;
  }
  PushFriendNotify(to_uid, 1, from_uid, NicknameOf(from_uid));
  return true;
}

bool SocialService::FriendAccept(int64_t uid, int64_t from_uid, std::string* err) {
  if (!mysql_.Available()) {
    if (err) *err = "mysql unavailable";
    return false;
  }
  if (!HasPending(from_uid, uid)) {
    if (err) *err = "no pending request";
    return false;
  }
  if (FriendCount(uid) >= cfg_.friend_max || FriendCount(from_uid) >= cfg_.friend_max) {
    if (err) *err = "friend limit reached";
    return false;
  }
  const int64_t lo = (std::min)(uid, from_uid);
  const int64_t hi = (std::max)(uid, from_uid);
  mysql_.ExecBind("UPDATE friend_request SET status=1, updated_at=NOW(3) WHERE from_uid=? AND to_uid=? AND status=0",
                  {I64(from_uid), I64(uid)});
  const int r =
      mysql_.ExecBind("INSERT IGNORE INTO friendship(uid_low,uid_high,created_at) VALUES(?,?,NOW(3))", {I64(lo), I64(hi)});
  if (r < 0) {
    if (err) *err = mysql_.LastError();
    return false;
  }
  PushFriendNotify(from_uid, 2, uid, NicknameOf(uid));
  return true;
}

bool SocialService::FriendReject(int64_t uid, int64_t from_uid, std::string* err) {
  if (!mysql_.Available()) {
    if (err) *err = "mysql unavailable";
    return false;
  }
  const int r = mysql_.ExecBind(
      "UPDATE friend_request SET status=2, updated_at=NOW(3) WHERE from_uid=? AND to_uid=? AND status=0",
      {I64(from_uid), I64(uid)});
  if (r <= 0) {
    if (err) *err = "no pending request";
    return false;
  }
  return true;
}

bool SocialService::FriendRemove(int64_t uid, int64_t friend_uid, std::string* err) {
  if (!mysql_.Available()) {
    if (err) *err = "mysql unavailable";
    return false;
  }
  const int64_t lo = (std::min)(uid, friend_uid);
  const int64_t hi = (std::max)(uid, friend_uid);
  const int r = mysql_.ExecBind("DELETE FROM friendship WHERE uid_low=? AND uid_high=?", {I64(lo), I64(hi)});
  if (r <= 0) {
    if (err) *err = "not friends";
    return false;
  }
  return true;
}

void SocialService::PushFriendNotify(int64_t uid, int kind, int64_t from_uid, const std::string& nickname) {
  if (!hub_.IsOnline(uid)) return;
  const auto body = proto_wire::EncodeS2C_FriendNotify(kind, from_uid, nickname);
  hub_.Send(uid, MsgId::kS2C_FriendNotify, body);
}

void SocialService::PushMailNotify(int64_t uid, int64_t mail_id, const std::string& title, bool has_attach) {
  if (!hub_.IsOnline(uid)) return;
  const auto body = proto_wire::EncodeS2C_MailNotify(mail_id, title, has_attach);
  hub_.Send(uid, MsgId::kS2C_MailNotify, body);
}

void SocialService::InsertMailForUid(int64_t uid, const std::string& title, const std::string& body,
                                     const std::string& attach_json) {
  const int days = cfg_.mail_expire_days > 0 ? cfg_.mail_expire_days : 30;
  const std::string attach = attach_json.empty() ? "{}" : attach_json;
  mysql_.ExecBind(
      "INSERT INTO mail(to_uid,title,body,attach_json,status,expire_at,created_at) "
      "VALUES(?,?,?,?,0,DATE_ADD(NOW(3), INTERVAL ? DAY),NOW(3))",
      {I64(uid), Str(title), Str(body), Str(attach), I64(days)});
  int64_t mail_id = 0;
  auto rows = mysql_.QueryBind("SELECT id FROM mail WHERE to_uid=? ORDER BY id DESC LIMIT 1", {I64(uid)});
  if (rows && !rows->empty()) mail_id = ParseI64(rows->front().cols[0]);
  bool has_attach = false;
  try {
    const auto j = json::parse(attach, nullptr, false);
    has_attach = j.is_object() && j.value("amount", static_cast<int64_t>(0)) > 0;
  } catch (...) {
  }
  if (mail_id > 0) PushMailNotify(uid, mail_id, title, has_attach);
}

std::string SocialService::MailListJson(int64_t uid) const {
  json items = json::array();
  if (!mysql_.Available()) return json{{"items", items}}.dump();
  auto rows = mysql_.QueryBind(
      "SELECT id,title,body,attach_json,status,expire_at,created_at FROM mail "
      "WHERE to_uid=? AND status<>3 AND expire_at>NOW(3) ORDER BY id DESC LIMIT 100",
      {I64(uid)});
  if (rows) {
    for (const auto& row : *rows) {
      if (row.cols.size() < 7) continue;
      json attach = json::object();
      try {
        attach = json::parse(row.cols[3], nullptr, false);
        if (!attach.is_object()) attach = json::object();
      } catch (...) {
      }
      items.push_back({{"id", ParseI64(row.cols[0])},
                       {"title", row.cols[1]},
                       {"body", row.cols[2]},
                       {"attach_json", attach},
                       {"status", ParseInt(row.cols[4])},
                       {"expire_at", row.cols[5]},
                       {"created_at", row.cols[6]}});
    }
  }
  return json{{"items", items}}.dump();
}

bool SocialService::MailRead(int64_t uid, int64_t mail_id, std::string* err) {
  if (!mysql_.Available()) {
    if (err) *err = "mysql unavailable";
    return false;
  }
  const int r = mysql_.ExecBind(
      "UPDATE mail SET status=1 WHERE id=? AND to_uid=? AND status=0 AND expire_at>NOW(3)", {I64(mail_id), I64(uid)});
  if (r < 0) {
    if (err) *err = mysql_.LastError();
    return false;
  }
  // already read / claimed is ok
  return true;
}

SocialOpResult SocialService::MailClaim(int64_t uid, int64_t mail_id) {
  SocialOpResult r;
  if (!mysql_.Available()) {
    r.error = "mysql unavailable";
    return r;
  }
  auto rows = mysql_.QueryBind(
      "SELECT attach_json,status,expire_at FROM mail WHERE id=? AND to_uid=? AND status<>3 LIMIT 1",
      {I64(mail_id), I64(uid)});
  if (!rows || rows->empty() || rows->front().cols.size() < 3) {
    r.error = "mail not found";
    return r;
  }
  const int status = ParseInt(rows->front().cols[1]);
  if (status == 2) {
    r.error = "already claimed";
    return r;
  }
  // expire check via query filter would need comparing; re-check
  auto alive = mysql_.QueryBind(
      "SELECT id FROM mail WHERE id=? AND to_uid=? AND expire_at>NOW(3) LIMIT 1", {I64(mail_id), I64(uid)});
  if (!alive || alive->empty()) {
    r.error = "mail expired";
    return r;
  }

  int currency = 1;
  int64_t amount = 0;
  json items = json::array();
  try {
    const auto attach = json::parse(rows->front().cols[0], nullptr, false);
    if (attach.is_object()) {
      currency = attach.value("currency", 1);
      amount = attach.value("amount", static_cast<int64_t>(0));
      if (attach.contains("items") && attach["items"].is_array()) items = attach["items"];
    }
  } catch (...) {
  }
  const std::string idem = "mail:" + std::to_string(mail_id) + ":" + std::to_string(uid);
  if (amount <= 0 && items.empty()) {
    mysql_.ExecBind("UPDATE mail SET status=2 WHERE id=? AND to_uid=?", {I64(mail_id), I64(uid)});
    r.ok = true;
    auto p = wallet_.Profile(uid);
    r.balance = p ? p->gold : 0;
    r.currency = currency;
    return r;
  }

  if (amount > 0) {
    auto adj = wallet_.Adjust(uid, currency == 2 ? Currency::kDiamond : Currency::kGold, amount, "mail_reward", idem,
                              std::to_string(mail_id));
    if (!adj.ok) {
      r.error = adj.error.empty() ? "wallet adjust failed" : adj.error;
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
    if (!bag_.GrantItemsFromJson(uid, items.dump(), idem, "mail_reward", std::to_string(mail_id), &ierr)) {
      r.error = ierr.empty() ? "bag grant failed" : ierr;
      return r;
    }
  }
  mysql_.ExecBind("UPDATE mail SET status=2 WHERE id=? AND to_uid=?", {I64(mail_id), I64(uid)});
  r.ok = true;
  return r;
}

bool SocialService::MailDelete(int64_t uid, int64_t mail_id, std::string* err) {
  if (!mysql_.Available()) {
    if (err) *err = "mysql unavailable";
    return false;
  }
  const int r = mysql_.ExecBind("UPDATE mail SET status=3 WHERE id=? AND to_uid=? AND status<>3", {I64(mail_id), I64(uid)});
  if (r <= 0) {
    if (err) *err = "mail not found";
    return false;
  }
  return true;
}

bool SocialService::AdminSendMail(int admin_id, const std::string& scope, const std::vector<int64_t>& uids,
                                  const std::string& title, const std::string& body, const std::string& attach_json,
                                  std::string* err) {
  if (!mysql_.Available()) {
    if (err) *err = "mysql unavailable";
    return false;
  }
  if (title.empty()) {
    if (err) *err = "title required";
    return false;
  }
  const std::string attach = attach_json.empty() ? "{}" : attach_json;
  json target = json::object();
  std::vector<int64_t> targets = uids;
  if (scope == "all") {
    target = json::object();
    auto rows = mysql_.QueryBind("SELECT uid FROM `user` ORDER BY uid ASC LIMIT 100000", {});
    targets.clear();
    if (rows) {
      for (const auto& row : *rows) {
        if (!row.cols.empty()) targets.push_back(ParseI64(row.cols[0]));
      }
    }
  } else {
    target = json{{"uids", uids}};
    if (targets.empty()) {
      if (err) *err = "uids required";
      return false;
    }
  }

  mysql_.ExecBind(
      "INSERT INTO mail_send_log(admin_id,scope,target_json,title,body,attach_json,created_at) "
      "VALUES(?,?,?,?,?,?,NOW(3))",
      {I64(admin_id), Str(scope.empty() ? "uids" : scope), Str(target.dump()), Str(title), Str(body), Str(attach)});

  const int batch = cfg_.mail_broadcast_batch > 0 ? cfg_.mail_broadcast_batch : 500;
  int n = 0;
  for (int64_t uid : targets) {
    if (uid <= 0) continue;
    InsertMailForUid(uid, title, body, attach);
    if (++n >= batch && scope == "all") {
      // still send all, just keep going; batch is advisory for future async
      (void)batch;
    }
  }
  return true;
}

std::string SocialService::AdminMailLogJson(int page, int page_size) const {
  if (page < 1) page = 1;
  if (page_size < 1) page_size = 20;
  if (page_size > 100) page_size = 100;
  json items = json::array();
  if (!mysql_.Available()) return json{{"items", items}, {"total", 0}}.dump();
  int total = 0;
  auto cnt = mysql_.QueryBind("SELECT COUNT(*) FROM mail_send_log", {});
  if (cnt && !cnt->empty()) total = ParseInt(cnt->front().cols[0]);
  const int64_t offset = static_cast<int64_t>(page - 1) * page_size;
  auto rows = mysql_.QueryBind(
      "SELECT id,admin_id,scope,target_json,title,body,attach_json,"
      "DATE_FORMAT(created_at,'%Y-%m-%d %H:%i:%s') FROM mail_send_log "
      "ORDER BY id DESC LIMIT ? OFFSET ?",
      {I64(page_size), I64(offset)});
  if (rows) {
    for (const auto& row : *rows) {
      if (row.cols.size() < 8) continue;
      json target = json::object();
      json attach = json::object();
      try {
        target = json::parse(row.cols[3], nullptr, false);
      } catch (...) {
      }
      try {
        attach = json::parse(row.cols[6], nullptr, false);
      } catch (...) {
      }
      items.push_back({{"id", ParseI64(row.cols[0])},
                       {"admin_id", ParseInt(row.cols[1])},
                       {"scope", row.cols[2]},
                       {"target_json", target},
                       {"title", row.cols[4]},
                       {"body", row.cols[5]},
                       {"attach_json", attach},
                       {"created_at", row.cols[7]}});
    }
  }
  return json{{"items", items}, {"total", total}, {"page", page}, {"page_size", page_size}}.dump();
}

void SocialService::OnGoldChanged(int64_t uid, int64_t gold) {
  if (uid <= 0) return;
  auto job = [this, uid, gold]() {
    if (!redis_.Available()) return;
    const std::string member = std::to_string(uid);
    const double score = static_cast<double>(gold);
    const std::string daily = RankRedisKey("daily");
    const std::string weekly = RankRedisKey("weekly");
    if (!redis_.ZAdd(daily, member, score)) PLOG_WARN("rank zadd daily fail: " << redis_.LastError());
    else redis_.Expire(daily, 48 * 3600);
    if (!redis_.ZAdd(weekly, member, score)) PLOG_WARN("rank zadd weekly fail: " << redis_.LastError());
    else redis_.Expire(weekly, 14 * 24 * 3600);
  };
  if (!persist_.Post(job)) job();
}

std::string SocialService::RankJson(int64_t uid, const std::string& period, int limit) const {
  if (period != "daily" && period != "weekly") {
    return json{{"error", "bad period"}}.dump();
  }
  if (limit < 1) limit = 50;
  if (limit > cfg_.rank_top_n) limit = cfg_.rank_top_n;
  if (limit > 100) limit = 100;

  json list = json::array();
  json me = {{"rank", 0}, {"score", 0}};
  const std::string period_key = PeriodKeyOf(period);

  if (redis_.Available()) {
    const std::string key = RankRedisKey(period);
    std::vector<std::pair<std::string, double>> rows;
    if (redis_.ZRevRangeWithScores(key, 0, limit - 1, &rows)) {
      int rank = 1;
      for (const auto& [member, score] : rows) {
        const int64_t ruid = ParseI64(member);
        list.push_back({{"rank", rank}, {"uid", ruid}, {"nickname", NicknameOf(ruid)}, {"score", static_cast<int64_t>(score)}});
        ++rank;
      }
    }
    auto zr = redis_.ZRevRank(key, std::to_string(uid));
    auto zs = redis_.ZScore(key, std::to_string(uid));
    if (zr) me["rank"] = static_cast<int>(*zr) + 1;
    if (zs) me["score"] = static_cast<int64_t>(*zs);
  } else if (mysql_.Available()) {
    // Fallback: order by gold
    auto rows = mysql_.QueryBind(
        "SELECT uid,gold,nickname FROM player_profile ORDER BY gold DESC, uid ASC LIMIT ?", {I64(limit)});
    int rank = 1;
    if (rows) {
      for (const auto& row : *rows) {
        if (row.cols.size() < 3) continue;
        const int64_t ruid = ParseI64(row.cols[0]);
        const int64_t score = ParseI64(row.cols[1]);
        list.push_back({{"rank", rank},
                        {"uid", ruid},
                        {"nickname", row.cols[2].empty() ? ("Player" + row.cols[0]) : row.cols[2]},
                        {"score", score}});
        if (ruid == uid) {
          me["rank"] = rank;
          me["score"] = score;
        }
        ++rank;
      }
    }
    if (me["rank"] == 0) {
      auto p = wallet_.Profile(uid);
      if (p) me["score"] = p->gold;
    }
  }

  return json{{"period", period}, {"period_key", period_key}, {"list", list}, {"me", me}}.dump();
}

bool SocialService::SnapshotRank(const std::string& period, std::string* err) {
  if (period != "daily" && period != "weekly") {
    if (err) *err = "bad period";
    return false;
  }
  if (!mysql_.Available()) {
    if (err) *err = "mysql unavailable";
    return false;
  }
  const std::string period_key = PeriodKeyOf(period);
  const int top_n = cfg_.rank_top_n > 0 ? cfg_.rank_top_n : 100;
  std::vector<std::pair<int64_t, int64_t>> entries;  // uid, score

  if (redis_.Available()) {
    std::vector<std::pair<std::string, double>> rows;
    if (redis_.ZRevRangeWithScores(RankRedisKey(period), 0, top_n - 1, &rows)) {
      for (const auto& [member, score] : rows) {
        entries.emplace_back(ParseI64(member), static_cast<int64_t>(score));
      }
    }
  }
  if (entries.empty()) {
    auto rows = mysql_.QueryBind("SELECT uid,gold FROM player_profile ORDER BY gold DESC, uid ASC LIMIT ?",
                                 {I64(top_n)});
    if (rows) {
      for (const auto& row : *rows) {
        if (row.cols.size() < 2) continue;
        entries.emplace_back(ParseI64(row.cols[0]), ParseI64(row.cols[1]));
      }
    }
  }

  int rank_no = 1;
  for (const auto& [uid, score] : entries) {
    mysql_.ExecBind(
        "INSERT INTO rank_snapshot(period,period_key,uid,score,rank_no,created_at) VALUES(?,?,?,?,?,NOW(3)) "
        "ON DUPLICATE KEY UPDATE score=VALUES(score), rank_no=VALUES(rank_no), created_at=NOW(3)",
        {Str(period), Str(period_key), I64(uid), I64(score), I64(rank_no)});
    ++rank_no;
  }
  return true;
}

std::string SocialService::AdminRankSnapshotJson(const std::string& period, int page, int page_size) const {
  if (page < 1) page = 1;
  if (page_size < 1) page_size = 50;
  if (page_size > 100) page_size = 100;
  json items = json::array();
  std::string use_period = period.empty() ? "daily" : period;
  if (!mysql_.Available()) return json{{"items", items}, {"period", use_period}}.dump();

  std::string period_key;
  auto keys = mysql_.QueryBind(
      "SELECT period_key FROM rank_snapshot WHERE period=? ORDER BY created_at DESC LIMIT 1", {Str(use_period)});
  if (keys && !keys->empty()) period_key = keys->front().cols[0];
  else period_key = PeriodKeyOf(use_period);

  const int64_t offset = static_cast<int64_t>(page - 1) * page_size;
  auto rows = mysql_.QueryBind(
      "SELECT uid,score,rank_no,created_at FROM rank_snapshot WHERE period=? AND period_key=? "
      "ORDER BY rank_no ASC LIMIT ? OFFSET ?",
      {Str(use_period), Str(period_key), I64(page_size), I64(offset)});
  if (rows) {
    for (const auto& row : *rows) {
      if (row.cols.size() < 4) continue;
      const int64_t ruid = ParseI64(row.cols[0]);
      items.push_back({{"uid", ruid},
                       {"nickname", NicknameOf(ruid)},
                       {"score", ParseI64(row.cols[1])},
                       {"rank_no", ParseInt(row.cols[2])},
                       {"created_at", row.cols[3]}});
    }
  }
  return json{{"period", use_period}, {"period_key", period_key}, {"items", items}, {"page", page}, {"page_size", page_size}}
      .dump();
}

}  // namespace pandora
