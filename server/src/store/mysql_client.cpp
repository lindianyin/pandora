#include "store/mysql_client.hpp"

#include <mysql/mysql.h>

#include <chrono>
#include <cstring>
#include <memory>
#include <thread>
#include <utility>

#include "common/log.hpp"

// Windows headers may `#define error`; MYSQL_BIND has a member named `error`.
#ifdef error
#undef error
#endif

namespace pandora {

namespace {

thread_local std::string g_mysql_last_error;

void HardCloseMysql(MYSQL* mysql) {
  if (!mysql) return;
  // Bound read/write timeouts (set at connect) keep COM_QUIT from hanging long.
  mysql_close(mysql);
}

std::string Part(const std::string& dsn, const char* key, const std::string& def) {
  const std::string needle = std::string(key) + "=";
  const auto p = dsn.find(needle);
  if (p == std::string::npos) return def;
  auto start = p + needle.size();
  auto end = dsn.find(';', start);
  if (end == std::string::npos) end = dsn.size();
  return dsn.substr(start, end - start);
}

struct StmtDeleter {
  void operator()(MYSQL_STMT* s) const {
    if (s) mysql_stmt_close(s);
  }
};

using StmtPtr = std::unique_ptr<MYSQL_STMT, StmtDeleter>;

struct BindScratch {
  std::vector<MYSQL_BIND> binds;
  std::vector<char> is_null;
  std::vector<unsigned long> lengths;
  std::vector<const char*> str_ptrs;
};

bool FillParamBinds(BindScratch& scratch, const std::vector<SqlArg>& args, std::string& err) {
  const std::size_t n = args.size();
  scratch.binds.assign(n, MYSQL_BIND{});
  scratch.is_null.assign(n, 0);
  scratch.lengths.assign(n, 0);
  scratch.str_ptrs.assign(n, nullptr);

  for (std::size_t i = 0; i < n; ++i) {
    MYSQL_BIND& b = scratch.binds[i];
    std::memset(&b, 0, sizeof(b));
    const SqlArg& a = args[i];
    switch (a.type) {
      case SqlArg::Type::Null:
        scratch.is_null[i] = 1;
        b.buffer_type = MYSQL_TYPE_NULL;
        b.is_null = reinterpret_cast<bool*>(&scratch.is_null[i]);
        break;
      case SqlArg::Type::Int64:
        b.buffer_type = MYSQL_TYPE_LONGLONG;
        b.buffer = const_cast<int64_t*>(&a.i64);
        b.is_unsigned = 0;
        b.is_null = reinterpret_cast<bool*>(&scratch.is_null[i]);
        break;
      case SqlArg::Type::String:
        scratch.lengths[i] = static_cast<unsigned long>(a.str.size());
        scratch.str_ptrs[i] = a.str.data();
        b.buffer_type = MYSQL_TYPE_STRING;
        b.buffer = const_cast<char*>(scratch.str_ptrs[i]);
        b.buffer_length = scratch.lengths[i];
        b.length = &scratch.lengths[i];
        b.is_null = reinterpret_cast<bool*>(&scratch.is_null[i]);
        break;
      default:
        err = "unknown SqlArg type";
        return false;
    }
  }
  return true;
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
  std::vector<MYSQL*> to_close;
  {
    std::unique_lock<std::mutex> lk(mu_);
    stopping_ = true;
    cv_.notify_all();
    // Borrowed RAII should release quickly; do not stall shutdown for seconds.
    cv_.wait_for(lk, std::chrono::milliseconds(100), [this] { return borrowed_ == 0; });
    to_close.reserve(slots_.size());
    for (auto& slot : slots_) {
      if (slot.mysql) {
        to_close.push_back(slot.mysql);
        slot.mysql = nullptr;
      }
      slot.busy = false;
    }
    borrowed_ = 0;
  }
  available_.store(false, std::memory_order_relaxed);
  // Close in parallel — serial mysql_close to Docker/MySQL on Windows is often multi-second.
  std::vector<std::thread> closers;
  closers.reserve(to_close.size());
  for (MYSQL* m : to_close) {
    closers.emplace_back([m]() { HardCloseMysql(m); });
  }
  for (auto& t : closers) t.join();
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
  unsigned int connect_timeout = 2;
  unsigned int rw_timeout = 1;
  mysql_options(m, MYSQL_OPT_CONNECT_TIMEOUT, &connect_timeout);
  mysql_options(m, MYSQL_OPT_READ_TIMEOUT, &rw_timeout);
  mysql_options(m, MYSQL_OPT_WRITE_TIMEOUT, &rw_timeout);
  // Prefer prepared statements; multi-statements remain for legacy Exec paths.
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
  if (!mysql) return;
  HardCloseMysql(mysql);
  mysql = nullptr;
}

bool MysqlClient::ReconnectSlot(std::size_t index) {
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

  int n = ExecOn(*b, sql);
  if (n >= 0) {
    available_.store(true, std::memory_order_relaxed);
    return n;
  }
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

int MysqlClient::ExecBindOn(Borrowed& b, const std::string& sql, const std::vector<SqlArg>& args) {
  StmtPtr stmt(mysql_stmt_init(b.mysql));
  if (!stmt) {
    SetError("mysql_stmt_init failed");
    return -1;
  }
  if (mysql_stmt_prepare(stmt.get(), sql.c_str(), static_cast<unsigned long>(sql.size())) != 0) {
    SetError(mysql_stmt_error(stmt.get()));
    return -1;
  }
  const unsigned long param_count = mysql_stmt_param_count(stmt.get());
  if (param_count != args.size()) {
    SetError("ExecBind param count mismatch: sql expects " + std::to_string(param_count) + " got " +
             std::to_string(args.size()));
    return -1;
  }
  BindScratch scratch;
  std::string bind_err;
  if (!FillParamBinds(scratch, args, bind_err)) {
    SetError(bind_err);
    return -1;
  }
  if (param_count > 0 && mysql_stmt_bind_param(stmt.get(), scratch.binds.data()) != 0) {
    SetError(mysql_stmt_error(stmt.get()));
    return -1;
  }
  if (mysql_stmt_execute(stmt.get()) != 0) {
    SetError(mysql_stmt_error(stmt.get()));
    return -1;
  }
  g_mysql_last_error.clear();
  return static_cast<int>(mysql_stmt_affected_rows(stmt.get()));
}

std::optional<std::vector<MysqlRow>> MysqlClient::QueryBindOn(Borrowed& b, const std::string& sql,
                                                             const std::vector<SqlArg>& args) {
  StmtPtr stmt(mysql_stmt_init(b.mysql));
  if (!stmt) {
    SetError("mysql_stmt_init failed");
    return std::nullopt;
  }
  if (mysql_stmt_prepare(stmt.get(), sql.c_str(), static_cast<unsigned long>(sql.size())) != 0) {
    SetError(mysql_stmt_error(stmt.get()));
    return std::nullopt;
  }
  const unsigned long param_count = mysql_stmt_param_count(stmt.get());
  if (param_count != args.size()) {
    SetError("QueryBind param count mismatch: sql expects " + std::to_string(param_count) + " got " +
             std::to_string(args.size()));
    return std::nullopt;
  }
  BindScratch scratch;
  std::string bind_err;
  if (!FillParamBinds(scratch, args, bind_err)) {
    SetError(bind_err);
    return std::nullopt;
  }
  if (param_count > 0 && mysql_stmt_bind_param(stmt.get(), scratch.binds.data()) != 0) {
    SetError(mysql_stmt_error(stmt.get()));
    return std::nullopt;
  }
  if (mysql_stmt_execute(stmt.get()) != 0) {
    SetError(mysql_stmt_error(stmt.get()));
    return std::nullopt;
  }

  MYSQL_RES* meta = mysql_stmt_result_metadata(stmt.get());
  if (!meta) {
    // Not a result set (e.g. UPDATE via QueryBind) — treat as empty success.
    g_mysql_last_error.clear();
    return std::vector<MysqlRow>{};
  }

  const unsigned cols = mysql_num_fields(meta);
  MYSQL_FIELD* fields = mysql_fetch_fields(meta);

  std::vector<MYSQL_BIND> out_binds(cols);
  std::vector<std::vector<char>> buffers(cols);
  std::vector<unsigned long> col_lens(cols);
  std::vector<char> null_flags(cols, 0);
  std::vector<char> err_flags(cols, 0);
  std::memset(out_binds.data(), 0, sizeof(MYSQL_BIND) * cols);

  for (unsigned i = 0; i < cols; ++i) {
    // Cap per-column buffer; enlarge on truncation via length.
    unsigned long cap = fields[i].length;
    if (cap < 64) cap = 64;
    if (cap > 65536) cap = 65536;
    buffers[i].assign(cap, '\0');
    out_binds[i].buffer_type = MYSQL_TYPE_STRING;
    out_binds[i].buffer = buffers[i].data();
    out_binds[i].buffer_length = cap;
    out_binds[i].length = &col_lens[i];
    out_binds[i].is_null = reinterpret_cast<bool*>(&null_flags[i]);
    out_binds[i].error = reinterpret_cast<bool*>(&err_flags[i]);
  }
  mysql_free_result(meta);

  if (mysql_stmt_bind_result(stmt.get(), out_binds.data()) != 0) {
    SetError(mysql_stmt_error(stmt.get()));
    return std::nullopt;
  }
  if (mysql_stmt_store_result(stmt.get()) != 0) {
    SetError(mysql_stmt_error(stmt.get()));
    return std::nullopt;
  }

  std::vector<MysqlRow> rows;
  while (true) {
    const int fr = mysql_stmt_fetch(stmt.get());
    if (fr == MYSQL_NO_DATA) break;
    if (fr != 0 && fr != MYSQL_DATA_TRUNCATED) {
      SetError(mysql_stmt_error(stmt.get()));
      return std::nullopt;
    }
    MysqlRow row;
    row.cols.resize(cols);
    for (unsigned i = 0; i < cols; ++i) {
      if (null_flags[i]) {
        row.cols[i].clear();
        continue;
      }
      if (col_lens[i] > out_binds[i].buffer_length) {
        // Truncated — refetch this column with exact length.
        std::vector<char> big(col_lens[i] + 1, '\0');
        MYSQL_BIND one{};
        unsigned long got = col_lens[i];
        char ni = 0;
        one.buffer_type = MYSQL_TYPE_STRING;
        one.buffer = big.data();
        one.buffer_length = got;
        one.length = &got;
        one.is_null = reinterpret_cast<bool*>(&ni);
        if (mysql_stmt_fetch_column(stmt.get(), &one, i, 0) == 0 && !ni) {
          row.cols[i].assign(big.data(), got);
        }
      } else {
        row.cols[i].assign(buffers[i].data(), col_lens[i]);
      }
    }
    rows.push_back(std::move(row));
  }

  g_mysql_last_error.clear();
  return rows;
}

int MysqlClient::ExecBind(const std::string& sql, std::initializer_list<SqlArg> args) {
  return ExecBind(sql, std::vector<SqlArg>(args));
}

int MysqlClient::ExecBind(const std::string& sql, const std::vector<SqlArg>& args) {
  auto b = Acquire();
  if (!b) return -1;

  int n = ExecBindOn(*b, sql, args);
  if (n >= 0) {
    available_.store(true, std::memory_order_relaxed);
    return n;
  }
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
  n = ExecBindOn(*b, sql, args);
  available_.store(n >= 0, std::memory_order_relaxed);
  return n;
}

std::optional<std::vector<MysqlRow>> MysqlClient::QueryBind(const std::string& sql,
                                                           std::initializer_list<SqlArg> args) {
  return QueryBind(sql, std::vector<SqlArg>(args));
}

std::optional<std::vector<MysqlRow>> MysqlClient::QueryBind(const std::string& sql, const std::vector<SqlArg>& args) {
  auto b = Acquire();
  if (!b) return std::nullopt;

  auto rows = QueryBindOn(*b, sql, args);
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
  rows = QueryBindOn(*b, sql, args);
  available_.store(rows.has_value(), std::memory_order_relaxed);
  return rows;
}

}  // namespace pandora
