#include "game/fish/fish_room_game.hpp"

#include <cmath>
#include <sstream>

#include <nlohmann/json.hpp>

#include "common/errors.hpp"
#include "common/log.hpp"
#include "common/proto_wire.hpp"
#include "game/fish/config_store.hpp"
#include "game/fish/math.hpp"
#include "game/game_ids.hpp"
#include "store/memory_store.hpp"
#include "wallet/wallet_service.hpp"

namespace pandora {

namespace {

void Broadcast(RoomCtx& ctx, uint32_t msg_id, const std::vector<uint8_t>& body) {
  if (!ctx.send) return;
  for (int i = 0; i < ctx.seat_count; ++i) {
    const int64_t uid = ctx.uid_of ? ctx.uid_of(i) : 0;
    if (uid) ctx.send(uid, msg_id, body);
  }
}

void SendError(RoomCtx& ctx, int64_t uid, Err e, uint32_t ref) {
  if (!ctx.send) return;
  ctx.send(uid, MsgId::kS2C_Error,
           proto_wire::EncodeS2C_Error(static_cast<int32_t>(e), ErrMessage(e), ref));
}

std::string CfgJson(const fish::FishConfig& cfg) {
  nlohmann::json j;
  j["base_score"] = cfg.base_score;
  j["rake_bp"] = cfg.rake_bp;
  j["fire_rate_hz"] = cfg.fire_rate_hz;
  j["p_min"] = cfg.p_min;
  j["p_max"] = cfg.p_max;
  j["cannon_mults"] = cfg.cannon_mults;
  j["max_catch_reward"] = cfg.max_catch_reward;
  return j.dump();
}

}  // namespace

FishRoomGame::FishRoomGame(RoomCtx& ctx) : ctx_(ctx) {}

int64_t FishRoomGame::SeatGold(int64_t uid) const {
  if (!ctx_.store || !uid) return 0;
  auto p = ctx_.store->GetPlayer(uid);
  return p ? p->gold : 0;
}

void FishRoomGame::Start() {
  auto cfg = fish::FishConfigStore::Instance().Get();
  cfg.base_score = ctx_.cfg.base_score > 0 ? ctx_.cfg.base_score : cfg.base_score;
  cfg.rake_bp = ctx_.cfg.rake_bp;

  table_ = std::make_unique<fish::FishTable>(cfg, [this]() { return static_cast<uint32_t>(rng_()); });
  table_->SetSink([this](const fish::OutEvent& ev) { OnEvent(ev); });

  std::vector<int64_t> uids;
  uids.reserve(static_cast<size_t>(ctx_.seat_count));
  for (int i = 0; i < ctx_.seat_count; ++i) uids.push_back(ctx_.uid_of ? ctx_.uid_of(i) : 0);

  round_id_ = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::system_clock::now().time_since_epoch())
                  .count();
  done_ = false;
  started_ = true;
  last_tick_ = std::chrono::steady_clock::now();
  if (ctx_.set_phase) ctx_.set_phase("Playing");
  table_->Start(uids);
  BroadcastStart();
  if (ctx_.push_room_state) ctx_.push_room_state();
  LogRound(round_id_, "server", "fish", ctx_.room_id, uids.empty() ? 0 : uids[0], 0, "start",
           "seats=" + std::to_string(ctx_.seat_count));
}

void FishRoomGame::BroadcastStart() {
  if (!table_) return;
  auto cfg = fish::FishConfigStore::Instance().Get();
  cfg.base_score = ctx_.cfg.base_score > 0 ? ctx_.cfg.base_score : cfg.base_score;
  for (int i = 0; i < ctx_.seat_count; ++i) {
    const int64_t uid = ctx_.uid_of ? ctx_.uid_of(i) : 0;
    if (!uid || !ctx_.send) continue;
    SendSnapshot(uid);
  }
}

void FishRoomGame::SendSnapshot(int64_t uid) {
  if (!table_ || !ctx_.send) return;
  const int seat = ctx_.seat_of ? ctx_.seat_of(uid) : -1;
  auto cfg = fish::FishConfigStore::Instance().Get();
  if (ctx_.cfg.base_score > 0) cfg.base_score = ctx_.cfg.base_score;
  cfg.rake_bp = ctx_.cfg.rake_bp;

  proto_wire::FishGameStartData data;
  data.round_id = round_id_;
  data.room_id = ctx_.room_id;
  data.template_id = ctx_.template_id;
  data.self_seat = seat;
  data.base_score = cfg.base_score;
  data.cannon_mults = cfg.cannon_mults;
  data.cfg_snapshot = CfgJson(cfg);

  for (int i = 0; i < ctx_.seat_count; ++i) {
    const int64_t su = ctx_.uid_of ? ctx_.uid_of(i) : 0;
    if (!su) continue;
    proto_wire::FishSeatData s;
    s.seat_id = i;
    s.uid = su;
    s.cannon_mult = table_->seat(i).mult;
    s.online = table_->seat(i).online && !table_->seat(i).left;
    s.gold = SeatGold(su);
    s.last_client_seq = table_->seat(i).max_client_seq;
    if (ctx_.store) {
      auto p = ctx_.store->GetPlayer(su);
      if (p) s.nickname = p->nickname;
    }
    data.seats.push_back(s);
  }

  for (const auto& f : table_->SnapshotFish()) {
    proto_wire::FishSnapData s;
    s.fish_id = f.fish_id;
    s.type_id = f.type_id;
    s.x = f.x;
    s.y = f.y;
    s.vx = f.vx;
    s.vy = f.vy;
    s.radius = f.radius;
    s.hp = f.hp;
    s.hp_max = f.hp_max;
    data.fish.push_back(s);
  }

  ctx_.send(uid, MsgId::kS2C_FishGameStart, proto_wire::EncodeS2C_FishGameStart(data));
}

void FishRoomGame::OnEvent(const fish::OutEvent& ev) {
  if (!table_) return;
  if (ev.type == "spawn") {
    proto_wire::FishSnapData f;
    f.fish_id = ev.fish_id;
    f.type_id = ev.type_id;
    f.x = ev.x;
    f.y = ev.y;
    f.vx = ev.vx;
    f.vy = ev.vy;
    f.radius = ev.radius;
    f.hp = ev.hp;
    f.hp_max = ev.hp_max;
    Broadcast(ctx_, MsgId::kS2C_FishSpawn, proto_wire::EncodeS2C_FishSpawn({f}));
    return;
  }
  if (ev.type == "despawn") {
    Broadcast(ctx_, MsgId::kS2C_FishDespawn, proto_wire::EncodeS2C_FishDespawn({ev.fish_id}, ev.detail));
    return;
  }
  if (ev.type == "fire") {
    // Broadcast after wallet in Handle
    return;
  }
  if (ev.type == "hit") {
    Broadcast(ctx_, MsgId::kS2C_FishHit,
              proto_wire::EncodeS2C_FishHit(ev.bullet_id, ev.fish_id, ev.seat, ev.hp, ev.hp_max));
    return;
  }
  if (ev.type == "catch") {
    // Applied in ApplyPendingRewards
    return;
  }
  if (ev.type == "seat_update") {
    proto_wire::FishSeatData s;
    s.seat_id = ev.seat;
    s.uid = ev.uid;
    if (ev.seat >= 0 && ev.seat < fish::kMaxSeats) {
      s.cannon_mult = table_->seat(ev.seat).mult;
      s.online = table_->seat(ev.seat).online && !table_->seat(ev.seat).left;
      s.last_client_seq = table_->seat(ev.seat).max_client_seq;
    }
    s.gold = SeatGold(ev.uid);
    Broadcast(ctx_, MsgId::kS2C_FishSeatUpdate, proto_wire::EncodeS2C_FishSeatUpdate(s));
  }
}

void FishRoomGame::ApplyPendingRewards() {
  if (!table_ || !ctx_.wallet) return;
  for (const auto& plan : table_->pending_rewards()) {
    const std::string key =
        "fish:catch:" + std::to_string(round_id_) + ":" + std::to_string(plan.fish_id) + ":" +
        std::to_string(plan.uid);
    auto adj = ctx_.wallet->Adjust(plan.uid, Currency::kGold, plan.reward, "fish_catch", key,
                                   std::to_string(ctx_.room_id));
    const int64_t gold = adj.ok ? adj.balance : SeatGold(plan.uid);
    Broadcast(ctx_, MsgId::kS2C_FishCatch,
              proto_wire::EncodeS2C_FishCatch(plan.fish_id, plan.type_id, plan.seat, plan.uid, plan.reward, gold));
    LogRound(round_id_, "server", "fish", ctx_.room_id, plan.uid, plan.seat, "catch",
             "fish=" + std::to_string(plan.fish_id) + " reward=" + std::to_string(plan.reward));
  }
  table_->ClearPendingRewards();
}

void FishRoomGame::Tick(std::chrono::steady_clock::time_point now) {
  if (!started_ || done_ || !table_) return;
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_tick_).count();
  if (elapsed <= 0) return;
  last_tick_ = now;
  // Cap catch-up
  if (elapsed > 200) elapsed = 200;
  table_->Tick(static_cast<int>(elapsed));
  ApplyPendingRewards();
  FinishIfEmpty();
}

void FishRoomGame::FinishIfEmpty() {
  if (!table_ || done_) return;
  bool any = false;
  for (int i = 0; i < ctx_.seat_count; ++i) {
    if (table_->seat(i).uid && !table_->seat(i).left) {
      any = true;
      break;
    }
  }
  if (any) return;
  done_ = true;
  if (ctx_.set_phase) ctx_.set_phase("Closed");
  if (ctx_.on_round_finished) ctx_.on_round_finished();
}

bool FishRoomGame::InProgress() const { return started_ && !done_; }

void FishRoomGame::OnDisconnect(int64_t uid) {
  if (!table_) return;
  const int seat = ctx_.seat_of ? ctx_.seat_of(uid) : -1;
  if (seat < 0) return;
  table_->SetOnline(seat, false);
  LogRound(round_id_, "server", "fish", ctx_.room_id, uid, seat, "disconnect", "");
}

void FishRoomGame::OnReconnect(int64_t uid) {
  if (!table_) return;
  const int seat = ctx_.seat_of ? ctx_.seat_of(uid) : -1;
  if (seat < 0) return;
  table_->SetOnline(seat, true);
  SendSnapshot(uid);
  LogRound(round_id_, "server", "fish", ctx_.room_id, uid, seat, "reconnect", "");
}

bool FishRoomGame::Handle(int64_t uid, uint32_t msg_id, const uint8_t* body, size_t len) {
  if (!table_ || done_) return false;
  const int seat = ctx_.seat_of ? ctx_.seat_of(uid) : -1;
  if (seat < 0) {
    SendError(ctx_, uid, Err::kFishBadSeat, msg_id);
    return true;
  }

  if (msg_id == MsgId::kC2S_FishSetMult) {
    int32_t mult = 0;
    if (!proto_wire::DecodeC2S_FishSetMult(body, len, mult)) {
      SendError(ctx_, uid, Err::kBadParam, msg_id);
      return true;
    }
    if (!table_->SetCannonMult(seat, mult)) {
      SendError(ctx_, uid, Err::kFishIllegalMult, msg_id);
    }
    return true;
  }

  if (msg_id == MsgId::kC2S_FishLeave) {
    table_->OnLeave(seat);
    LogRound(round_id_, "server", "fish", ctx_.room_id, uid, seat, "leave", "");
    FinishIfEmpty();
    return true;
  }

  if (msg_id == MsgId::kC2S_FishFire) {
    fish::FireRequest req;
    if (!proto_wire::DecodeC2S_FishFire(body, len, req.mult, req.aim_x, req.aim_y, req.lock_fish_id,
                                        req.client_seq)) {
      SendError(ctx_, uid, Err::kBadParam, msg_id);
      return true;
    }
    req.seat = seat;
    req.uid = uid;

    if (!table_->seat(seat).online || table_->seat(seat).left) {
      SendError(ctx_, uid, Err::kFishBadSeat, msg_id);
      return true;
    }

    // Pre-check mult / rate / seq without creating bullet: TryFire does all
    const int64_t cost = fish::FireCost(req.mult, ctx_.cfg.base_score > 0 ? ctx_.cfg.base_score : 100);
    if (SeatGold(uid) < cost) {
      SendError(ctx_, uid, Err::kFishInsufficient, msg_id);
      return true;
    }

    if (req.client_seq > 0 && table_->seat(seat).seen_seq.count(req.client_seq)) {
      SendError(ctx_, uid, Err::kFishDupSeq, msg_id);
      return true;
    }

    fish::FireCostPlan plan{};
    if (!table_->TryFire(req, &plan)) {
      bool valid_mult = false;
      auto cfg = fish::FishConfigStore::Instance().Get();
      for (int m : cfg.cannon_mults)
        if (m == req.mult) valid_mult = true;
      if (!valid_mult)
        SendError(ctx_, uid, Err::kFishIllegalMult, msg_id);
      else
        SendError(ctx_, uid, Err::kFishFireTooFast, msg_id);
      return true;
    }

    if (!ctx_.wallet) {
      SendError(ctx_, uid, Err::kInternal, msg_id);
      return true;
    }
    const std::string key =
        "fish:fire:" + std::to_string(round_id_) + ":" + std::to_string(uid) + ":" +
        std::to_string(plan.client_seq);
    const std::string remark = "rake=" + std::to_string(plan.rake);
    auto adj = ctx_.wallet->Adjust(uid, Currency::kGold, -plan.cost, "fish_fire", key, remark);
    if (!adj.ok) {
      SendError(ctx_, uid, Err::kFishInsufficient, msg_id);
      return true;
    }

    proto_wire::FishFireBroadcastData fb;
    fb.seat_id = seat;
    fb.uid = uid;
    fb.bullet_id = plan.bullet_id;
    fb.mult = plan.mult;
    fb.x = fish::kSeatCannonPos[seat].x;
    fb.y = fish::kSeatCannonPos[seat].y;
    // velocity from last fire event not stored — approximate toward aim
    {
      float dx = req.aim_x - fb.x;
      float dy = req.aim_y - fb.y;
      float len = std::sqrt(dx * dx + dy * dy);
      float spd = 1200.f;
      if (len > 1e-3f) {
        fb.vx = dx / len * spd;
        fb.vy = dy / len * spd;
      } else {
        fb.vx = 0;
        fb.vy = spd;
      }
    }
    fb.client_seq = plan.client_seq;
    fb.gold = adj.balance;
    fb.cost = plan.cost;
    Broadcast(ctx_, MsgId::kS2C_FishFireBroadcast, proto_wire::EncodeS2C_FishFireBroadcast(fb));
    LogRound(round_id_, "server", "fish", ctx_.room_id, uid, seat, "fire",
             "mult=" + std::to_string(plan.mult) + " cost=" + std::to_string(plan.cost) +
                 " rake=" + std::to_string(plan.rake));
    return true;
  }

  if (msg_id == MsgId::kC2S_FishLock) {
    // P1 stub: accept and ignore
    return true;
  }

  return false;
}

}  // namespace pandora
