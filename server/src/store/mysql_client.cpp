#include "store/mysql_client.hpp"

#include <mysql/mysql.h>

#include <chrono>

#include "common/log.hpp"

namespace pandora {

namespace {

thread_local std::string g_mysql_last_error;

std::string Part(const std::string& dsn, const char* key, const std::string& def) {
  const std::string needle = std::string(key) + "=";
  const auto p = dsn.find(needle);
  if (p == std::string::npos) return def;
  auto start = p + needle.size();
  auto end = dsn.find(';', start);
  if (end == std::string::npos) end = dsn.size();
  return dsn.substr(start, end - start);
}

}  // namespace

void MysqlClient::Borrowed::Release() {
  if (owner) {
    owner->ReleaseIndex(index);
    owner = nullptr;
    mysql = nullptr;
  }
}

MysqlClient::MysqlClient(std::string dsn, int pool_size) : dsn_(std::move(dsn)) {
  pool_size_ = pool_size < 1 ? 1 : pool_size;
  ParseDsn();
  slots_.resize(static_cast<std::size_t>(pool_size_));

  int ok = 0;
  for (auto& slot : slots_) {
    slot.mysql = ConnectOne();
    if (slot.mysql) ++ok;
  }
  available_.store(ok > 0, std::memory_order_relaxed);
  if (ok > 0) {
    PLOG_INFO("mysql pool ok " << host_ << ":" << port_ << "/" << database_ << " size=" << pool_size_
                               << " live=" << ok);
  } else {
    PLOG_WARN("mysql pool unavailable: " << g_mysql_last_error);
  }
}

MysqlClient::~MysqlClient() {
  {
    std::unique_lock<std::mutex> lk(mu_);
    stopping_ = true;
    cv_.notify_all();
    cv_.wait_for(lk, std::chrono::seconds(5), [this] { return borrowed_ == 0; });
    for (auto& slot : slots_) {
      CloseOne(slot.mysql);
      slot.busy = false;
    }
  }
  available_.store(false, std::memory_order_relaxed);
}

void MysqlClient::ParseDsn() {
  host_ = Part(dsn_, "host", host_);
  user_ = Part(dsn_, "user", user_);
  password_ = Part(dsn_, "password", password_);
  database_ = Part(dsn_, "database", database_);
  try {
    port_ = std::stoi(Part(dsn_, "port", std::to_string(port_)));
  } catch (...) {
  }
}

MYSQL* MysqlClient::ConnectOne() {
  MYSQL* m = mysql_init(nullptr);
  if (!m) {
    SetError("mysql_init failed");
    return nullptr;
  }
  if (!mysql_real_connect(m, host_.c_str(), user_.c_str(), password_.c_str(), database_.c_str(),
                          static_cast<unsigned>(port_), nullptr, CLIENT_MULTI_STATEMENTS)) {
    SetError(mysql_error(m));
    mysql_close(m);
    return nullptr;
  }
  mysql_set_character_set(m, "utf8mb4");
  g_mysql_last_error.clear();
  return m;
}

void MysqlClient::CloseOne(MYSQL*& mysql) {
  if (mysql) {
    mysql_close(mysql);
    mysql = nullptr;
  }
}

bool MysqlClient::ReconnectSlot(std::size_t index) {
  // Caller must hold mu_ only briefly; reconnect does network I/O outside lock when possible.
  MYSQL* old = nullptr;
  {
    std::lock_guard<std::mutex> lk(mu_);
    if (index >= slots_.size()) return false;
    old = slots_[index].mysql;
    slots_[index].mysql = nullptr;
  }
  CloseOne(old);
  MYSQL* neu = ConnectOne();
  {
    std::lock_guard<std::mutex> lk(mu_);
    if (index >= slots_.size() || stopping_) {
      CloseOne(neu);
      return false;
    }
    slots_[index].mysql = neu;
  }
  if (neu) available_.store(true, std::memory_order_relaxed);
  return neu != nullptr;
}

bool MysqlClient::EnsureAlive(Borrowed& b) {
  if (!b.mysql) return false;
  if (mysql_ping(b.mysql) == 0) return true;
  SetError(mysql_error(b.mysql));
  if (!ReconnectSlot(b.index)) return false;
  {
    std::lock_guard<std::mutex> lk(mu_);
    b.mysql = (b.index < slots_.size()) ? slots_[b.index].mysql : nullptr;
  }
  return b.mysql != nullptr;
}

std::optional<MysqlClient::Borrowed> MysqlClient::Acquire() {
  std::unique_lock<std::mutex> lk(mu_);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (!stopping_) {
    for (std::size_t i = 0; i < slots_.size(); ++i) {
      if (slots_[i].busy) continue;
      slots_[i].busy = true;
      ++borrowed_;
      MYSQL* m = slots_[i].mysql;
      lk.unlock();

      if (!m) {
        if (!ReconnectSlot(i)) {
          ReleaseIndex(i);
          return std::nullopt;
        }
        std::lock_guard<std::mutex> lk2(mu_);
        m = slots_[i].mysql;
      }
      if (!m) {
        ReleaseIndex(i);
        return std::nullopt;
      }
      return Borrowed{this, i, m};
    }
    if (cv_.wait_until(lk, deadline) == std::cv_status::timeout) {
      SetError("mysql pool acquire timeout");
      return std::nullopt;
    }
  }
  SetError("mysql pool stopping");
  return std::nullopt;
}

void MysqlClient::ReleaseIndex(std::size_t index) {
  std::lock_guard<std::mutex> lk(mu_);
  if (index >= slots_.size()) return;
  if (slots_[index].busy) {
    slots_[index].busy = false;
    --borrowed_;
    cv_.notify_one();
  }
}

void MysqlClient::SetError(std::string err) const { g_mysql_last_error = std::move(err); }

std::string MysqlClient::LastError() const { return g_mysql_last_error; }

bool MysqlClient::Ping() {
  auto b = Acquire();
  if (!b) {
    available_.store(false, std::memory_order_relaxed);
    return false;
  }
  const bool ok = EnsureAlive(*b);
  available_.store(ok, std::memory_order_relaxed);
  if (ok) g_mysql_last_error.clear();
  return ok;
}

int MysqlClient::ExecOn(Borrowed& b, const std::string& sql) {
  if (mysql_real_query(b.mysql, sql.c_str(), static_cast<unsigned long>(sql.size())) != 0) {
    SetError(mysql_error(b.mysql));
    return -1;
  }
  g_mysql_last_error.clear();
  return static_cast<int>(mysql_affected_rows(b.mysql));
}

int MysqlClient::Exec(const std::string& sql) {
  auto b = Acquire();
  if (!b) return -1;
  if (!EnsureAlive(*b)) return -1;

  int n = ExecOn(*b, sql);
  if (n >= 0) {
    available_.store(true, std::memory_order_relaxed);
    return n;
  }
  // Only retry when the connection is dead (not on SQL errors).
  if (b->mysql && mysql_ping(b->mysql) == 0) return -1;
  if (!ReconnectSlot(b->index)) {
    available_.store(false, std::memory_order_relaxed);
    return -1;
  }
  {
    std::lock_guard<std::mutex> lk(mu_);
    b->mysql = (b->index < slots_.size()) ? slots_[b->index].mysql : nullptr;
  }
  if (!b->mysql) {
    available_.store(false, std::memory_order_relaxed);
    return -1;
  }
  n = ExecOn(*b, sql);
  available_.store(n >= 0, std::memory_order_relaxed);
  return n;
}

std::optional<std::vector<MysqlRow>> MysqlClient::QueryOn(Borrowed& b, const std::string& sql) {
  if (mysql_real_query(b.mysql, sql.c_str(), static_cast<unsigned long>(sql.size())) != 0) {
    SetError(mysql_error(b.mysql));
    return std::nullopt;
  }
  MYSQL_RES* res = mysql_store_result(b.mysql);
  if (!res) {
    if (mysql_field_count(b.mysql) == 0) {
      g_mysql_last_error.clear();
      return std::vector<MysqlRow>{};
    }
    SetError(mysql_error(b.mysql));
    return std::nullopt;
  }
  const unsigned cols = mysql_num_fields(res);
  std::vector<MysqlRow> rows;
  MYSQL_ROW row;
  while ((row = mysql_fetch_row(res)) != nullptr) {
    MysqlRow r;
    r.cols.resize(cols);
    for (unsigned i = 0; i < cols; ++i) r.cols[i] = row[i] ? row[i] : "";
    rows.push_back(std::move(r));
  }
  mysql_free_result(res);
  g_mysql_last_error.clear();
  return rows;
}

std::optional<std::vector<MysqlRow>> MysqlClient::Query(const std::string& sql) {
  auto b = Acquire();
  if (!b) return std::nullopt;
  if (!EnsureAlive(*b)) return std::nullopt;

  auto rows = QueryOn(*b, sql);
  if (rows) {
    available_.store(true, std::memory_order_relaxed);
    return rows;
  }
  if (b->mysql && mysql_ping(b->mysql) == 0) return std::nullopt;
  if (!ReconnectSlot(b->index)) {
    available_.store(false, std::memory_order_relaxed);
    return std::nullopt;
  }
  {
    std::lock_guard<std::mutex> lk(mu_);
    b->mysql = (b->index < slots_.size()) ? slots_[b->index].mysql : nullptr;
  }
  if (!b->mysql) {
    available_.store(false, std::memory_order_relaxed);
    return std::nullopt;
  }
  rows = QueryOn(*b, sql);
  available_.store(rows.has_value(), std::memory_order_relaxed);
  return rows;
}

}  // namespace pandora
