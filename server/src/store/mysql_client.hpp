#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

struct MYSQL;

namespace pandora {

struct MysqlRow {
  std::vector<std::string> cols;
};

class MysqlClient {
 public:
  // dsn: host=127.0.0.1;port=3306;user=pandora;password=pandora;database=pandora
  explicit MysqlClient(std::string dsn, int pool_size = 10);
  ~MysqlClient();

  MysqlClient(const MysqlClient&) = delete;
  MysqlClient& operator=(const MysqlClient&) = delete;

  bool Ping();
  bool Available() const { return available_.load(std::memory_order_relaxed); }
  int PoolSize() const { return pool_size_; }

  // Returns affected rows on success, -1 on failure.
  int Exec(const std::string& sql);
  std::optional<std::vector<MysqlRow>> Query(const std::string& sql);
  std::string LastError() const;

 private:
  struct Slot {
    MYSQL* mysql{nullptr};
    bool busy{false};
  };

  struct Borrowed {
    MysqlClient* owner{nullptr};
    std::size_t index{0};
    MYSQL* mysql{nullptr};

    Borrowed() = default;
    Borrowed(MysqlClient* o, std::size_t i, MYSQL* m) : owner(o), index(i), mysql(m) {}
    Borrowed(const Borrowed&) = delete;
    Borrowed& operator=(const Borrowed&) = delete;
    Borrowed(Borrowed&& other) noexcept
        : owner(other.owner), index(other.index), mysql(other.mysql) {
      other.owner = nullptr;
      other.mysql = nullptr;
    }
    Borrowed& operator=(Borrowed&& other) noexcept {
      if (this != &other) {
        Release();
        owner = other.owner;
        index = other.index;
        mysql = other.mysql;
        other.owner = nullptr;
        other.mysql = nullptr;
      }
      return *this;
    }
    ~Borrowed() { Release(); }

    explicit operator bool() const { return mysql != nullptr; }
    void Release();
  };

  void ParseDsn();
  MYSQL* ConnectOne();
  void CloseOne(MYSQL*& mysql);
  bool ReconnectSlot(std::size_t index);
  bool EnsureAlive(Borrowed& b);
  std::optional<Borrowed> Acquire();
  void ReleaseIndex(std::size_t index);
  void SetError(std::string err) const;
  int ExecOn(Borrowed& b, const std::string& sql);
  std::optional<std::vector<MysqlRow>> QueryOn(Borrowed& b, const std::string& sql);

  std::string dsn_;
  std::string host_{"127.0.0.1"};
  int port_{3306};
  std::string user_{"pandora"};
  std::string password_{"pandora"};
  std::string database_{"pandora"};
  int pool_size_{10};

  mutable std::mutex mu_;
  std::condition_variable cv_;
  std::vector<Slot> slots_;
  int borrowed_{0};
  bool stopping_{false};
  std::atomic<bool> available_{false};
};

}  // namespace pandora
