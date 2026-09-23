#pragma once

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
  void Bind(int64_t uid, std::shared_ptr<ISessionConn> conn);
  void Unbind(int64_t uid, ISessionConn* conn);
  bool Send(int64_t uid, uint32_t msg_id, const std::vector<uint8_t>& body);
  void Broadcast(const std::vector<int64_t>& uids, uint32_t msg_id, const std::vector<uint8_t>& body);
  void BroadcastAll(uint32_t msg_id, const std::vector<uint8_t>& body);
  bool IsOnline(int64_t uid);
  void Kick(int64_t uid, int32_t reason, const std::string& message);
  size_t OnlineCount();

 private:
  std::mutex mu_;
  std::unordered_map<int64_t, std::shared_ptr<ISessionConn>> by_uid_;
};

}  // namespace pandora

