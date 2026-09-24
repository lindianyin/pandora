#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace pandora {

class ISessionConn {
 public:
  virtual ~ISessionConn() = default;
  virtual void Send(uint32_t msg_id, const std::vector<uint8_t>& body) = 0;
  virtual void Close() {}
};

class SessionHub {
 public:
  static constexpr std::size_t kShards = 64;

  void Bind(int64_t uid, std::shared_ptr<ISessionConn> conn);
  void Unbind(int64_t uid, ISessionConn* conn);
  bool Send(int64_t uid, uint32_t msg_id, const std::vector<uint8_t>& body);
  void Broadcast(const std::vector<int64_t>& uids, uint32_t msg_id, const std::vector<uint8_t>& body);
  void BroadcastAll(uint32_t msg_id, const std::vector<uint8_t>& body);
  bool IsOnline(int64_t uid);
  void Kick(int64_t uid, int32_t reason, const std::string& message);
  void CloseAll();
  size_t OnlineCount();

 private:
  struct Shard {
    std::mutex mu;
    std::unordered_map<int64_t, std::shared_ptr<ISessionConn>> by_uid;
  };

  static std::size_t ShardOf(int64_t uid) {
    return static_cast<std::size_t>(uid >= 0 ? uid : -uid) % kShards;
  }

  std::array<Shard, kShards> shards_{};
};

}  // namespace pandora
