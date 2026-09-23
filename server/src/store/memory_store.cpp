#include "store/memory_store.hpp"

#include <random>
#include <sstream>

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

MemoryStore::MemoryStore(MysqlClient& mysql, RedisClient& redis) : mysql_(mysql), redis_(redis) {}

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
  std::ostringstream oss;
  oss << "{\"uid\":" << p.uid << ",\"open_id\":\"" << EscapeSql(p.open_id) << "\",\"nickname\":\""
      << EscapeSql(p.nickname) << "\",\"gold\":" << p.gold << ",\"diamond\":" << p.diamond
      << ",\"status\":" << p.status << "}";
  redis_.Set("player:" + std::to_string(p.uid), oss.str(), 3600);
  redis_.Set("user:open:1:" + p.open_id, std::to_string(p.uid), 3600);
}

void MemoryStore::CachePlayer(const PlayerRecord& p) {
  players_[p.uid] = p;
  if (!p.open_id.empty()) open_id_index_[p.open_id] = p.uid;
  WriteRedisProfile(p);
}

std::optional<PlayerRecord> MemoryStore::LoadFromMysqlByOpenId(const std::string& open_id) {
  if (!mysql_.Available() || open_id.empty()) return std::nullopt;
  auto rows = mysql_.Query(
      "SELECT u.uid,u.open_id,u.status,IFNULL(p.nickname,''),IFNULL(p.gold,10000),IFNULL(p.diamond,0) "
      "FROM `user` u LEFT JOIN player_profile p ON p.uid=u.uid WHERE u.account_type=1 AND u.open_id='" +
      EscapeSql(open_id) + "' LIMIT 1");
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
  auto rows = mysql_.Query(
      "SELECT u.uid,u.open_id,u.status,IFNULL(p.nickname,''),IFNULL(p.gold,10000),IFNULL(p.diamond,0) "
      "FROM `user` u LEFT JOIN player_profile p ON p.uid=u.uid WHERE u.uid=" +
      std::to_string(uid) + " LIMIT 1");
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
  const int r = mysql_.Exec("INSERT INTO `user`(account_type,open_id,status) VALUES(1,'" + EscapeSql(p.open_id) +
                            "'," + std::to_string(p.status) + ")");
  if (r < 0) {
    // duplicate open_id — load existing
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
  mysql_.Exec("INSERT INTO player_profile(uid,nickname,avatar,gold,diamond,level) VALUES(" + std::to_string(p.uid) +
              ",'" + EscapeSql(p.nickname) + "',''," + std::to_string(p.gold) + "," + std::to_string(p.diamond) +
              ",1) ON DUPLICATE KEY UPDATE nickname=VALUES(nickname)");
  return true;
}

PlayerRecord MemoryStore::CreateGuest(const std::string& device_id) {
  const std::string open_id = device_id.empty() ? ("guest-tmp-" + MakeToken()) : device_id;

  {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = open_id_index_.find(open_id);
    if (it != open_id_index_.end()) {
      auto pit = players_.find(it->second);
      if (pit != players_.end()) return pit->second;
    }
  }

  if (auto exist = LoadFromMysqlByOpenId(open_id)) {
    std::lock_guard<std::mutex> lk(mu_);
    CachePlayer(*exist);
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
    // ensure nickname persisted
    if (mysql_.Available()) {
      mysql_.Exec("UPDATE player_profile SET nickname='" + EscapeSql(p.nickname) + "' WHERE uid=" +
                  std::to_string(p.uid));
    }
    std::lock_guard<std::mutex> lk(mu_);
    CachePlayer(p);
    PLOG_INFO("user persisted uid=" << p.uid << " open_id=" << p.open_id);
    return p;
  }

  // MySQL unavailable: local-only fallback
  std::lock_guard<std::mutex> lk(mu_);
  p.uid = next_uid_++;
  if (p.open_id.rfind("guest-tmp-", 0) == 0) p.open_id = "guest-" + std::to_string(p.uid);
  p.nickname = "Player" + std::to_string(p.uid);
  CachePlayer(p);
  PLOG_WARN("CreateGuest without mysql uid=" << p.uid);
  return p;
}

std::optional<PlayerRecord> MemoryStore::GetPlayer(int64_t uid) {
  {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = players_.find(uid);
    if (it != players_.end()) return it->second;
  }
  if (auto p = LoadFromMysqlByUid(uid)) {
    std::lock_guard<std::mutex> lk(mu_);
    CachePlayer(*p);
    return p;
  }
  return std::nullopt;
}

SessionRecord MemoryStore::CreateSession(int64_t uid) {
  std::lock_guard<std::mutex> lk(mu_);
  SessionRecord s;
  s.token = MakeToken();
  s.uid = uid;
  sessions_[s.token] = s;
  if (redis_.Available()) redis_.Set("sess:" + s.token, std::to_string(uid), 86400);
  return s;
}

std::optional<SessionRecord> MemoryStore::GetSession(const std::string& token) {
  {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = sessions_.find(token);
    if (it != sessions_.end()) return it->second;
  }
  if (redis_.Available()) {
    auto v = redis_.Get("sess:" + token);
    if (v && !v->empty()) {
      try {
        SessionRecord s;
        s.token = token;
        s.uid = std::stoll(*v);
        std::lock_guard<std::mutex> lk(mu_);
        sessions_[token] = s;
        return s;
      } catch (...) {
      }
    }
  }
  return std::nullopt;
}

void MemoryStore::RevokeSession(const std::string& token) {
  std::lock_guard<std::mutex> lk(mu_);
  sessions_.erase(token);
  if (redis_.Available()) redis_.Del("sess:" + token);
}

size_t MemoryStore::SessionCount() {
  std::lock_guard<std::mutex> lk(mu_);
  return sessions_.size();
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
  std::lock_guard<std::mutex> lk(mu_);
  return players_.size();
}

void MemoryStore::SetStatus(int64_t uid, int status) {
  {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = players_.find(uid);
    if (it != players_.end()) {
      it->second.status = status;
      WriteRedisProfile(it->second);
    }
  }
  if (mysql_.Available()) {
    mysql_.Exec("UPDATE `user` SET status=" + std::to_string(status) + " WHERE uid=" + std::to_string(uid));
  }
}

std::vector<PlayerRecord> MemoryStore::ListPlayers(size_t limit) {
  if (mysql_.Available()) {
    auto rows = mysql_.Query(
        "SELECT u.uid,u.open_id,u.status,IFNULL(p.nickname,''),IFNULL(p.gold,0),IFNULL(p.diamond,0) "
        "FROM `user` u LEFT JOIN player_profile p ON p.uid=u.uid ORDER BY u.uid DESC LIMIT " +
        std::to_string(limit));
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
  std::lock_guard<std::mutex> lk(mu_);
  std::vector<PlayerRecord> out;
  for (const auto& kv : players_) {
    out.push_back(kv.second);
    if (out.size() >= limit) break;
  }
  return out;
}

std::optional<int64_t> MemoryStore::Adjust(int64_t uid, Currency currency, int64_t delta, const std::string& biz_type,
                                           const std::string& idem_key, const std::string& ref_id) {
  if (!idem_key.empty() && mysql_.Available()) {
    auto rows = mysql_.Query("SELECT balance_after FROM ledger WHERE idempotent_key='" + EscapeSql(idem_key) +
                             "' LIMIT 1");
    if (rows && !rows->empty() && !rows->front().cols.empty()) {
      try {
        const int64_t bal = std::stoll(rows->front().cols[0]);
        std::lock_guard<std::mutex> lk(mu_);
        idem_balance_[idem_key] = bal;
        auto it = players_.find(uid);
        if (it != players_.end()) {
          if (currency == Currency::kDiamond)
            it->second.diamond = bal;
          else
            it->second.gold = bal;
          WriteRedisProfile(it->second);
        }
        return bal;
      } catch (...) {
      }
    }
  }

  // ensure player loaded
  if (!GetPlayer(uid)) return std::nullopt;

  std::lock_guard<std::mutex> lk(mu_);
  if (!idem_key.empty()) {
    auto it = idem_balance_.find(idem_key);
    if (it != idem_balance_.end()) return it->second;
  }
  auto pit = players_.find(uid);
  if (pit == players_.end()) return std::nullopt;
  int64_t* bal = (currency == Currency::kDiamond) ? &pit->second.diamond : &pit->second.gold;
  const int64_t next = *bal + delta;
  if (next < 0) return std::nullopt;
  *bal = next;
  if (!idem_key.empty()) idem_balance_[idem_key] = next;

  LedgerEntry e;
  e.id = next_ledger_id_++;
  e.uid = uid;
  e.currency = static_cast<int>(currency);
  e.delta = delta;
  e.balance_after = next;
  e.biz_type = biz_type;
  e.idempotent_key = idem_key;
  e.ref_id = ref_id;
  ledgers_.push_back(e);

  WriteRedisProfile(pit->second);

  if (mysql_.Available()) {
    const char* col = (currency == Currency::kDiamond) ? "diamond" : "gold";
    mysql_.Exec(std::string("UPDATE player_profile SET ") + col + "=" + std::to_string(next) +
                " WHERE uid=" + std::to_string(uid));
    if (!idem_key.empty()) {
      mysql_.Exec("INSERT INTO ledger(uid,currency,delta,balance_after,biz_type,idempotent_key,ref_id) VALUES(" +
                  std::to_string(uid) + "," + std::to_string(static_cast<int>(currency)) + "," +
                  std::to_string(delta) + "," + std::to_string(next) + ",'" + EscapeSql(biz_type) + "','" +
                  EscapeSql(idem_key) + "','" + EscapeSql(ref_id) + "')");
    }
  }
  return next;
}

std::vector<LedgerEntry> MemoryStore::RecentLedgers(int64_t uid, size_t limit) {
  if (mysql_.Available()) {
    std::string sql =
        "SELECT id,uid,currency,delta,balance_after,biz_type,idempotent_key,IFNULL(ref_id,'') FROM ledger";
    if (uid > 0) sql += " WHERE uid=" + std::to_string(uid);
    sql += " ORDER BY id DESC LIMIT " + std::to_string(limit);
    auto rows = mysql_.Query(sql);
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
  std::lock_guard<std::mutex> lk(mu_);
  std::vector<LedgerEntry> out;
  for (auto it = ledgers_.rbegin(); it != ledgers_.rend() && out.size() < limit; ++it) {
    if (uid == 0 || it->uid == uid) out.push_back(*it);
  }
  return out;
}

}  // namespace pandora
