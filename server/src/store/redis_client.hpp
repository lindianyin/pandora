#pragma once

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

struct redisContext;

namespace pandora {

class RedisClient {
 public:
  // uri: redis://127.0.0.1:6379/0
  explicit RedisClient(std::string uri, int pool_size = 10);
  ~RedisClient();

  RedisClient(const RedisClient&) = delete;
  RedisClient& operator=(const RedisClient&) = delete;

  bool Ping();
  int PoolSize() const { return pool_size_; }

  bool Set(const std::string& key, const std::string& value, int ttl_sec = 0);
  // SET NX: returns true only when key was newly set.
  bool SetNx(const std::string& key, const std::string& value, int ttl_sec = 0);
  std::optional<std::string> Get(const std::string& key);
  bool Del(const std::string& key);
  std::optional<int64_t> Decr(const std::string& key);
  bool Expire(const std::string& key, int ttl_sec);
  bool ZAdd(const std::string& key, const std::string& member, double score);
  // Descending range with scores; out as (member, score) pairs.
  bool ZRevRangeWithScores(const std::string& key, long long start, long long stop,
                           std::vector<std::pair<std::string, double>>* out);
  // 0-based rank from highest score; nullopt if member missing.
  std::optional<long long> ZRevRank(const std::string& key, const std::string& member);
  std::optional<double> ZScore(const std::string& key, const std::string& member);
  std::string LastError() const;

 private:
  struct Slot {
    redisContext* ctx{nullptr};
    bool busy{false};
  };

  struct Borrowed {
    RedisClient* owner{nullptr};
    std::size_t index{0};
    redisContext* ctx{nullptr};

    Borrowed() = default;
    Borrowed(RedisClient* o, std::size_t i, redisContext* c) : owner(o), index(i), ctx(c) {}
    Borrowed(const Borrowed&) = delete;
    Borrowed& operator=(const Borrowed&) = delete;
    Borrowed(Borrowed&& other) noexcept
        : owner(other.owner), index(other.index), ctx(other.ctx) {
      other.owner = nullptr;
      other.ctx = nullptr;
    }
    Borrowed& operator=(Borrowed&& other) noexcept {
      if (this != &other) {
        Release();
        owner = other.owner;
        index = other.index;
        ctx = other.ctx;
        other.owner = nullptr;
        other.ctx = nullptr;
      }
      return *this;
    }
    ~Borrowed() { Release(); }

    explicit operator bool() const { return ctx != nullptr; }
    void Release();
  };

  void ParseUri();
  redisContext* ConnectOne();
  void CloseOne(redisContext*& ctx);
  bool ReconnectSlot(std::size_t index);
  std::optional<Borrowed> Acquire();
  void ReleaseIndex(std::size_t index);
  void SetError(std::string err) const;
  bool WithConn(const std::function<bool(redisContext*)>& fn);

  std::string uri_;
  std::string host_{"127.0.0.1"};
  int port_{6379};
  int db_{0};
  int pool_size_{10};

  mutable std::mutex mu_;
  std::condition_variable cv_;
  std::vector<Slot> slots_;
  int borrowed_{0};
  bool stopping_{false};
};

}  // namespace pandora
