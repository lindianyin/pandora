#include "room/room_manager.hpp"

#include <algorithm>
#include <random>
#include <sstream>

#include <nlohmann/json.hpp>

#include "admin/admin_service.hpp"
#include "activity/activity_service.hpp"
#include "common/errors.hpp"
#include "common/log.hpp"
#include "common/proto_wire.hpp"
#include "social/social_service.hpp"
#include "wallet/wallet_service.hpp"

namespace pandora {

namespace {

int64_t NowMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::string JoinIds(const std::vector<int32_t>& ids) {
  std::ostringstream oss;
  for (size_t i = 0; i < ids.size(); ++i) {
    if (i) oss << ',';
    oss << ids[i];
  }
  return oss.str();
}

int SecondsLeft(std::chrono::steady_clock::time_point deadline) {
  using namespace std::chrono;
  const auto now = steady_clock::now();
  if (now >= deadline) return 0;
  const auto ms = duration_cast<milliseconds>(deadline - now).count();
  if (ms <= 0) return 0;
  return static_cast<int>((ms + 999) / 1000);
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
  return CreateRoom(template_id, std::vector<int64_t>{uids[0], uids[1], uids[2]}, 1);
}

int64_t RoomManager::CreateRoom(int32_t template_id, const std::vector<int64_t>& uids, int32_t game_id) {
  const int seat_count = std::min(4, std::max(1, static_cast<int>(uids.size())));
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
    room.game_id = game_id > 0 ? game_id : 1;
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
  PLOG_INFO("room created id=" << rid << " game_id=" << game_id << " seats=" << seat_count);
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
  if (room.game && !room.game->Finished()) return true;
  if (room.hzmj && !room.hzmj_done) return true;
  return false;
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

void RoomManager::MaybeStart(Room& room) {
  for (int i = 0; i < room.seat_count; ++i) {
    const auto& s = room.seats[static_cast<size_t>(i)];
    if (!s.uid || !s.ready) return;
  }
  for (int i = 0; i < room.seat_count; ++i) room.seats[static_cast<size_t>(i)].ready = false;

  if (room.game_id == 2) {
    StartHzmj(room);
    return;
  }

  std::array<int64_t, 3> uids{room.seats[0].uid, room.seats[1].uid, room.seats[2].uid};
  room.game = std::make_unique<DdzClassicSimple>(*this, room.room_id, uids, cfg_);
  room.phase = "Deal";
  PushRoomState(room.room_id);
  room.game->Start();
  room.phase = room.game->Phase();
}

namespace {

std::vector<int32_t> HandToList(const hzmj::HandCount& h) {
  std::vector<int32_t> out;
  for (int t = 0; t < hzmj::kTileKinds; ++t) {
    for (int n = 0; n < h[static_cast<size_t>(t)]; ++n) out.push_back(t);
  }
  return out;
}

}  // namespace

void RoomManager::StartHzmj(Room& room) {
  std::array<int64_t, 4> uids{};
  for (int i = 0; i < room.seat_count; ++i) uids[static_cast<size_t>(i)] = room.seats[static_cast<size_t>(i)].uid;
  hzmj::HzmjConfig hcfg;
  hcfg.base_score = cfg_.base_score;
  if (cfg_.hzmj_debug_deal) {
    hcfg.start_as_sanlao = true;
    room.hzmj_banker = 1;
  }
  room.hzmj = std::make_unique<hzmj::HzmjTable>(hcfg, uids, room.hzmj_banker, room.hzmj_lian);
  room.hzmj_round_id = NowMs();
  room.hzmj_done = false;
  room.phase = "Deal";
  const int64_t rid = room.room_id;
  room.hzmj->SetSink([this, rid](const hzmj::OutEvent& ev) {
    Room* r = FindRoomUnlocked(rid);
    if (!r || !r->hzmj) return;
    OnHzmjEvent(*r, ev);
  });
  if (cfg_.hzmj_debug_deal) {
    room.hzmj->ApplyDebugDianpaoDeal();
    room.hzmj_banker = room.hzmj->banker_seat();
    PLOG_INFO("hzmj debug deal: seat0 ting Dong, banker=1 discards Dong first, N=8");
  }
  PushRoomState(room.room_id);
  room.hzmj->Start();
  if (room.hzmj->phase() == hzmj::Phase::kPlay) room.phase = "Play";
  ArmHzmjDeadline(room);
}

void RoomManager::ArmHzmjDeadline(Room& room) {
  room.hzmj_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(cfg_.play_timeout_s);
}

void RoomManager::OnHzmjEvent(Room& room, const hzmj::OutEvent& ev) {
  if (!room.hzmj) return;
  auto* t = room.hzmj.get();
  const int timeout_s = cfg_.play_timeout_s;
  auto uid_of = [&](int seat) -> int64_t {
    if (seat < 0 || seat >= room.seat_count) return 0;
    return room.seats[static_cast<size_t>(seat)].uid;
  };

  if (ev.type == "GameStart") {
    const int N = t->CurrentN();
    std::ostringstream hands;
    hands << "banker=" << t->banker_seat() << " N=" << N << " lian=" << t->lian_zhuang()
          << " wall=" << t->wall_remain();
    for (int i = 0; i < room.seat_count; ++i) {
      hands << " hand" << i << "=" << JoinIds(HandToList(t->seat(i).hand));
    }
    LogRound(room.hzmj_round_id, "server", "hzmj", room.room_id, uid_of(t->banker_seat()), t->banker_seat(), "start",
             hands.str());
    std::vector<int32_t> caishen{hzmj::kBai};
    for (int i = 0; i < room.seat_count; ++i) {
      const int64_t uid = room.seats[static_cast<size_t>(i)].uid;
      if (!uid) continue;
      auto hand = HandToList(t->seat(i).hand);
      auto body = proto_wire::EncodeS2C_HzmjGameStart(room.hzmj_round_id, room.room_id, room.template_id,
                                                     t->banker_seat(), t->lian_zhuang(), N, caishen, hand,
                                                     t->wall_remain(), i, cfg_.base_score);
      hub_.Send(uid, MsgId::kS2C_HzmjGameStart, body);
    }
    room.phase = "Play";
    ArmHzmjDeadline(room);
    return;
  }

  if (ev.type == "Turn" || ev.type == "ClaimWindow" || ev.type == "Piao") {
    std::string sub = ev.detail.empty() ? (ev.type == "ClaimWindow" ? "claim" : "discard") : ev.detail;
    if (ev.type == "ClaimWindow") sub = "claim";
    if (ev.type == "Piao") sub = "piao";
    if (ev.type == "Turn" && sub.empty()) sub = "discard";
    const int piao = t->piao_active() ? t->piao_seat() : -1;
    {
      std::ostringstream detail;
      detail << "sub=" << sub << " wall=" << t->wall_remain() << " piao=" << piao << " timeout=" << timeout_s;
      LogRound(room.hzmj_round_id, "server", "hzmj", room.room_id, uid_of(ev.seat), ev.seat,
               ev.type == "Turn" ? "turn" : sub, detail.str());
    }
    ArmHzmjDeadline(room);
    for (int i = 0; i < room.seat_count; ++i) {
      const int64_t uid = room.seats[static_cast<size_t>(i)].uid;
      if (!uid) continue;
      // 鸣牌窗：只推给有牌权的座位，避免无牌者误点「胡」
      if (ev.type == "ClaimWindow" && !t->SeatNeedsClaimInput(i)) continue;
      std::vector<int32_t> sync_hand;
      if (sub == "discard" && i == ev.seat) sync_hand = HandToList(t->seat(i).hand);
      const bool can_zimo = (sub == "discard" || sub == "piao") && t->can_zimo();
      auto body =
          proto_wire::EncodeS2C_HzmjTurn(ev.seat, sub, timeout_s, t->wall_remain(), piao, sync_hand, can_zimo);
      hub_.Send(uid, MsgId::kS2C_HzmjTurn, body);
    }
    return;
  }

  if (ev.type == "Draw") {
    {
      std::ostringstream detail;
      detail << "tile=" << static_cast<int>(ev.tile) << " wall=" << t->wall_remain();
      LogRound(room.hzmj_round_id, "server", "hzmj", room.room_id, uid_of(ev.seat), ev.seat, "draw", detail.str());
    }
    for (int i = 0; i < room.seat_count; ++i) {
      const int64_t uid = room.seats[static_cast<size_t>(i)].uid;
      if (!uid) continue;
      const int32_t tile = (i == ev.seat) ? static_cast<int32_t>(ev.tile) : -1;
      hub_.Send(uid, MsgId::kS2C_HzmjDraw, proto_wire::EncodeS2C_HzmjDraw(ev.seat, tile));
    }
    return;
  }

  if (ev.type == "Discard") {
    {
      std::ostringstream detail;
      detail << "tile=" << static_cast<int>(ev.tile);
      LogRound(room.hzmj_round_id, "server", "hzmj", room.room_id, uid_of(ev.seat), ev.seat, "discard", detail.str());
    }
    auto body = proto_wire::EncodeS2C_HzmjDiscardBroadcast(ev.seat, ev.tile);
    for (int i = 0; i < room.seat_count; ++i) {
      if (room.seats[static_cast<size_t>(i)].uid)
        hub_.Send(room.seats[static_cast<size_t>(i)].uid, MsgId::kS2C_HzmjDiscardBroadcast, body);
    }
    return;
  }

  if (ev.type == "Peng" || ev.type == "Chi" || ev.type == "MingGang" || ev.type == "AnGang" || ev.type == "BuGang") {
    int act = 2;
    int meld_kind = 2;
    if (ev.type == "Chi") {
      act = 1;
      meld_kind = 1;
    } else if (ev.type == "Peng") {
      act = 2;
      meld_kind = 2;
    } else if (ev.type == "MingGang") {
      act = 3;
      meld_kind = 3;
    } else if (ev.type == "AnGang") {
      act = 3;
      meld_kind = 4;
    } else if (ev.type == "BuGang") {
      act = 3;
      meld_kind = 5;
    }
    std::vector<int32_t> tiles;
    tiles.reserve(ev.tiles.size());
    for (auto tile : ev.tiles) tiles.push_back(static_cast<int32_t>(tile));
    {
      std::ostringstream detail;
      detail << "tile=" << static_cast<int>(ev.tile) << " from=" << ev.from_seat << " tiles=" << JoinIds(tiles);
      LogRound(room.hzmj_round_id, "server", "hzmj", room.room_id, uid_of(ev.seat), ev.seat, ev.type, detail.str());
    }
    auto body =
        proto_wire::EncodeS2C_HzmjActionBroadcast(ev.seat, act, ev.tile, tiles, ev.from_seat, meld_kind);
    for (int i = 0; i < room.seat_count; ++i) {
      if (room.seats[static_cast<size_t>(i)].uid)
        hub_.Send(room.seats[static_cast<size_t>(i)].uid, MsgId::kS2C_HzmjActionBroadcast, body);
    }
    ArmHzmjDeadline(room);
    return;
  }

  if (ev.type == "GangScore") {
    {
      std::ostringstream detail;
      detail << "kind=" << ev.detail;
      for (int i = 0; i < room.seat_count; ++i) detail << " d" << i << "=" << ev.deltas[static_cast<size_t>(i)];
      LogRound(room.hzmj_round_id, "server", "hzmj", room.room_id, uid_of(ev.seat), ev.seat, "gang_score", detail.str());
    }
    for (int i = 0; i < room.seat_count; ++i) {
      const int64_t uid = room.seats[static_cast<size_t>(i)].uid;
      const int64_t d = ev.deltas[static_cast<size_t>(i)];
      if (!uid || d == 0) continue;
      const std::string key = "hzmj:gang:" + std::to_string(room.hzmj_round_id) + ":" + ev.detail + ":" +
                              std::to_string(uid);
      wallet_.Adjust(uid, Currency::kGold, d, "hzmj_gang", key);
    }
    return;
  }

  if (ev.type == "Settle") {
    ApplyHzmjSettle(room);
    return;
  }

  if (ev.type == "LiuJu") {
    {
      std::ostringstream detail;
      detail << "lian=" << t->lian_zhuang();
      LogRound(room.hzmj_round_id, "server", "hzmj", room.room_id, 0, -1, "liuju", detail.str());
    }
    room.hzmj_banker = t->banker_seat();
    room.hzmj_lian = t->lian_zhuang();
    auto body = proto_wire::EncodeS2C_HzmjLiuJu(t->lian_zhuang());
    for (int i = 0; i < room.seat_count; ++i) {
      if (room.seats[static_cast<size_t>(i)].uid)
        hub_.Send(room.seats[static_cast<size_t>(i)].uid, MsgId::kS2C_HzmjLiuJu, body);
    }
    FinishHzmjRound(room);
  }
}

void RoomManager::ApplyHzmjSettle(Room& room) {
  if (!room.hzmj) return;
  auto* t = room.hzmj.get();
  auto plan = t->last_settle();
  std::array<int64_t, 4> deltas = plan.deltas;

  // rake on winners
  for (int i = 0; i < room.seat_count; ++i) {
    if (deltas[static_cast<size_t>(i)] > 0) {
      const int64_t rake = deltas[static_cast<size_t>(i)] * cfg_.rake_bp / 10000;
      deltas[static_cast<size_t>(i)] -= rake;
    }
  }

  int M = t->last_hu_M();
  int N = t->last_hu_N();
  if (M < 1) M = 1;
  if (N < 1) N = 2;

  std::vector<proto_wire::SettleEntry> entries;
  nlohmann::json players = nlohmann::json::array();
  for (int i = 0; i < room.seat_count; ++i) {
    const int64_t uid = room.seats[static_cast<size_t>(i)].uid;
    const int64_t d = deltas[static_cast<size_t>(i)];
    if (uid) {
      const std::string key = hzmj::IdemSettleKey(room.hzmj_round_id, uid);
      wallet_.Adjust(uid, Currency::kGold, d, "game_settle", key);
    }
    proto_wire::SettleEntry e;
    e.uid = uid;
    e.seat_id = i;
    e.delta_gold = d;
    entries.push_back(e);
    players.push_back({{"uid", uid}, {"seat_id", i}, {"delta", d}});
  }

  const int winner = [&]() {
    int w = 0;
    for (int i = 1; i < room.seat_count; ++i) {
      if (deltas[static_cast<size_t>(i)] > deltas[static_cast<size_t>(w)]) w = i;
    }
    return w;
  }();
  // After FinishHu, banker/lian already updated — store for next round
  room.hzmj_banker = t->banker_seat();
  room.hzmj_lian = t->lian_zhuang();

  const int32_t hu_tile = t->last_hu_tile() == hzmj::kTileInvalid ? -1 : static_cast<int32_t>(t->last_hu_tile());
  {
    std::ostringstream detail;
    detail << "winner=" << winner << " hu=" << hu_tile << " zimo=" << (t->last_hu_zimo() ? 1 : 0)
           << " shooter=" << t->last_shooter_seat() << " M=" << M << " N=" << N
           << " contractor=" << plan.contractor_seat;
    for (int i = 0; i < room.seat_count; ++i) detail << " d" << i << "=" << deltas[static_cast<size_t>(i)];
    const int64_t winner_uid =
        (winner >= 0 && winner < room.seat_count) ? room.seats[static_cast<size_t>(winner)].uid : 0;
    LogRound(room.hzmj_round_id, "server", "hzmj", room.room_id, winner_uid, winner, "settle", detail.str());
  }
  auto body = proto_wire::EncodeS2C_HzmjSettle(room.hzmj_round_id, winner, hu_tile, t->last_hu_zimo(),
                                              t->last_shooter_seat(), M, N, plan.contractor_seat, cfg_.base_score,
                                              entries);
  for (int i = 0; i < room.seat_count; ++i) {
    if (room.seats[static_cast<size_t>(i)].uid)
      hub_.Send(room.seats[static_cast<size_t>(i)].uid, MsgId::kS2C_HzmjSettle, body);
  }

  const std::string players_json = players.dump();
  if (admin_) {
    admin_->RecordRound(room.hzmj_round_id, room.room_id, room.template_id, players_json, cfg_.base_score, M * N, 2);
  }
  if (social_) {
    try {
      social_->OnRoundSettled(room.hzmj_round_id, room.template_id, players_json, cfg_.base_score, M * N);
    } catch (...) {
    }
  }
  if (activity_) {
    for (int i = 0; i < room.seat_count; ++i) {
      const int64_t uid = room.seats[static_cast<size_t>(i)].uid;
      if (!uid) continue;
      try {
        activity_->OnGameSettled(uid, room.template_id);
      } catch (...) {
      }
    }
  }
  FinishHzmjRound(room);
}

void RoomManager::FinishHzmjRound(Room& room) {
  room.hzmj_done = true;
  room.hzmj.reset();
  room.phase = "WaitReady";
  PushRoomState(room.room_id);
}

void RoomManager::TickHzmj(Room& room, std::chrono::steady_clock::time_point now) {
  if (!room.hzmj || room.hzmj_done) return;
  auto* t = room.hzmj.get();
  if (t->phase() != hzmj::Phase::kPlay) return;

  auto maybe_timeout = [&](int seat) {
    if (seat < 0 || seat >= room.seat_count) return;
    // Disconnect marks trusteeship but must not skip the remaining countdown.
    // Auto-play only when the original deadline is due.
    if (now >= room.hzmj_deadline) {
      const char* sub = t->sub() == hzmj::PlaySub::kClaimWindow ? "claim" : "discard";
      LogRound(room.hzmj_round_id, "server", "hzmj", room.room_id, room.seats[static_cast<size_t>(seat)].uid, seat,
               "timeout", sub);
      t->OnTimeout(seat);
    }
  };

  if (t->sub() == hzmj::PlaySub::kClaimWindow) {
    for (int i = 0; i < room.seat_count; ++i) {
      if (i == t->last_discard_seat()) continue;
      maybe_timeout(i);
      if (!room.hzmj || room.hzmj_done) return;
    }
  } else {
    maybe_timeout(t->turn_seat());
  }
}

bool RoomManager::SetReady(int64_t uid, bool ready) {
  int64_t rid = 0;
  {
    std::lock_guard<std::mutex> ilk(index_mu_);
    auto rid_opt = RoomIdOfUnlocked(uid);
    if (!rid_opt) {
      hub_.Send(uid, MsgId::kS2C_Error,
                proto_wire::EncodeS2C_Error(static_cast<int>(Err::kBadParam), "not in room", MsgId::kC2S_Ready));
      return false;
    }
    rid = *rid_opt;
  }
  std::lock_guard<std::recursive_mutex> slk(Shard(rid).mu);
  Room* r = FindRoomUnlocked(rid);
  if (!r) return false;
  if (GameInProgress(*r)) {
    hub_.Send(uid, MsgId::kS2C_Error,
              proto_wire::EncodeS2C_Error(static_cast<int>(Err::kBadParam), "game in progress", MsgId::kC2S_Ready));
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
  int seat = -1;
  for (int i = 0; i < r->seat_count; ++i) {
    if (r->seats[static_cast<size_t>(i)].uid == uid) {
      r->seats[static_cast<size_t>(i)].online = false;
      if (GameInProgress(*r)) r->seats[static_cast<size_t>(i)].trusteeship = true;
      seat = i;
    }
  }
  if (r->hzmj && !r->hzmj_done && seat >= 0) {
    LogRound(r->hzmj_round_id, "server", "hzmj", r->room_id, uid, seat, "disconnect",
             r->hzmj->sub() == hzmj::PlaySub::kClaimWindow ? "claim" : "play");
  } else if (r->game && !r->game->Finished() && seat >= 0) {
    LogRound(r->game->RoundId(), "server", "ddz", r->room_id, uid, seat, "disconnect", r->game->Phase());
  }
  // 鸣牌窗内断线立刻代打过，避免整桌卡死
  if (r->hzmj && !r->hzmj_done && seat >= 0 && r->hzmj->phase() == hzmj::Phase::kPlay &&
      r->hzmj->sub() == hzmj::PlaySub::kClaimWindow && seat != r->hzmj->last_discard_seat()) {
    r->hzmj->OnTimeout(seat);
  }
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
  int seat = -1;
  for (int i = 0; i < r->seat_count; ++i) {
    if (r->seats[static_cast<size_t>(i)].uid == uid) {
      r->seats[static_cast<size_t>(i)].online = true;
      r->seats[static_cast<size_t>(i)].trusteeship = false;
      seat = i;
    }
  }
  PushRoomState(r->room_id);
  if (r->game && !r->game->Finished()) r->game->SendReconnectSnapshot(uid);
  if (r->hzmj && !r->hzmj_done && seat >= 0) {
    auto* t = r->hzmj.get();
    const int N = t->CurrentN();
    std::vector<int32_t> caishen{hzmj::kBai};
    auto hand = HandToList(t->seat(seat).hand);
    auto body = proto_wire::EncodeS2C_HzmjGameStart(r->hzmj_round_id, r->room_id, r->template_id, t->banker_seat(),
                                                    t->lian_zhuang(), N, caishen, hand, t->wall_remain(), seat,
                                                    cfg_.base_score);
    hub_.Send(uid, MsgId::kS2C_HzmjGameStart, body);
    // 重连补发各家门前副露
    for (int s = 0; s < 4; ++s) {
      for (const auto& meld : t->seat(s).melds) {
        int act = 2;
        int meld_kind = 2;
        std::vector<int32_t> tiles;
        if (meld.type == hzmj::MeldType::kChi) {
          act = 1;
          meld_kind = 1;
          tiles = {meld.chi_tiles[0], meld.chi_tiles[1], meld.chi_tiles[2]};
        } else if (meld.type == hzmj::MeldType::kPeng) {
          act = 2;
          meld_kind = 2;
          tiles = {meld.tile, meld.tile, meld.tile};
        } else if (meld.type == hzmj::MeldType::kMingGang) {
          act = 3;
          meld_kind = 3;
          tiles = {meld.tile, meld.tile, meld.tile, meld.tile};
        } else if (meld.type == hzmj::MeldType::kAnGang) {
          act = 3;
          meld_kind = 4;
          tiles = {meld.tile, meld.tile, meld.tile, meld.tile};
        } else if (meld.type == hzmj::MeldType::kBuGang) {
          act = 3;
          meld_kind = 5;
          tiles = {meld.tile, meld.tile, meld.tile, meld.tile};
        }
        hub_.Send(uid, MsgId::kS2C_HzmjActionBroadcast,
                  proto_wire::EncodeS2C_HzmjActionBroadcast(s, act, meld.tile, tiles, meld.from_seat, meld_kind));
      }
    }
    if (t->last_discard() != hzmj::kTileInvalid && t->last_discard_seat() >= 0) {
      hub_.Send(uid, MsgId::kS2C_HzmjDiscardBroadcast,
                proto_wire::EncodeS2C_HzmjDiscardBroadcast(t->last_discard_seat(), t->last_discard()));
    }
    const int piao = t->piao_active() ? t->piao_seat() : -1;
    std::string sub = "discard";
    int turn = t->turn_seat();
    if (t->sub() == hzmj::PlaySub::kClaimWindow) {
      sub = "claim";
      turn = t->last_discard_seat();
    } else if (t->sub() == hzmj::PlaySub::kPiaoLock) {
      sub = "piao";
    }
    std::vector<int32_t> sync_hand;
    if (sub == "discard" && seat == turn) sync_hand = HandToList(t->seat(seat).hand);
    const int left = SecondsLeft(r->hzmj_deadline);
    {
      std::ostringstream detail;
      detail << "sub=" << sub << " turn=" << turn << " left=" << left << " wall=" << t->wall_remain()
             << " hand=" << JoinIds(hand);
      LogRound(r->hzmj_round_id, "server", "hzmj", r->room_id, uid, seat, "reconnect", detail.str());
    }
    const bool can_zimo = (sub == "discard" || sub == "piao") && t->can_zimo();
    hub_.Send(uid, MsgId::kS2C_HzmjTurn,
              proto_wire::EncodeS2C_HzmjTurn(turn, sub, left, t->wall_remain(), piao, sync_hand, can_zimo));
  }
}

void RoomManager::ClearTrusteeshipUid(int64_t uid) {
  // Prefer ClearTrusteeshipInRoom when room is already known.
  auto rid_opt = RoomIdOfUnlocked(uid);
  if (!rid_opt) return;
  Room* r = FindRoomUnlocked(*rid_opt);
  if (!r) return;
  for (int i = 0; i < r->seat_count; ++i) {
    if (r->seats[static_cast<size_t>(i)].uid == uid) r->seats[static_cast<size_t>(i)].trusteeship = false;
  }
}

void RoomManager::ClearTrusteeshipInRoom(Room& r, int64_t uid) {
  for (int i = 0; i < r.seat_count; ++i) {
    if (r.seats[static_cast<size_t>(i)].uid == uid) r.seats[static_cast<size_t>(i)].trusteeship = false;
  }
}

bool RoomManager::IsTrusteeship(int64_t room_id, int seat) {
  // Caller typically holds shard lock (recursive).
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

void RoomManager::OnBid(int64_t uid, int score) {
  int64_t rid = 0;
  {
    std::lock_guard<std::mutex> ilk(index_mu_);
    auto rid_opt = RoomIdOfUnlocked(uid);
    if (!rid_opt) return;
    rid = *rid_opt;
  }
  std::lock_guard<std::recursive_mutex> slk(Shard(rid).mu);
  Room* r = FindRoomUnlocked(rid);
  if (!r || !r->game) return;
  ClearTrusteeshipInRoom(*r, uid);
  int seat = SeatOfUid(*r, uid);
  if (seat < 0) return;
  r->game->OnBid(seat, score);
  r->phase = r->game->Phase();
}

void RoomManager::OnPlay(int64_t uid, bool pass, const std::vector<int>& cards) {
  int64_t rid = 0;
  {
    std::lock_guard<std::mutex> ilk(index_mu_);
    auto rid_opt = RoomIdOfUnlocked(uid);
    if (!rid_opt) return;
    rid = *rid_opt;
  }
  std::lock_guard<std::recursive_mutex> slk(Shard(rid).mu);
  Room* r = FindRoomUnlocked(rid);
  if (!r || !r->game) return;
  ClearTrusteeshipInRoom(*r, uid);
  int seat = SeatOfUid(*r, uid);
  if (seat < 0) return;
  r->game->OnPlay(seat, pass, cards);
  r->phase = r->game->Phase();
  if (r->game->Finished()) {
    r->phase = "WaitReady";
    r->game.reset();
    PushRoomState(r->room_id);
  }
}

void RoomManager::OnHzmjAction(int64_t uid, int32_t action, const std::vector<int32_t>& chi_hand) {
  int64_t rid = 0;
  {
    std::lock_guard<std::mutex> ilk(index_mu_);
    auto rid_opt = RoomIdOfUnlocked(uid);
    if (!rid_opt) return;
    rid = *rid_opt;
  }
  std::lock_guard<std::recursive_mutex> slk(Shard(rid).mu);
  Room* r = FindRoomUnlocked(rid);
  if (!r || !r->hzmj || r->hzmj_done) return;
  ClearTrusteeshipInRoom(*r, uid);
  int seat = SeatOfUid(*r, uid);
  if (seat < 0) return;
  {
    std::ostringstream detail;
    detail << "action=" << action << " chi=" << JoinIds(chi_hand);
    LogRound(r->hzmj_round_id, "server", "hzmj", r->room_id, uid, seat, "cmd_action", detail.str());
  }
  hzmj::ActionKind act = hzmj::ActionKind::kPass;
  if (action >= 0 && action <= 4) act = static_cast<hzmj::ActionKind>(action);
  // 出牌阶段「胡」= 自摸确认
  if (act == hzmj::ActionKind::kHu && r->hzmj->sub() == hzmj::PlaySub::kDiscard) {
    if (!r->hzmj->OnZimoHu(seat)) {
      const char* msg = r->hzmj->can_zimo() ? "不能自摸胡：牌型未成胡" : "吃碰之后须出牌，不能自摸";
      hub_.Send(uid, MsgId::kS2C_Error,
                proto_wire::EncodeS2C_Error(static_cast<int>(Err::kBadParam), msg, MsgId::kC2S_HzmjAction));
    }
    return;
  }
  hzmj::ChiOption chi{};
  const hzmj::ChiOption* chi_ptr = nullptr;
  if (act == hzmj::ActionKind::kChi && chi_hand.size() >= 2) {
    chi.hand_tiles[0] = chi_hand[0];
    chi.hand_tiles[1] = chi_hand[1];
    chi_ptr = &chi;
  }
  if (!r->hzmj->OnAction(seat, act, chi_ptr)) {
    std::string msg = "非法鸣牌操作";
    if (act == hzmj::ActionKind::kHu) {
      msg = "不能点炮胡：需三牢且庄闲关系，牌型可胡；财神打出不可胡";
    }
    hub_.Send(uid, MsgId::kS2C_Error,
              proto_wire::EncodeS2C_Error(static_cast<int>(Err::kBadParam), msg, MsgId::kC2S_HzmjAction));
  }
}

void RoomManager::OnHzmjDiscard(int64_t uid, int32_t tile) {
  int64_t rid = 0;
  {
    std::lock_guard<std::mutex> ilk(index_mu_);
    auto rid_opt = RoomIdOfUnlocked(uid);
    if (!rid_opt) return;
    rid = *rid_opt;
  }
  std::lock_guard<std::recursive_mutex> slk(Shard(rid).mu);
  Room* r = FindRoomUnlocked(rid);
  if (!r || !r->hzmj || r->hzmj_done) return;
  ClearTrusteeshipInRoom(*r, uid);
  int seat = SeatOfUid(*r, uid);
  if (seat < 0) return;
  {
    std::ostringstream detail;
    detail << "tile=" << tile;
    LogRound(r->hzmj_round_id, "server", "hzmj", r->room_id, uid, seat, "cmd_discard", detail.str());
  }
  if (!r->hzmj->OnDiscard(seat, tile)) {
    hub_.Send(uid, MsgId::kS2C_Error,
              proto_wire::EncodeS2C_Error(static_cast<int>(Err::kBadParam), "非法出牌",
                                          MsgId::kC2S_HzmjDiscard));
  }
}

void RoomManager::OnHzmjGang(int64_t uid, int32_t kind, int32_t tile) {
  int64_t rid = 0;
  {
    std::lock_guard<std::mutex> ilk(index_mu_);
    auto rid_opt = RoomIdOfUnlocked(uid);
    if (!rid_opt) return;
    rid = *rid_opt;
  }
  std::lock_guard<std::recursive_mutex> slk(Shard(rid).mu);
  Room* r = FindRoomUnlocked(rid);
  if (!r || !r->hzmj || r->hzmj_done) return;
  ClearTrusteeshipInRoom(*r, uid);
  int seat = SeatOfUid(*r, uid);
  if (seat < 0) return;
  {
    std::ostringstream detail;
    detail << "kind=" << kind << " tile=" << tile;
    LogRound(r->hzmj_round_id, "server", "hzmj", r->room_id, uid, seat, "cmd_gang", detail.str());
  }
  if (kind == 0) r->hzmj->OnAnGang(seat, tile);
  else r->hzmj->OnBuGang(seat, tile);
}

void RoomManager::Tick() {
  const auto now = std::chrono::steady_clock::now();
  for (auto& sh : shards_) {
    std::lock_guard<std::recursive_mutex> lk(sh.mu);
    for (auto& kv : sh.rooms) {
      Room& r = kv.second;
      if (r.game) {
        r.game->Tick(now);
        r.phase = r.game->Phase();
        if (r.game->Finished()) {
          r.phase = "WaitReady";
          r.game.reset();
          PushRoomState(r.room_id);
        }
      }
      if (r.hzmj && !r.hzmj_done) {
        TickHzmj(r, now);
      }
    }
  }
}

DdzClassicSimple::DdzClassicSimple(RoomManager& rooms, int64_t room_id, std::array<int64_t, 3> uids, GameConfig cfg)
    : rooms_(rooms), room_id_(room_id), uids_(uids), cfg_(std::move(cfg)) {
  round_id_ = NowMs();
}

std::vector<int64_t> DdzClassicSimple::AllUids() const { return {uids_[0], uids_[1], uids_[2]}; }

void DdzClassicSimple::Start() { DealAndBid(); }

void DdzClassicSimple::DealAndBid() {
  std::vector<int> deck;
  for (int i = 0; i < 54; ++i) deck.push_back(i);
  std::mt19937 rng{std::random_device{}()};
  std::shuffle(deck.begin(), deck.end(), rng);
  for (int i = 0; i < 3; ++i) {
    hands_[i].assign(deck.begin() + i * 17, deck.begin() + (i + 1) * 17);
    std::sort(hands_[i].begin(), hands_[i].end(), [](int a, int b) {
      return ddz::CardRank(a) > ddz::CardRank(b) || (ddz::CardRank(a) == ddz::CardRank(b) && a > b);
    });
  }
  bottom_.assign(deck.begin() + 51, deck.end());
  landlord_ = -1;
  bid_score_ = 0;
  bids_made_ = 0;
  current_seat_ = 0;
  phase_ = "Bid";

  for (int i = 0; i < 3; ++i) {
    std::vector<int32_t> hand(hands_[i].begin(), hands_[i].end());
    rooms_.SendToUid(uids_[i], MsgId::kS2C_DdzGameStart,
                     proto_wire::EncodeS2C_DdzGameStart(i, hand, -1, {}, round_id_));
  }
  {
    std::ostringstream detail;
    detail << "phase=Bid";
    for (int i = 0; i < 3; ++i) {
      std::vector<int32_t> hand(hands_[i].begin(), hands_[i].end());
      detail << " hand" << i << "=" << JoinIds(hand);
    }
    std::vector<int32_t> bot(bottom_.begin(), bottom_.end());
    detail << " bottom=" << JoinIds(bot);
    LogRound(round_id_, "server", "ddz", room_id_, uids_[0], 0, "deal", detail.str());
  }
  BroadcastTurn();
}

void DdzClassicSimple::BroadcastTurn() {
  const int timeout = phase_ == "Bid" ? cfg_.bid_timeout_s : cfg_.play_timeout_s;
  const auto body = proto_wire::EncodeS2C_DdzTurn(current_seat_, phase_, timeout);
  for (int64_t uid : AllUids()) rooms_.SendToUid(uid, MsgId::kS2C_DdzTurn, body);
  LogRound(round_id_, "server", "ddz", room_id_, uids_[current_seat_], current_seat_, "turn",
           "phase=" + phase_ + " timeout=" + std::to_string(timeout));
  deadline_ = std::chrono::steady_clock::now() + std::chrono::seconds(timeout);
}

void DdzClassicSimple::OnBid(int seat, int score) {
  if (finished_ || phase_ != "Bid" || seat != current_seat_) return;
  if (score < 0 || score > 3) {
    rooms_.SendToUid(uids_[seat], MsgId::kS2C_Error,
                     proto_wire::EncodeS2C_Error(static_cast<int>(Err::kBadParam), "bad bid", MsgId::kC2S_DdzBid));
    return;
  }
  if (score > 0 && score <= bid_score_) {
    rooms_.SendToUid(uids_[seat], MsgId::kS2C_Error,
                     proto_wire::EncodeS2C_Error(static_cast<int>(Err::kBadParam), "bid too low", MsgId::kC2S_DdzBid));
    return;
  }
  LogRound(round_id_, "server", "ddz", room_id_, uids_[seat], seat, "bid", "score=" + std::to_string(score));
  const auto body = proto_wire::EncodeS2C_DdzBidBroadcast(seat, score);
  for (int64_t uid : AllUids()) rooms_.SendToUid(uid, MsgId::kS2C_DdzBidBroadcast, body);

  if (score > bid_score_) {
    bid_score_ = score;
    landlord_ = seat;
  }
  ++bids_made_;
  if (score == 3 || bids_made_ >= 3) {
    FinishBid();
    return;
  }
  current_seat_ = (current_seat_ + 1) % 3;
  BroadcastTurn();
}

void DdzClassicSimple::FinishBid() {
  if (landlord_ < 0 || bid_score_ <= 0) {
    ++redeal_;
    if (redeal_ >= 3) {
      landlord_ = 0;
      bid_score_ = 1;
    } else {
      DealAndBid();
      return;
    }
  }
  hands_[landlord_].insert(hands_[landlord_].end(), bottom_.begin(), bottom_.end());
  std::sort(hands_[landlord_].begin(), hands_[landlord_].end(), [](int a, int b) {
    return ddz::CardRank(a) > ddz::CardRank(b) || (ddz::CardRank(a) == ddz::CardRank(b) && a > b);
  });

  for (int i = 0; i < 3; ++i) {
    std::vector<int32_t> hand(hands_[i].begin(), hands_[i].end());
    std::vector<int32_t> bot(bottom_.begin(), bottom_.end());
    rooms_.SendToUid(uids_[i], MsgId::kS2C_DdzGameStart,
                     proto_wire::EncodeS2C_DdzGameStart(i, hand, landlord_, bot, round_id_));
  }
  {
    std::ostringstream detail;
    detail << "landlord=" << landlord_ << " bid=" << bid_score_;
    std::vector<int32_t> bot(bottom_.begin(), bottom_.end());
    detail << " bottom=" << JoinIds(bot);
    LogRound(round_id_, "server", "ddz", room_id_, landlord_ >= 0 ? uids_[landlord_] : 0, landlord_, "play_start",
             detail.str());
  }

  phase_ = "Play";
  current_seat_ = landlord_;
  last_play_seat_ = -1;
  last_pattern_ = {};
  last_cards_.clear();
  passes_ = 0;
  bomb_count_ = 0;
  spring_ = true;
  play_count_non_landlord_ = 0;
  BroadcastTurn();
}

void DdzClassicSimple::OnPlay(int seat, bool pass, const std::vector<int>& cards) {
  if (finished_ || phase_ != "Play" || seat != current_seat_) return;

  if (pass) {
    if (last_play_seat_ < 0) {
      rooms_.SendToUid(uids_[seat], MsgId::kS2C_Error,
                       proto_wire::EncodeS2C_Error(static_cast<int>(Err::kBadParam), "cannot pass on lead",
                                                   MsgId::kC2S_DdzPlay));
      return;
    }
    LogRound(round_id_, "server", "ddz", room_id_, uids_[seat], seat, "pass", "");
    const auto body =
        proto_wire::EncodeS2C_DdzPlayBroadcast(seat, true, {}, static_cast<int32_t>(hands_[seat].size()));
    for (int64_t uid : AllUids()) rooms_.SendToUid(uid, MsgId::kS2C_DdzPlayBroadcast, body);
    ++passes_;
    if (passes_ >= 2) {
      last_play_seat_ = -1;
      last_pattern_ = {};
      last_cards_.clear();
      passes_ = 0;
    }
    current_seat_ = (current_seat_ + 1) % 3;
    BroadcastTurn();
    return;
  }

  if (!ddz::ContainsAll(hands_[seat], cards)) {
    rooms_.SendToUid(uids_[seat], MsgId::kS2C_Error,
                     proto_wire::EncodeS2C_Error(static_cast<int>(Err::kBadParam), "cards not in hand",
                                                 MsgId::kC2S_DdzPlay));
    return;
  }
  auto pat = ddz::Identify(cards);
  if (pat.type == ddz::PatternType::kInvalid) {
    rooms_.SendToUid(uids_[seat], MsgId::kS2C_Error,
                     proto_wire::EncodeS2C_Error(static_cast<int>(Err::kBadParam), "invalid pattern",
                                                 MsgId::kC2S_DdzPlay));
    return;
  }
  if (!ddz::CanBeat(pat, last_pattern_)) {
    rooms_.SendToUid(uids_[seat], MsgId::kS2C_Error,
                     proto_wire::EncodeS2C_Error(static_cast<int>(Err::kBadParam), "cannot beat", MsgId::kC2S_DdzPlay));
    return;
  }

  ddz::RemoveCards(hands_[seat], cards);
  if (pat.type == ddz::PatternType::kBomb || pat.type == ddz::PatternType::kRocket) ++bomb_count_;
  if (seat != landlord_) ++play_count_non_landlord_;

  last_pattern_ = pat;
  last_cards_ = cards;
  last_play_seat_ = seat;
  passes_ = 0;

  std::vector<int32_t> played(cards.begin(), cards.end());
  LogRound(round_id_, "server", "ddz", room_id_, uids_[seat], seat, "play", "cards=" + JoinIds(played));
  const auto body =
      proto_wire::EncodeS2C_DdzPlayBroadcast(seat, false, played, static_cast<int32_t>(hands_[seat].size()));
  for (int64_t uid : AllUids()) rooms_.SendToUid(uid, MsgId::kS2C_DdzPlayBroadcast, body);

  if (hands_[seat].empty()) {
    const bool landlord_win = (seat == landlord_);
    if (landlord_win) {
      spring_ = (play_count_non_landlord_ == 0);
    } else {
      spring_ = static_cast<int>(hands_[landlord_].size()) == 20;
    }
    DoSettle(landlord_win);
    return;
  }

  current_seat_ = (current_seat_ + 1) % 3;
  BroadcastTurn();
}

void DdzClassicSimple::DoSettle(bool landlord_win) {
  phase_ = "Settle";
  int mult = bid_score_;
  for (int i = 0; i < bomb_count_; ++i) mult *= 2;
  if (spring_) mult *= 2;
  const int64_t stake = static_cast<int64_t>(cfg_.base_score) * mult;

  std::vector<proto_wire::SettleEntry> entries;
  std::array<int64_t, 3> deltas{0, 0, 0};
  if (landlord_win) {
    deltas[landlord_] = stake * 2;
    for (int i = 0; i < 3; ++i)
      if (i != landlord_) deltas[i] = -stake;
  } else {
    deltas[landlord_] = -stake * 2;
    for (int i = 0; i < 3; ++i)
      if (i != landlord_) deltas[i] = stake;
  }

  for (int i = 0; i < 3; ++i) {
    if (deltas[i] > 0) {
      const int64_t rake = deltas[i] * cfg_.rake_bp / 10000;
      deltas[i] -= rake;
    }
  }

  for (int i = 0; i < 3; ++i) {
    const std::string key = "settle:" + std::to_string(round_id_) + ":" + std::to_string(uids_[i]);
    rooms_.Wallet().Adjust(uids_[i], Currency::kGold, deltas[i], "game_settle", key);
    proto_wire::SettleEntry e;
    e.uid = uids_[i];
    e.seat_id = i;
    e.delta_gold = deltas[i];
    entries.push_back(e);
  }

  const auto body = proto_wire::EncodeS2C_DdzSettle(round_id_, cfg_.base_score, mult, entries);
  for (int64_t uid : AllUids()) rooms_.SendToUid(uid, MsgId::kS2C_DdzSettle, body);
  int tid = 1;
  if (auto* room = rooms_.FindRoomUnlocked(room_id_)) tid = room->template_id;
  nlohmann::json players = nlohmann::json::array();
  for (int i = 0; i < 3; ++i) {
    players.push_back(
        {{"uid", uids_[i]}, {"seat_id", i}, {"delta", deltas[i]}, {"is_landlord", i == landlord_}});
  }
  const std::string players_json = players.dump();
  if (rooms_.Admin()) {
    rooms_.Admin()->RecordRound(round_id_, room_id_, tid, players_json, cfg_.base_score, mult, 1);
  }
  if (rooms_.Social()) {
    try {
      rooms_.Social()->OnRoundSettled(round_id_, tid, players_json, cfg_.base_score, mult);
    } catch (...) {
    }
  }
  if (rooms_.Activity()) {
    for (int64_t uid : AllUids()) {
      try {
        rooms_.Activity()->OnGameSettled(uid, tid);
      } catch (...) {
      }
    }
  }
  finished_ = true;
  {
    std::ostringstream detail;
    detail << "landlord=" << landlord_ << " win=" << (landlord_win ? 1 : 0) << " mult=" << mult
           << " base=" << cfg_.base_score;
    for (int i = 0; i < 3; ++i) detail << " d" << i << "=" << deltas[i];
    LogRound(round_id_, "server", "ddz", room_id_, landlord_ >= 0 ? uids_[landlord_] : 0, landlord_, "settle",
             detail.str());
  }
}

void DdzClassicSimple::Tick(std::chrono::steady_clock::time_point now) {
  if (finished_) return;
  // Trusteeship: act immediately when it's their turn
  if (rooms_.IsTrusteeship(room_id_, current_seat_)) {
    LogRound(round_id_, "server", "ddz", room_id_, uids_[current_seat_], current_seat_, "trust_act", phase_);
    AutoActIfTrusted(current_seat_);
    return;
  }
  if (now < deadline_) return;
  LogRound(round_id_, "server", "ddz", room_id_, uids_[current_seat_], current_seat_, "timeout", phase_);
  AutoActIfTrusted(current_seat_);
}

void DdzClassicSimple::AutoActIfTrusted(int seat) {
  if (finished_ || seat != current_seat_) return;
  if (phase_ == "Bid") {
    OnBid(seat, 0);
  } else if (phase_ == "Play") {
    if (last_play_seat_ < 0) {
      if (!hands_[seat].empty()) {
        std::vector<int> c{hands_[seat].back()};
        OnPlay(seat, false, c);
      }
    } else {
      OnPlay(seat, true, {});
    }
  }
}

int DdzClassicSimple::TimeoutLeftS(std::chrono::steady_clock::time_point now) const {
  if (now >= deadline_) return 0;
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(deadline_ - now).count();
  if (ms <= 0) return 0;
  return static_cast<int>((ms + 999) / 1000);
}

std::vector<int> DdzClassicSimple::HandOf(int seat) const {
  if (seat < 0 || seat > 2) return {};
  return hands_[seat];
}

void DdzClassicSimple::SendReconnectSnapshot(int64_t uid) {
  int seat = -1;
  for (int i = 0; i < 3; ++i)
    if (uids_[i] == uid) seat = i;
  if (seat < 0) return;
  std::vector<int32_t> hand(hands_[seat].begin(), hands_[seat].end());
  const int timeout = TimeoutLeftS(std::chrono::steady_clock::now());
  {
    std::ostringstream detail;
    detail << "phase=" << phase_ << " turn=" << current_seat_ << " left=" << timeout
           << " hand=" << JoinIds(hand);
    LogRound(round_id_, "server", "ddz", room_id_, uid, seat, "reconnect", detail.str());
  }
  rooms_.SendToUid(uid, MsgId::kS2C_DdzReconnect,
                   proto_wire::EncodeS2C_DdzReconnect(seat, phase_, hand, landlord_, current_seat_, timeout));
}

}  // namespace pandora

