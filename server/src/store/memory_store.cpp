#include "store/memory_store.hpp"

#include <random>
#include <sstream>

#include <nlohmann/json.hpp>

#include "common/log.hpp"

namespace pandora {

namespace {

std::string MakeToken() {
  static thread_local std::mt19937_64 rng{std::random_device{}()};
  std::uniform_int_distribution<uint64_t> dist;
  std::ostringstream oss;
  oss << std::hex << dist(rng) << dist(rng);
  return oss.str();
}

}  // namespace

MemoryStore::MemoryStore(MysqlClient& mysql, RedisClient& redis, AsyncWorker& persist)
    : mysql_(mysql), redis_(redis), persist_(persist) {}

std::string MemoryStore::EscapeSql(const std::string& s) const {
  std::string o;
  for (char c : s) {
    if (c == '\'' || c == '\\') o.push_back('\\');
    o.push_back(c);
  }
  return o;
}

void MemoryStore::WriteRedisProfile(const PlayerRecord& p) {
  if (!redis_.Available()) return;
  const nlohmann::json j = {{"uid", p.uid},
                            {"open_id", p.open_id},
                            {"nickname", p.nickname},
                            {"gold", p.gold},
                            {"diamond", p.diamond},
                            {"status", p.status}};
  redis_.Set("player:" + std::to_string(p.uid), j.dump(), 3600);
  if (!p.open_id.empty()) redis_.Set("user:open:1:" + p.open_id, std::to_string(p.uid), 3600);
}

void MemoryStore::CachePlayer(const PlayerRecord& p) {
  {
    auto& sh = player_shards_[UidShard(p.uid)];
    std::lock_guard<std::mutex> lk(sh.mu);
    sh.players[p.uid] = p;
  }
  if (!p.open_id.empty()) {
    std::lock_guard<std::mutex> lk(open_mu_);
    open_id_index_[p.open_id] = p.uid;
  }
}

void MemoryStore::EnqueuePersistAdjust(const PlayerRecord& snap, const LedgerEntry& e) {
  persist_.Post([this, snap, e]() { PersistAdjust(snap, e); });
}

void MemoryStore::PersistAdjust(PlayerRecord snap, LedgerEntry e) {
  WriteRedisProfile(snap);
  if (!mysql_.Available()) return;
  if (e.currency == static_cast<int>(Currency::kDiamond)) {
    mysql_.ExecBind("UPDATE player_profile SET diamond=? WHERE uid=?", {I64(e.balance_after), I64(e.uid)});
  } else {
    mysql_.ExecBind("UPDATE player_profile SET gold=? WHERE uid=?", {I64(e.balance_after), I64(e.uid)});
  }
  if (!e.idempotent_key.empty()) {
    mysql_.ExecBind(
        "INSERT IGNORE INTO ledger(uid,currency,delta,balance_after,biz_type,idempotent_key,ref_id) "
        "VALUES(?,?,?,?,?,?,?)",
        {I64(e.uid), I64(e.currency), I64(e.delta), I64(e.balance_after), Str(e.biz_type), Str(e.idempotent_key),
         Str(e.ref_id)});
  }
}

std::optional<PlayerRecord> MemoryStore::LoadFromMysqlByOpenId(const std::string& open_id) {
  if (!mysql_.Available() || open_id.empty()) return std::nullopt;
  auto rows = mysql_.QueryBind(
      "SELECT u.uid,u.open_id,u.status,p.nickname,p.gold,p.diamond "
      "FROM `user` u INNER JOIN player_profile p ON p.uid=u.uid WHERE u.account_type=1 AND u.open_id=? LIMIT 1",
      {Str(open_id)});
  if (!rows || rows->empty() || rows->front().cols.size() < 6) return std::nullopt;
  const auto& c = rows->front().cols;
  PlayerRecord p;
  try {
    p.uid = std::stoll(c[0]);
    p.open_id = c[1];
    p.status = std::stoi(c[2]);
    p.nickname = c[3].empty() ? ("Player" + c[0]) : c[3];
    p.gold = std::stoll(c[4]);
    p.diamond = std::stoll(c[5]);
  } catch (...) {
    return std::nullopt;
  }
  return p;
}

std::optional<PlayerRecord> MemoryStore::LoadFromMysqlByUid(int64_t uid) {
  if (!mysql_.Available() || uid <= 0) return std::nullopt;
  auto rows = mysql_.QueryBind(
      "SELECT u.uid,u.open_id,u.status,p.nickname,p.gold,p.diamond "
      "FROM `user` u INNER JOIN player_profile p ON p.uid=u.uid WHERE u.uid=? LIMIT 1",
      {I64(uid)});
  if (!rows || rows->empty() || rows->front().cols.size() < 6) return std::nullopt;
  const auto& c = rows->front().cols;
  PlayerRecord p;
  try {
    p.uid = std::stoll(c[0]);
    p.open_id = c[1];
    p.status = std::stoi(c[2]);
    p.nickname = c[3].empty() ? ("Player" + c[0]) : c[3];
    p.gold = std::stoll(c[4]);
    p.diamond = std::stoll(c[5]);
  } catch (...) {
    return std::nullopt;
  }
  return p;
}

bool MemoryStore::InsertMysqlUser(PlayerRecord& p) {
  if (!mysql_.Available()) return false;
  const int r = mysql_.ExecBind("INSERT INTO `user`(account_type,open_id,phone,status) VALUES(1,?,'',?)",
                                {Str(p.open_id), I64(p.status)});
  if (r < 0) {
    auto exist = LoadFromMysqlByOpenId(p.open_id);
    if (!exist) {
      PLOG_WARN("InsertMysqlUser fail: " << mysql_.LastError());
      return false;
    }
    p = *exist;
    return true;
  }
  auto loaded = LoadFromMysqlByOpenId(p.open_id);
  if (!loaded) {
    PLOG_WARN("InsertMysqlUser: insert ok but select failed open_id=" << p.open_id);
    return false;
  }
  p.uid = loaded->uid;
  if (p.nickname.empty()) p.nickname = "Player" + std::to_string(p.uid);
  mysql_.ExecBind(
      "INSERT INTO player_profile(uid,nickname,avatar,gold,diamond,level) VALUES(?,?,'',?,?,1) "
      "ON DUPLICATE KEY UPDATE nickname=VALUES(nickname)",
      {I64(p.uid), Str(p.nickname), I64(p.gold), I64(p.diamond)});
  return true;
}

PlayerRecord MemoryStore::CreateGuest(const std::string& device_id) {
  const std::string open_id = device_id.empty() ? ("guest-tmp-" + MakeToken()) : device_id;

  {
    std::lock_guard<std::mutex> lk(open_mu_);
    auto it = open_id_index_.find(open_id);
    if (it != open_id_index_.end()) {
      const int64_t uid = it->second;
      auto& sh = player_shards_[UidShard(uid)];
      std::lock_guard<std::mutex> plk(sh.mu);
      auto pit = sh.players.find(uid);
      if (pit != sh.players.end()) return pit->second;
    }
  }

  if (auto exist = LoadFromMysqlByOpenId(open_id)) {
    CachePlayer(*exist);
    WriteRedisProfile(*exist);
    return *exist;
  }

  PlayerRecord p;
  p.open_id = open_id;
  p.gold = 10000;
  p.diamond = 0;
  p.status = 0;
  p.nickname = "";

  if (InsertMysqlUser(p)) {
    if (p.nickname.empty() || p.nickname.rfind("Player", 0) == 0)
      p.nickname = "Player" + std::to_string(p.uid);
    if (mysql_.Available()) {
      mysql_.ExecBind("UPDATE player_profile SET nickname=? WHERE uid=?", {Str(p.nickname), I64(p.uid)});
    }
    CachePlayer(p);
    WriteRedisProfile(p);
    PLOG_INFO("user persisted uid=" << p.uid << " open_id=" << p.open_id);
    return p;
  }

  {
    std::lock_guard<std::mutex> lk(meta_mu_);
    p.uid = next_uid_++;
  }
  if (p.open_id.rfind("guest-tmp-", 0) == 0) p.open_id = "guest-" + std::to_string(p.uid);
  p.nickname = "Player" + std::to_string(p.uid);
  CachePlayer(p);
  PLOG_WARN("CreateGuest without mysql uid=" << p.uid);
  return p;
}

std::optional<PlayerRecord> MemoryStore::GetPlayer(int64_t uid) {
  {
    auto& sh = player_shards_[UidShard(uid)];
    std::lock_guard<std::mutex> lk(sh.mu);
    auto it = sh.players.find(uid);
    if (it != sh.players.end()) return it->second;
  }
  if (auto p = LoadFromMysqlByUid(uid)) {
    CachePlayer(*p);
    return p;
  }
  return std::nullopt;
}

SessionRecord MemoryStore::CreateSession(int64_t uid) {
  SessionRecord s;
  s.token = MakeToken();
  s.uid = uid;
  {
    auto& sh = session_shards_[TokenShard(s.token)];
    std::lock_guard<std::mutex> lk(sh.mu);
    sh.sessions[s.token] = s;
  }
  if (redis_.Available()) redis_.Set("sess:" + s.token, std::to_string(uid), 86400);
  return s;
}

std::optional<SessionRecord> MemoryStore::GetSession(const std::string& token) {
  {
    auto& sh = session_shards_[TokenShard(token)];
    std::lock_guard<std::mutex> lk(sh.mu);
    auto it = sh.sessions.find(token);
    if (it != sh.sessions.end()) return it->second;
  }
  if (redis_.Available()) {
    auto v = redis_.Get("sess:" + token);
    if (v && !v->empty()) {
      try {
        SessionRecord s;
        s.token = token;
        s.uid = std::stoll(*v);
        auto& sh = session_shards_[TokenShard(token)];
        std::lock_guard<std::mutex> lk(sh.mu);
        sh.sessions[token] = s;
        return s;
      } catch (...) {
      }
    }
  }
  return std::nullopt;
}

void MemoryStore::RevokeSession(const std::string& token) {
  {
    auto& sh = session_shards_[TokenShard(token)];
    std::lock_guard<std::mutex> lk(sh.mu);
    sh.sessions.erase(token);
  }
  if (redis_.Available()) redis_.Del("sess:" + token);
}

size_t MemoryStore::SessionCount() {
  size_t n = 0;
  for (auto& sh : session_shards_) {
    std::lock_guard<std::mutex> lk(sh.mu);
    n += sh.sessions.size();
  }
  return n;
}

size_t MemoryStore::PlayerCount() {
  if (mysql_.Available()) {
    auto rows = mysql_.Query("SELECT COUNT(*) FROM `user`");
    if (rows && !rows->empty() && !rows->front().cols.empty()) {
      try {
        return static_cast<size_t>(std::stoll(rows->front().cols[0]));
      } catch (...) {
      }
    }
  }
  size_t n = 0;
  for (auto& sh : player_shards_) {
    std::lock_guard<std::mutex> lk(sh.mu);
    n += sh.players.size();
  }
  return n;
}

void MemoryStore::SetStatus(int64_t uid, int status) {
  PlayerRecord snap;
  bool have = false;
  {
    auto& sh = player_shards_[UidShard(uid)];
    std::lock_guard<std::mutex> lk(sh.mu);
    auto it = sh.players.find(uid);
    if (it != sh.players.end()) {
      it->second.status = status;
      snap = it->second;
      have = true;
    }
  }
  if (have) {
    persist_.Post([this, snap]() { WriteRedisProfile(snap); });
  }
  if (mysql_.Available()) {
    persist_.Post([this, uid, status]() {
      mysql_.ExecBind("UPDATE `user` SET status=? WHERE uid=?", {I64(status), I64(uid)});
    });
  }
}

std::vector<PlayerRecord> MemoryStore::ListPlayers(size_t limit) {
  if (mysql_.Available()) {
    auto rows = mysql_.QueryBind(
        "SELECT u.uid,u.open_id,u.status,p.nickname,p.gold,p.diamond "
        "FROM `user` u INNER JOIN player_profile p ON p.uid=u.uid ORDER BY u.uid DESC LIMIT ?",
        {I64(static_cast<int64_t>(limit))});
    std::vector<PlayerRecord> out;
    if (rows) {
      for (const auto& row : *rows) {
        if (row.cols.size() < 6) continue;
        PlayerRecord p;
        try {
          p.uid = std::stoll(row.cols[0]);
          p.open_id = row.cols[1];
          p.status = std::stoi(row.cols[2]);
          p.nickname = row.cols[3];
          p.gold = std::stoll(row.cols[4]);
          p.diamond = std::stoll(row.cols[5]);
        } catch (...) {
          continue;
        }
        out.push_back(p);
      }
    }
    if (!out.empty()) return out;
  }
  std::vector<PlayerRecord> out;
  for (auto& sh : player_shards_) {
    std::lock_guard<std::mutex> lk(sh.mu);
    for (const auto& kv : sh.players) {
      out.push_back(kv.second);
      if (out.size() >= limit) return out;
    }
  }
  return out;
}

std::optional<int64_t> MemoryStore::Adjust(int64_t uid, Currency currency, int64_t delta, const std::string& biz_type,
                                           const std::string& idem_key, const std::string& ref_id) {
  // Ensure cached; cold miss may hit MySQL (login / first touch), not mid-hand after warm.
  if (!GetPlayer(uid)) return std::nullopt;

  PlayerRecord snap;
  LedgerEntry e;
  {
    auto& sh = player_shards_[UidShard(uid)];
    std::lock_guard<std::mutex> lk(sh.mu);
    if (!idem_key.empty()) {
      auto it = sh.idem_balance.find(idem_key);
      if (it != sh.idem_balance.end()) return it->second;
    }
    auto pit = sh.players.find(uid);
    if (pit == sh.players.end()) return std::nullopt;
    int64_t* bal = (currency == Currency::kDiamond) ? &pit->second.diamond : &pit->second.gold;
    const int64_t next = *bal + delta;
    if (next < 0) return std::nullopt;
    *bal = next;
    if (!idem_key.empty()) sh.idem_balance[idem_key] = next;

    e.uid = uid;
    e.currency = static_cast<int>(currency);
    e.delta = delta;
    e.balance_after = next;
    e.biz_type = biz_type;
    e.idempotent_key = idem_key;
    e.ref_id = ref_id;
    snap = pit->second;
  }

  {
    std::lock_guard<std::mutex> lk(meta_mu_);
    e.id = next_ledger_id_++;
    ledgers_.push_back(e);
    if (ledgers_.size() > 5000) {
      ledgers_.erase(ledgers_.begin(), ledgers_.begin() + static_cast<std::ptrdiff_t>(ledgers_.size() - 4000));
    }
  }

  EnqueuePersistAdjust(snap, e);
  return e.balance_after;
}

std::vector<LedgerEntry> MemoryStore::RecentLedgers(int64_t uid, size_t limit) {
  if (mysql_.Available()) {
    std::optional<std::vector<MysqlRow>> rows;
    if (uid > 0) {
      rows = mysql_.QueryBind(
          "SELECT id,uid,currency,delta,balance_after,biz_type,idempotent_key,ref_id FROM ledger "
          "WHERE uid=? ORDER BY id DESC LIMIT ?",
          {I64(uid), I64(static_cast<int64_t>(limit))});
    } else {
      rows = mysql_.QueryBind(
          "SELECT id,uid,currency,delta,balance_after,biz_type,idempotent_key,ref_id FROM ledger "
          "ORDER BY id DESC LIMIT ?",
          {I64(static_cast<int64_t>(limit))});
    }
    std::vector<LedgerEntry> out;
    if (rows) {
      for (const auto& row : *rows) {
        if (row.cols.size() < 8) continue;
        LedgerEntry e;
        try {
          e.id = std::stoll(row.cols[0]);
          e.uid = std::stoll(row.cols[1]);
          e.currency = std::stoi(row.cols[2]);
          e.delta = std::stoll(row.cols[3]);
          e.balance_after = std::stoll(row.cols[4]);
          e.biz_type = row.cols[5];
          e.idempotent_key = row.cols[6];
          e.ref_id = row.cols[7];
        } catch (...) {
          continue;
        }
        out.push_back(e);
      }
    }
    if (!out.empty()) return out;
  }
  std::lock_guard<std::mutex> lk(meta_mu_);
  std::vector<LedgerEntry> out;
  for (auto it = ledgers_.rbegin(); it != ledgers_.rend() && out.size() < limit; ++it) {
    if (uid == 0 || it->uid == uid) out.push_back(*it);
  }
  return out;
}

}  // namespace pandora
