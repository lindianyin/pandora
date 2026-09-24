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
#include "wallet/wallet_service.hpp"

namespace pandora {

namespace {

int64_t NowMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
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
  // Prefetch nicknames / online without holding room locks.
  std::array<std::string, 3> nicks{};
  std::array<bool, 3> online{};
  for (int i = 0; i < 3; ++i) {
    auto p = store_.GetPlayer(uids[i]);
    nicks[i] = p ? p->nickname : ("P" + std::to_string(uids[i]));
    online[i] = hub_.IsOnline(uids[i]);
  }

  int64_t rid = 0;
  {
    std::lock_guard<std::mutex> ilk(index_mu_);
    rid = next_room_id_++;
    for (int64_t uid : uids) uid_to_room_[uid] = rid;
    std::lock_guard<std::recursive_mutex> slk(Shard(rid).mu);
    Room room;
    room.room_id = rid;
    room.template_id = template_id;
    room.phase = "WaitReady";
    for (int i = 0; i < 3; ++i) {
      room.seats[i].uid = uids[i];
      room.seats[i].nickname = nicks[i];
      room.seats[i].ready = false;
      room.seats[i].online = online[i];
    }
    Shard(rid).rooms[rid] = std::move(room);
  }
  PLOG_INFO("room created id=" << rid);
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

void RoomManager::PushRoomState(int64_t room_id) {
  std::lock_guard<std::recursive_mutex> lk(Shard(room_id).mu);
  Room* r = FindRoomUnlocked(room_id);
  if (!r) return;
  std::vector<proto_wire::RoomSeat> seats;
  for (int i = 0; i < 3; ++i) {
    proto_wire::RoomSeat s;
    s.seat_id = i;
    s.uid = r->seats[i].uid;
    s.nickname = r->seats[i].nickname;
    s.ready = r->seats[i].ready;
    s.online = r->seats[i].online;
    s.trusteeship = r->seats[i].trusteeship;
    seats.push_back(s);
  }
  const auto body = proto_wire::EncodeS2C_RoomState(r->room_id, r->template_id, seats, r->phase);
  for (const auto& s : r->seats) {
    if (s.uid) hub_.Send(s.uid, MsgId::kS2C_RoomState, body);
  }
}

void RoomManager::MaybeStart(Room& room) {
  for (const auto& s : room.seats) {
    if (!s.uid || !s.ready) return;
  }
  std::array<int64_t, 3> uids{room.seats[0].uid, room.seats[1].uid, room.seats[2].uid};
  room.game = std::make_unique<DdzClassicSimple>(*this, room.room_id, uids, cfg_);
  room.phase = "Deal";
  for (auto& s : room.seats) s.ready = false;
  PushRoomState(room.room_id);
  room.game->Start();
  room.phase = room.game->Phase();
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
  if (r->game && !r->game->Finished()) {
    hub_.Send(uid, MsgId::kS2C_Error,
              proto_wire::EncodeS2C_Error(static_cast<int>(Err::kBadParam), "game in progress", MsgId::kC2S_Ready));
    return false;
  }
  for (auto& s : r->seats) {
    if (s.uid == uid) {
      s.ready = ready;
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
  if (r->game && !r->game->Finished()) {
    for (auto& s : r->seats) {
      if (s.uid == uid) s.online = false;
    }
    PushRoomState(rid);
    return true;
  }
  for (auto& s : r->seats) {
    if (s.uid == uid) {
      s.uid = 0;
      s.ready = false;
      s.nickname.clear();
    }
  }
  uid_to_room_.erase(uid);
  bool empty = true;
  for (const auto& s : r->seats)
    if (s.uid) empty = false;
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
  for (auto& s : r->seats) {
    if (s.uid == uid) {
      s.online = false;
      if (r->game && !r->game->Finished()) s.trusteeship = true;
    }
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
  for (auto& s : r->seats) {
    if (s.uid == uid) {
      s.online = true;
      s.trusteeship = false;
    }
  }
  PushRoomState(r->room_id);
  if (r->game && !r->game->Finished()) r->game->SendReconnectSnapshot(uid);
}

void RoomManager::ClearTrusteeshipUid(int64_t uid) {
  // Prefer ClearTrusteeshipInRoom when room is already known.
  auto rid_opt = RoomIdOfUnlocked(uid);
  if (!rid_opt) return;
  Room* r = FindRoomUnlocked(*rid_opt);
  if (!r) return;
  for (auto& s : r->seats) {
    if (s.uid == uid) s.trusteeship = false;
  }
}

void RoomManager::ClearTrusteeshipInRoom(Room& r, int64_t uid) {
  for (auto& s : r.seats) {
    if (s.uid == uid) s.trusteeship = false;
  }
}

bool RoomManager::IsTrusteeship(int64_t room_id, int seat) {
  // Caller typically holds shard lock (recursive).
  std::lock_guard<std::recursive_mutex> lk(Shard(room_id).mu);
  Room* r = FindRoomUnlocked(room_id);
  if (!r || seat < 0 || seat > 2) return false;
  return r->seats[seat].trusteeship;
}

void RoomManager::SetTrusteeship(int64_t room_id, int seat, bool on) {
  std::lock_guard<std::recursive_mutex> lk(Shard(room_id).mu);
  Room* r = FindRoomUnlocked(room_id);
  if (!r || seat < 0 || seat > 2) return;
  r->seats[seat].trusteeship = on;
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
  int seat = -1;
  for (int i = 0; i < 3; ++i)
    if (r->seats[i].uid == uid) seat = i;
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
  int seat = -1;
  for (int i = 0; i < 3; ++i)
    if (r->seats[i].uid == uid) seat = i;
  if (seat < 0) return;
  r->game->OnPlay(seat, pass, cards);
  r->phase = r->game->Phase();
  if (r->game->Finished()) {
    r->phase = "WaitReady";
    r->game.reset();
    PushRoomState(r->room_id);
  }
}

void RoomManager::Tick() {
  const auto now = std::chrono::steady_clock::now();
  for (auto& sh : shards_) {
    std::lock_guard<std::recursive_mutex> lk(sh.mu);
    for (auto& kv : sh.rooms) {
      Room& r = kv.second;
      if (!r.game) continue;
      r.game->Tick(now);
      r.phase = r.game->Phase();
      if (r.game->Finished()) {
        r.phase = "WaitReady";
        r.game.reset();
        PushRoomState(r.room_id);
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
    rooms_.SendToUid(uids_[i], MsgId::kS2C_DdzGameStart, proto_wire::EncodeS2C_DdzGameStart(i, hand, -1, {}));
  }
  BroadcastTurn();
}

void DdzClassicSimple::BroadcastTurn() {
  const int timeout = phase_ == "Bid" ? cfg_.bid_timeout_s : cfg_.play_timeout_s;
  const auto body = proto_wire::EncodeS2C_DdzTurn(current_seat_, phase_, timeout);
  for (int64_t uid : AllUids()) rooms_.SendToUid(uid, MsgId::kS2C_DdzTurn, body);
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
                     proto_wire::EncodeS2C_DdzGameStart(i, hand, landlord_, bot));
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
  if (rooms_.Admin()) {
    nlohmann::json players = nlohmann::json::array();
    for (int i = 0; i < 3; ++i) {
      players.push_back({{"uid", uids_[i]}, {"delta", deltas[i]}});
    }
    rooms_.Admin()->RecordRound(round_id_, room_id_, tid, players.dump(), cfg_.base_score, mult);
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
  PLOG_INFO("settle room=" << room_id_ << " round=" << round_id_ << " mult=" << mult);
}

void DdzClassicSimple::Tick(std::chrono::steady_clock::time_point now) {
  if (finished_) return;
  // Trusteeship: act immediately when it's their turn
  if (rooms_.IsTrusteeship(room_id_, current_seat_)) {
    AutoActIfTrusted(current_seat_);
    return;
  }
  if (now < deadline_) return;
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
  return static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(deadline_ - now).count());
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
  rooms_.SendToUid(uid, MsgId::kS2C_DdzReconnect,
                   proto_wire::EncodeS2C_DdzReconnect(seat, phase_, hand, landlord_, current_seat_, timeout));
  BroadcastTurn();
}

}  // namespace pandora

