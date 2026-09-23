#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace pandora {

class RedisClient {
 public:
  // uri: redis://127.0.0.1:6379/0
  explicit RedisClient(std::string uri);

  bool Ping();
  bool Available() const { return available_; }

  bool Set(const std::string& key, const std::string& value, int ttl_sec = 0);
  std::optional<std::string> Get(const std::string& key);
  bool Del(const std::string& key);
  // DECR; returns new value, or nullopt on error / missing key treated as 0 then -1
  std::optional<int64_t> Decr(const std::string& key);
  std::string LastError() const { return last_error_; }

 private:
  bool EnsureConnected();
  void Disconnect();
  std::optional<std::string> Command(const std::vector<std::string>& args);

  std::string uri_;
  std::string host_{"127.0.0.1"};
  int port_{6379};
  int db_{0};
  bool available_{false};
  std::string last_error_;
  mutable std::mutex mu_;
  uintptr_t sock_{static_cast<uintptr_t>(-1)};
};

}  // namespace pandora

