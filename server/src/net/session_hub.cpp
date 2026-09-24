#include "net/session_hub.hpp"

#include "common/errors.hpp"
#include "common/proto_wire.hpp"

namespace pandora {

void SessionHub::Bind(int64_t uid, std::shared_ptr<ISessionConn> conn) {
  auto& sh = shards_[ShardOf(uid)];
  std::lock_guard<std::mutex> lk(sh.mu);
  sh.by_uid[uid] = std::move(conn);
}

void SessionHub::Unbind(int64_t uid, ISessionConn* conn) {
  auto& sh = shards_[ShardOf(uid)];
  std::lock_guard<std::mutex> lk(sh.mu);
  auto it = sh.by_uid.find(uid);
  if (it != sh.by_uid.end() && it->second.get() == conn) sh.by_uid.erase(it);
}

bool SessionHub::Send(int64_t uid, uint32_t msg_id, const std::vector<uint8_t>& body) {
  std::shared_ptr<ISessionConn> c;
  {
    auto& sh = shards_[ShardOf(uid)];
    std::lock_guard<std::mutex> lk(sh.mu);
    auto it = sh.by_uid.find(uid);
    if (it == sh.by_uid.end()) return false;
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
  for (auto& sh : shards_) {
    std::lock_guard<std::mutex> lk(sh.mu);
    for (auto& kv : sh.by_uid) conns.push_back(kv.second);
  }
  for (auto& c : conns)
    if (c) c->Send(msg_id, body);
}

bool SessionHub::IsOnline(int64_t uid) {
  auto& sh = shards_[ShardOf(uid)];
  std::lock_guard<std::mutex> lk(sh.mu);
  return sh.by_uid.count(uid) != 0;
}

void SessionHub::Kick(int64_t uid, int32_t reason, const std::string& message) {
  std::shared_ptr<ISessionConn> c;
  {
    auto& sh = shards_[ShardOf(uid)];
    std::lock_guard<std::mutex> lk(sh.mu);
    auto it = sh.by_uid.find(uid);
    if (it == sh.by_uid.end()) return;
    c = it->second;
    sh.by_uid.erase(it);
  }
  if (c) {
    c->Send(MsgId::kS2C_Kick, proto_wire::EncodeS2C_Kick(reason, message));
    c->Close();
  }
}

size_t SessionHub::OnlineCount() {
  size_t n = 0;
  for (auto& sh : shards_) {
    std::lock_guard<std::mutex> lk(sh.mu);
    n += sh.by_uid.size();
  }
  return n;
}

}  // namespace pandora
