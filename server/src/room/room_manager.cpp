#include "room/room_manager.hpp"

#include <algorithm>

#include "common/errors.hpp"
#include "common/log.hpp"
#include "common/proto_wire.hpp"
#include "common/send_error.hpp"
#include "game/game_ids.hpp"
#include "game/game_registry.hpp"

namespace pandora {

namespace {

int32_t NormalizeGameId(int32_t game_id) {
  if (game_id == 1) return GameId::kDdz;
  if (game_id == 2) return GameId::kHzmj;
  if (game_id == 3) return GameId::kPhz;
  if (game_id <= 0) return GameId::kDdz;
  return game_id;
}

}  // namespace

RoomManager::RoomManager(SessionHub& hub, MemoryStore& store, WalletService& wallet, GameConfig cfg)
    : hub_(hub), store_(store), wallet_(wallet), cfg_(std::move(cfg)) {}

std::optional<int64_t> RoomManager::RoomIdOfUnlocked(int64_t uid) const {
  auto it = uid_to_room_.find(uid);
  if (it == uid_to_room_.end()) return std::nullopt;
  return it->second;
}

int64_t RoomManager::CreateRoom(int32_t template_id, const std::array<int64_t, 3>& uids) {
  return CreateRoom(template_id, std::vector<int64_t>{uids[0], uids[1], uids[2]}, GameId::kDdz);
}

int64_t RoomManager::CreateRoom(int32_t template_id, const std::vector<int64_t>& uids, int32_t game_id) {
  const int32_t gid = NormalizeGameId(game_id);
  const int default_seats = GameRegistry::Instance().DefaultSeats(gid);
  const int seat_count = std::min(4, std::max(1, static_cast<int>(uids.size())));
  if (seat_count < default_seats) {
    PLOG_WARN("create room seats=" << seat_count << " < expected=" << default_seats << " game_id=" << gid);
  }
  std::array<std::string, 4> nicks{};
  std::array<bool, 4> online{};
  for (int i = 0; i < seat_count; ++i) {
    auto p = store_.GetPlayer(uids[static_cast<size_t>(i)]);
    nicks[static_cast<size_t>(i)] = p ? p->nickname : ("P" + std::to_string(uids[static_cast<size_t>(i)]));
    online[static_cast<size_t>(i)] = hub_.IsOnline(uids[static_cast<size_t>(i)]);
  }

  int64_t rid = 0;
  {
    std::lock_guard<std::mutex> ilk(index_mu_);
    rid = next_room_id_++;
    for (int i = 0; i < seat_count; ++i) uid_to_room_[uids[static_cast<size_t>(i)]] = rid;
    std::lock_guard<std::recursive_mutex> slk(Shard(rid).mu);
    Room room;
    room.room_id = rid;
    room.template_id = template_id;
    room.game_id = gid;
    room.seat_count = seat_count;
    room.phase = "WaitReady";
    for (int i = 0; i < seat_count; ++i) {
      room.seats[static_cast<size_t>(i)].uid = uids[static_cast<size_t>(i)];
      room.seats[static_cast<size_t>(i)].nickname = nicks[static_cast<size_t>(i)];
      room.seats[static_cast<size_t>(i)].ready = false;
      room.seats[static_cast<size_t>(i)].online = online[static_cast<size_t>(i)];
    }
    Shard(rid).rooms[rid] = std::move(room);
  }
  PLOG_INFO("room created id=" << rid << " game_id=" << gid << " seats=" << seat_count);
  return rid;
}

RoomManager::Room* RoomManager::FindRoomUnlocked(int64_t room_id) {
  auto& sh = Shard(room_id);
  auto it = sh.rooms.find(room_id);
  if (it == sh.rooms.end()) return nullptr;
  return &it->second;
}

std::optional<int64_t> RoomManager::RoomOf(int64_t uid) {
  std::lock_guard<std::mutex> lk(index_mu_);
  return RoomIdOfUnlocked(uid);
}

void RoomManager::SendToUid(int64_t uid, uint32_t msg_id, const std::vector<uint8_t>& body) {
  hub_.Send(uid, msg_id, body);
}

int RoomManager::SeatOfUid(const Room& room, int64_t uid) const {
  for (int i = 0; i < room.seat_count; ++i) {
    if (room.seats[static_cast<size_t>(i)].uid == uid) return i;
  }
  return -1;
}

bool RoomManager::GameInProgress(const Room& room) const {
  return room.logic && room.logic->InProgress();
}

void RoomManager::PushRoomState(int64_t room_id) {
  std::lock_guard<std::recursive_mutex> lk(Shard(room_id).mu);
  Room* r = FindRoomUnlocked(room_id);
  if (!r) return;
  std::vector<proto_wire::RoomSeat> seats;
  for (int i = 0; i < r->seat_count; ++i) {
    proto_wire::RoomSeat s;
    s.seat_id = i;
    s.uid = r->seats[static_cast<size_t>(i)].uid;
    s.nickname = r->seats[static_cast<size_t>(i)].nickname;
    s.ready = r->seats[static_cast<size_t>(i)].ready;
    s.online = r->seats[static_cast<size_t>(i)].online;
    s.trusteeship = r->seats[static_cast<size_t>(i)].trusteeship;
    seats.push_back(s);
  }
  const auto body = proto_wire::EncodeS2C_RoomState(r->room_id, r->template_id, seats, r->phase);
  for (int i = 0; i < r->seat_count; ++i) {
    const auto& s = r->seats[static_cast<size_t>(i)];
    if (s.uid) hub_.Send(s.uid, MsgId::kS2C_RoomState, body);
  }
}

RoomCtx RoomManager::MakeCtx(Room& room) {
  RoomCtx ctx;
  ctx.room_id = room.room_id;
  ctx.template_id = room.template_id;
  ctx.game_id = room.game_id;
  ctx.seat_count = room.seat_count;
  ctx.cfg = cfg_;
  ctx.wallet = &wallet_;
  ctx.admin = admin_;
  ctx.activity = activity_;
  ctx.social = social_;
  ctx.store = &store_;
  ctx.carry_banker = &room.carry_banker;
  ctx.carry_lian = &room.carry_lian;
  const int64_t rid = room.room_id;

  ctx.uid_of = [this, rid](int seat) -> int64_t {
    std::lock_guard<std::recursive_mutex> lk(Shard(rid).mu);
    Room* r = FindRoomUnlocked(rid);
    if (!r || seat < 0 || seat >= r->seat_count) return 0;
    return r->seats[static_cast<size_t>(seat)].uid;
  };
  ctx.seat_of = [this, rid](int64_t uid) -> int {
    std::lock_guard<std::recursive_mutex> lk(Shard(rid).mu);
    Room* r = FindRoomUnlocked(rid);
    if (!r) return -1;
    return SeatOfUid(*r, uid);
  };
  ctx.set_phase = [this, rid](std::string phase) {
    std::lock_guard<std::recursive_mutex> lk(Shard(rid).mu);
    Room* r = FindRoomUnlocked(rid);
    if (r) r->phase = std::move(phase);
  };
  ctx.is_trusteeship = [this, rid](int seat) -> bool { return IsTrusteeship(rid, seat); };
  ctx.set_trusteeship = [this, rid](int seat, bool on) { SetTrusteeship(rid, seat, on); };
  ctx.send = [this](int64_t uid, uint32_t msg_id, const std::vector<uint8_t>& body) { hub_.Send(uid, msg_id, body); };
  ctx.push_room_state = [this, rid]() { PushRoomState(rid); };
  ctx.on_round_finished = [this, rid]() {
    std::lock_guard<std::recursive_mutex> lk(Shard(rid).mu);
    Room* r = FindRoomUnlocked(rid);
    if (!r) return;
    r->logic.reset();
    r->phase = "WaitReady";
    PushRoomState(rid);
  };
  return ctx;
}

void RoomManager::MaybeStart(Room& room) {
  for (int i = 0; i < room.seat_count; ++i) {
    const auto& s = room.seats[static_cast<size_t>(i)];
    if (!s.uid || !s.ready) return;
  }
  for (int i = 0; i < room.seat_count; ++i) room.seats[static_cast<size_t>(i)].ready = false;

  room.game_ctx = MakeCtx(room);
  room.logic = GameRegistry::Instance().Create(room.game_id, room.game_ctx);
  if (!room.logic) {
    PLOG_WARN("no game logic for game_id=" << room.game_id << " room=" << room.room_id);
    return;
  }
  room.logic->Start();
}

bool RoomManager::SetReady(int64_t uid, bool ready) {
  int64_t rid = 0;
  {
    std::lock_guard<std::mutex> ilk(index_mu_);
    auto rid_opt = RoomIdOfUnlocked(uid);
    if (!rid_opt) {
      SendError(hub_, uid, Err::kNotInRoom, MsgId::kC2S_Ready);
      return false;
    }
    rid = *rid_opt;
  }
  std::lock_guard<std::recursive_mutex> slk(Shard(rid).mu);
  Room* r = FindRoomUnlocked(rid);
  if (!r) return false;
  if (GameInProgress(*r)) {
    SendError(hub_, uid, Err::kBadParam, MsgId::kC2S_Ready);
    return false;
  }
  for (int i = 0; i < r->seat_count; ++i) {
    if (r->seats[static_cast<size_t>(i)].uid == uid) {
      r->seats[static_cast<size_t>(i)].ready = ready;
      break;
    }
  }
  PushRoomState(r->room_id);
  MaybeStart(*r);
  return true;
}

bool RoomManager::Leave(int64_t uid) {
  int64_t rid = 0;
  {
    std::lock_guard<std::mutex> ilk(index_mu_);
    auto rid_opt = RoomIdOfUnlocked(uid);
    if (!rid_opt) return false;
    rid = *rid_opt;
  }
  std::lock_guard<std::mutex> ilk(index_mu_);
  std::lock_guard<std::recursive_mutex> slk(Shard(rid).mu);
  Room* r = FindRoomUnlocked(rid);
  if (!r) return false;
  if (GameInProgress(*r)) {
    for (int i = 0; i < r->seat_count; ++i) {
      if (r->seats[static_cast<size_t>(i)].uid == uid) r->seats[static_cast<size_t>(i)].online = false;
    }
    PushRoomState(rid);
    return true;
  }
  for (int i = 0; i < r->seat_count; ++i) {
    if (r->seats[static_cast<size_t>(i)].uid == uid) {
      r->seats[static_cast<size_t>(i)].uid = 0;
      r->seats[static_cast<size_t>(i)].ready = false;
      r->seats[static_cast<size_t>(i)].nickname.clear();
    }
  }
  uid_to_room_.erase(uid);
  bool empty = true;
  for (int i = 0; i < r->seat_count; ++i)
    if (r->seats[static_cast<size_t>(i)].uid) empty = false;
  if (empty) {
    Shard(rid).rooms.erase(rid);
  } else {
    PushRoomState(rid);
  }
  return true;
}

void RoomManager::OnDisconnect(int64_t uid) {
  int64_t rid = 0;
  {
    std::lock_guard<std::mutex> ilk(index_mu_);
    auto rid_opt = RoomIdOfUnlocked(uid);
    if (!rid_opt) return;
    rid = *rid_opt;
  }
  std::lock_guard<std::recursive_mutex> slk(Shard(rid).mu);
  Room* r = FindRoomUnlocked(rid);
  if (!r) return;
  for (int i = 0; i < r->seat_count; ++i) {
    if (r->seats[static_cast<size_t>(i)].uid == uid) {
      r->seats[static_cast<size_t>(i)].online = false;
      if (GameInProgress(*r)) r->seats[static_cast<size_t>(i)].trusteeship = true;
    }
  }
  if (r->logic && r->logic->InProgress()) r->logic->OnDisconnect(uid);
  PushRoomState(r->room_id);
}

void RoomManager::OnReconnect(int64_t uid) {
  int64_t rid = 0;
  {
    std::lock_guard<std::mutex> ilk(index_mu_);
    auto rid_opt = RoomIdOfUnlocked(uid);
    if (!rid_opt) return;
    rid = *rid_opt;
  }
  std::lock_guard<std::recursive_mutex> slk(Shard(rid).mu);
  Room* r = FindRoomUnlocked(rid);
  if (!r) return;
  for (int i = 0; i < r->seat_count; ++i) {
    if (r->seats[static_cast<size_t>(i)].uid == uid) {
      r->seats[static_cast<size_t>(i)].online = true;
      r->seats[static_cast<size_t>(i)].trusteeship = false;
    }
  }
  PushRoomState(r->room_id);
  if (r->logic && r->logic->InProgress()) r->logic->OnReconnect(uid);
}

void RoomManager::HandleGameMsg(int64_t uid, uint32_t msg_id, const uint8_t* body, size_t len) {
  int64_t rid = 0;
  {
    std::lock_guard<std::mutex> ilk(index_mu_);
    auto rid_opt = RoomIdOfUnlocked(uid);
    if (!rid_opt) {
      SendError(hub_, uid, Err::kNotInRoom, msg_id);
      return;
    }
    rid = *rid_opt;
  }
  std::lock_guard<std::recursive_mutex> slk(Shard(rid).mu);
  Room* r = FindRoomUnlocked(rid);
  if (!r || !r->logic || !r->logic->InProgress()) {
    SendError(hub_, uid, Err::kGameNotRunning, msg_id);
    return;
  }
  ClearTrusteeshipInRoom(*r, uid);
  if (!r->logic->Handle(uid, msg_id, body, len)) {
    SendError(hub_, uid, Err::kUnsupportedMsg, msg_id);
  }
}

void RoomManager::ClearTrusteeshipUid(int64_t uid) {
  std::lock_guard<std::mutex> ilk(index_mu_);
  auto rid_opt = RoomIdOfUnlocked(uid);
  if (!rid_opt) return;
  std::lock_guard<std::recursive_mutex> slk(Shard(*rid_opt).mu);
  Room* r = FindRoomUnlocked(*rid_opt);
  if (!r) return;
  ClearTrusteeshipInRoom(*r, uid);
}

void RoomManager::ClearTrusteeshipInRoom(Room& r, int64_t uid) {
  for (int i = 0; i < r.seat_count; ++i) {
    if (r.seats[static_cast<size_t>(i)].uid == uid) r.seats[static_cast<size_t>(i)].trusteeship = false;
  }
}

bool RoomManager::IsTrusteeship(int64_t room_id, int seat) {
  std::lock_guard<std::recursive_mutex> lk(Shard(room_id).mu);
  Room* r = FindRoomUnlocked(room_id);
  if (!r || seat < 0 || seat >= r->seat_count) return false;
  return r->seats[static_cast<size_t>(seat)].trusteeship;
}

void RoomManager::SetTrusteeship(int64_t room_id, int seat, bool on) {
  std::lock_guard<std::recursive_mutex> lk(Shard(room_id).mu);
  Room* r = FindRoomUnlocked(room_id);
  if (!r || seat < 0 || seat >= r->seat_count) return;
  r->seats[static_cast<size_t>(seat)].trusteeship = on;
}

void RoomManager::Tick() {
  const auto now = std::chrono::steady_clock::now();
  for (auto& sh : shards_) {
    std::lock_guard<std::recursive_mutex> lk(sh.mu);
    for (auto& kv : sh.rooms) {
      Room& r = kv.second;
      if (r.logic && r.logic->InProgress()) r.logic->Tick(now);
    }
  }
}

}  // namespace pandora
