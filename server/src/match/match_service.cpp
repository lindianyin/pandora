#include "match/match_service.hpp"

#include <algorithm>
#include <vector>

#include "common/errors.hpp"
#include "common/log.hpp"
#include "common/proto_wire.hpp"

namespace pandora {

MatchService::MatchService(SessionHub& hub, MemoryStore& store, LobbyService& lobby, RoomManager& rooms,
                           GameConfig cfg)
    : hub_(hub), store_(store), lobby_(lobby), rooms_(rooms), cfg_(std::move(cfg)) {}

void MatchService::RemoveFromQueue(int64_t uid) {
  auto it = uid_in_queue_.find(uid);
  if (it == uid_in_queue_.end()) return;
  const int32_t tid = it->second;
  auto& q = queues_[tid];
  q.erase(std::remove_if(q.begin(), q.end(), [&](const Entry& e) { return e.uid == uid; }), q.end());
  uid_in_queue_.erase(it);
}

void MatchService::TryMatch(int32_t template_id) {
  auto tmpl = lobby_.FindTemplate(template_id);
  if (!tmpl || !tmpl->enabled) return;
  int need = tmpl->players;
  if (need <= 0) need = (tmpl->game_id == 2) ? 4 : 3;
  if (need < 2) need = 2;
  if (need > 4) need = 4;

  auto& q = queues_[template_id];
  while (static_cast<int>(q.size()) >= need) {
    std::vector<int64_t> uids;
    uids.reserve(static_cast<size_t>(need));
    for (int i = 0; i < need; ++i) uids.push_back(q[static_cast<size_t>(i)].uid);
    q.erase(q.begin(), q.begin() + need);
    for (int64_t u : uids) uid_in_queue_.erase(u);

    const int64_t room_id = rooms_.CreateRoom(template_id, uids, tmpl->game_id);
    for (int64_t u : uids) {
      hub_.Send(u, MsgId::kS2C_MatchStatus, proto_wire::EncodeS2C_MatchStatus(1, room_id, "matched"));
    }
    rooms_.PushRoomState(room_id);
    PLOG_INFO("matched room=" << room_id << " game_id=" << tmpl->game_id << " players=" << need);
  }
}

void MatchService::QuickMatch(int64_t uid, int32_t template_id) {
  std::lock_guard<std::mutex> lk(mu_);
  if (rooms_.RoomOf(uid)) {
    hub_.Send(uid, MsgId::kS2C_Error,
              proto_wire::EncodeS2C_Error(static_cast<int>(Err::kBadParam), "already in room",
                                          MsgId::kC2S_QuickMatch));
    return;
  }
  if (uid_in_queue_.count(uid)) {
    hub_.Send(uid, MsgId::kS2C_MatchStatus, proto_wire::EncodeS2C_MatchStatus(0, 0, "already matching"));
    return;
  }
  const auto tmpl = lobby_.FindTemplate(template_id);
  if (!tmpl || !tmpl->enabled) {
    hub_.Send(uid, MsgId::kS2C_Error,
              proto_wire::EncodeS2C_Error(static_cast<int>(Err::kNotFound), "template not found",
                                          MsgId::kC2S_QuickMatch));
    return;
  }
  auto p = store_.GetPlayer(uid);
  if (!p) {
    hub_.Send(uid, MsgId::kS2C_Error,
              proto_wire::EncodeS2C_Error(static_cast<int>(Err::kNotFound), "player not found",
                                          MsgId::kC2S_QuickMatch));
    return;
  }
  if (p->gold < tmpl->min_gold || (tmpl->max_gold > 0 && p->gold > tmpl->max_gold)) {
    hub_.Send(uid, MsgId::kS2C_Error,
              proto_wire::EncodeS2C_Error(static_cast<int>(Err::kForbidden), "gold out of range",
                                          MsgId::kC2S_QuickMatch));
    return;
  }

  Entry e;
  e.uid = uid;
  e.template_id = template_id;
  e.enqueue_at = std::chrono::steady_clock::now();
  queues_[template_id].push_back(e);
  uid_in_queue_[uid] = template_id;
  hub_.Send(uid, MsgId::kS2C_MatchStatus, proto_wire::EncodeS2C_MatchStatus(0, 0, "matching"));
  TryMatch(template_id);
}

void MatchService::CancelMatch(int64_t uid) {
  std::lock_guard<std::mutex> lk(mu_);
  if (!uid_in_queue_.count(uid)) return;
  RemoveFromQueue(uid);
  hub_.Send(uid, MsgId::kS2C_MatchStatus, proto_wire::EncodeS2C_MatchStatus(3, 0, "cancelled"));
}

void MatchService::OnDisconnect(int64_t uid) {
  std::lock_guard<std::mutex> lk(mu_);
  RemoveFromQueue(uid);
}

void MatchService::Tick() {
  std::lock_guard<std::mutex> lk(mu_);
  const auto now = std::chrono::steady_clock::now();
  std::vector<int64_t> timed_out;
  for (auto& kv : queues_) {
    auto& q = kv.second;
    for (auto it = q.begin(); it != q.end();) {
      if (now - it->enqueue_at >= std::chrono::seconds(cfg_.match_timeout_s)) {
        timed_out.push_back(it->uid);
        uid_in_queue_.erase(it->uid);
        it = q.erase(it);
      } else {
        ++it;
      }
    }
  }
  for (int64_t uid : timed_out) {
    hub_.Send(uid, MsgId::kS2C_MatchStatus, proto_wire::EncodeS2C_MatchStatus(2, 0, "timeout"));
  }
}

}  // namespace pandora
