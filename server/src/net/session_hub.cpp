#include "net/session_hub.hpp"

#include "common/errors.hpp"
#include "common/proto_wire.hpp"

namespace pandora {

void SessionHub::Bind(int64_t uid, std::shared_ptr<ISessionConn> conn) {
  std::lock_guard<std::mutex> lk(mu_);
  by_uid_[uid] = std::move(conn);
}

void SessionHub::Unbind(int64_t uid, ISessionConn* conn) {
  std::lock_guard<std::mutex> lk(mu_);
  auto it = by_uid_.find(uid);
  if (it != by_uid_.end() && it->second.get() == conn) by_uid_.erase(it);
}

bool SessionHub::Send(int64_t uid, uint32_t msg_id, const std::vector<uint8_t>& body) {
  std::shared_ptr<ISessionConn> c;
  {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = by_uid_.find(uid);
    if (it == by_uid_.end()) return false;
    c = it->second;
  }
  if (!c) return false;
  c->Send(msg_id, body);
  return true;
}

void SessionHub::Broadcast(const std::vector<int64_t>& uids, uint32_t msg_id, const std::vector<uint8_t>& body) {
  for (int64_t uid : uids) Send(uid, msg_id, body);
}

void SessionHub::BroadcastAll(uint32_t msg_id, const std::vector<uint8_t>& body) {
  std::vector<std::shared_ptr<ISessionConn>> conns;
  {
    std::lock_guard<std::mutex> lk(mu_);
    for (auto& kv : by_uid_) conns.push_back(kv.second);
  }
  for (auto& c : conns)
    if (c) c->Send(msg_id, body);
}

bool SessionHub::IsOnline(int64_t uid) {
  std::lock_guard<std::mutex> lk(mu_);
  return by_uid_.count(uid) != 0;
}

void SessionHub::Kick(int64_t uid, int32_t reason, const std::string& message) {
  std::shared_ptr<ISessionConn> c;
  {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = by_uid_.find(uid);
    if (it == by_uid_.end()) return;
    c = it->second;
    by_uid_.erase(it);
  }
  if (c) {
    c->Send(MsgId::kS2C_Kick, proto_wire::EncodeS2C_Kick(reason, message));
    c->Close();
  }
}

size_t SessionHub::OnlineCount() {
  std::lock_guard<std::mutex> lk(mu_);
  return by_uid_.size();
}

}  // namespace pandora
