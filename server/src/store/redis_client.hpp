#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace sw {
namespace redis {
class Redis;
}
}  // namespace sw

namespace pandora {

class RedisClient {
 public:
  // uri: redis://127.0.0.1:6379/0
  explicit RedisClient(std::string uri, int pool_size = 10);
  ~RedisClient();

  RedisClient(const RedisClient&) = delete;
  RedisClient& operator=(const RedisClient&) = delete;

  bool Ping();
  bool Available() const { return available_.load(std::memory_order_relaxed); }
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
  void ParseUri();
  bool EnsureConnected();
  std::shared_ptr<sw::redis::Redis> GetRedis();
  void SetError(std::string err) const;
  bool RunWithRetry(const std::function<void(sw::redis::Redis&)>& fn);

  std::string uri_;
  std::string host_{"127.0.0.1"};
  int port_{6379};
  int db_{0};
  int pool_size_{10};

  mutable std::mutex mu_;
  std::shared_ptr<sw::redis::Redis> redis_;
  std::atomic<bool> available_{false};
};

}  // namespace pandora
