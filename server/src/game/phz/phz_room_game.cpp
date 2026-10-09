#include "game/phz/phz_room_game.hpp"

#include <sstream>

#include <nlohmann/json.hpp>

#include "activity/activity_service.hpp"
#include "admin/admin_service.hpp"
#include "common/errors.hpp"
#include "common/log.hpp"
#include "common/proto_wire.hpp"
#include "game/game_ids.hpp"
#include "game/phz/config.hpp"
#include "social/social_service.hpp"
#include "wallet/wallet_service.hpp"

namespace pandora {

namespace {

int64_t NowMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

int SecondsLeft(std::chrono::steady_clock::time_point deadline) {
  using namespace std::chrono;
  const auto now = steady_clock::now();
  if (now >= deadline) return 0;
  const auto ms = duration_cast<milliseconds>(deadline - now).count();
  if (ms <= 0) return 0;
  return static_cast<int>((ms + 999) / 1000);
}

std::vector<int32_t> PhzHandToList(const phz::HandCount& h) {
  std::vector<int32_t> out;
  for (int t = 0; t < phz::kTileKinds; ++t) {
    for (int n = 0; n < h[static_cast<size_t>(t)]; ++n) out.push_back(t);
  }
  return out;
}

int PhzActionCode(const std::string& type) {
  if (type == "Chi") return 1;
  if (type == "Peng") return 2;
  if (type == "Wei" || type == "ChouWei") return 3;
  if (type == "Pao") return 5;
  if (type == "Ti") return 6;
  return 0;
}

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

}  // namespace

PhzRoomGame::PhzRoomGame(RoomCtx& ctx) : ctx_(ctx) {}

void PhzRoomGame::Start() {
  std::array<int64_t, phz::kSeats> uids{};
  for (int i = 0; i < ctx_.seat_count && i < phz::kSeats; ++i)
    uids[static_cast<size_t>(i)] = ctx_.uid_of ? ctx_.uid_of(i) : 0;

  int banker = ctx_.carry_banker ? *ctx_.carry_banker : 0;

  phz::PhzConfig pcfg;
  pcfg.base_score = ctx_.cfg.base_score;
  pcfg.action_timeout_s = ctx_.cfg.play_timeout_s;
  pcfg.rake_bp = ctx_.cfg.rake_bp;
  if (ctx_.cfg.phz_debug_deal) banker = 0;

  table_ = std::make_unique<phz::PhzTable>(pcfg, uids, banker);
  round_id_ = NowMs();
  done_ = false;
  if (ctx_.set_phase) ctx_.set_phase("Deal");

  table_->SetSink([this](const phz::OutEvent& ev) { OnEvent(ev); });

  if (ctx_.cfg.phz_debug_deal) {
    table_->ApplyDebugQuickHuDeal();
    if (ctx_.carry_banker) *ctx_.carry_banker = table_->banker_seat();
    PLOG_INFO("phz debug deal: seat1 ting Yi, banker discards Shi, seat1 zimo Yi");
  }

  if (ctx_.push_room_state) ctx_.push_room_state();
  table_->Start();
  if (table_->phase() == phz::Phase::kPlay && ctx_.set_phase) ctx_.set_phase("Play");
  ArmDeadline();
}

void PhzRoomGame::ArmDeadline() {
  deadline_ = std::chrono::steady_clock::now() + std::chrono::seconds(ctx_.cfg.play_timeout_s);
}

void PhzRoomGame::OnEvent(const phz::OutEvent& ev) {
  if (!table_) return;
  auto* t = table_.get();
  const int timeout_s = ctx_.cfg.play_timeout_s;
  auto uid_of = [&](int seat) -> int64_t {
    if (seat < 0 || seat >= ctx_.seat_count) return 0;
    return ctx_.uid_of ? ctx_.uid_of(seat) : 0;
  };

  if (ev.type == "GameStart") {
    const std::string snap = phz::ConfigSnapshotJson(phz::PhzConfig{});
    for (int i = 0; i < ctx_.seat_count; ++i) {
      const int64_t uid = ctx_.uid_of ? ctx_.uid_of(i) : 0;
      if (!uid || !ctx_.send) continue;
      auto hand = PhzHandToList(t->seat(i).hand);
      auto body = proto_wire::EncodeS2C_PhzGameStart(round_id_, ctx_.room_id, ctx_.template_id, t->banker_seat(),
                                                    hand, t->wall_remain(), i, ctx_.cfg.base_score, snap);
      ctx_.send(uid, MsgId::kS2C_PhzGameStart, body);
    }
    LogRound(round_id_, "server", "phz", ctx_.room_id, uid_of(t->banker_seat()), t->banker_seat(), "start",
             "wall=" + std::to_string(t->wall_remain()));
    if (ctx_.set_phase) ctx_.set_phase("Play");
    ArmDeadline();
    return;
  }

  if (ev.type == "Turn" || ev.type == "ClaimWindow") {
    std::string sub = "discard";
    if (ev.type == "ClaimWindow") sub = "claim";
    else if (!ev.detail.empty()) sub = ev.detail;
    int turn = t->turn_seat();
    if (sub == "claim") {
      turn = t->has_pending_reveal() ? t->last_reveal_seat() : t->last_discard_seat();
    }
    for (int i = 0; i < ctx_.seat_count; ++i) {
      const int64_t uid = ctx_.uid_of ? ctx_.uid_of(i) : 0;
      if (!uid || !ctx_.send) continue;
      if (ev.type == "ClaimWindow" && !t->SeatNeedsClaimInput(i) && i != turn) continue;
      std::vector<int32_t> sync;
      if (sub == "discard" && i == t->turn_seat()) sync = PhzHandToList(t->seat(i).hand);
      const bool can_hu =
          sub == "claim" ? t->SeatCanHu(i) : (sub == "discard" && i == t->turn_seat() && t->can_hu());
      ctx_.send(uid, MsgId::kS2C_PhzTurn,
                proto_wire::EncodeS2C_PhzTurn(turn, sub, timeout_s, t->wall_remain(), sync, can_hu));
    }
    ArmDeadline();
    return;
  }

  if (ev.type == "Draw") {
    for (int i = 0; i < ctx_.seat_count; ++i) {
      const int64_t uid = ctx_.uid_of ? ctx_.uid_of(i) : 0;
      if (!uid || !ctx_.send) continue;
      const int tile = (i == ev.seat) ? ev.tile : -1;
      ctx_.send(uid, MsgId::kS2C_PhzDraw, proto_wire::EncodeS2C_PhzDraw(ev.seat, tile));
    }
    return;
  }

  if (ev.type == "Reveal") {
    Broadcast(ctx_, MsgId::kS2C_PhzReveal, proto_wire::EncodeS2C_PhzReveal(ev.seat, ev.tile));
    return;
  }

  if (ev.type == "Discard") {
    Broadcast(ctx_, MsgId::kS2C_PhzDiscardBroadcast, proto_wire::EncodeS2C_PhzDiscardBroadcast(ev.seat, ev.tile));
    return;
  }

  if (ev.type == "Chi" || ev.type == "Peng" || ev.type == "Wei" || ev.type == "Pao" || ev.type == "Ti") {
    const int act = PhzActionCode(ev.type);
    const int meld_kind = ev.meld_kind ? ev.meld_kind : act;
    Broadcast(ctx_, MsgId::kS2C_PhzActionBroadcast,
              proto_wire::EncodeS2C_PhzActionBroadcast(ev.seat, act, ev.tile, ev.tiles, ev.from_seat, meld_kind));
    return;
  }

  if (ev.type == "Settle") {
    ApplySettle();
    return;
  }

  if (ev.type == "LiuJu") {
    if (ctx_.carry_banker) *ctx_.carry_banker = t->banker_seat();
    Broadcast(ctx_, MsgId::kS2C_PhzLiuJu, proto_wire::EncodeS2C_PhzLiuJu(t->banker_seat()));
    LogRound(round_id_, "server", "phz", ctx_.room_id, uid_of(t->banker_seat()), t->banker_seat(), "liuju", "");
    FinishRound();
  }
}

void PhzRoomGame::ApplySettle() {
  if (!table_) return;
  auto* t = table_.get();
  auto plan = t->last_settle();
  auto deltas = plan.deltas;
  int winner = plan.winner_seat;
  if (winner >= 0 && winner < phz::kSeats && ctx_.cfg.rake_bp > 0 && deltas[static_cast<size_t>(winner)] > 0) {
    const int64_t rake = deltas[static_cast<size_t>(winner)] * ctx_.cfg.rake_bp / 10000;
    deltas[static_cast<size_t>(winner)] -= rake;
  }
  std::vector<proto_wire::SettleEntry> entries;
  nlohmann::json players = nlohmann::json::array();
  for (int i = 0; i < ctx_.seat_count && i < phz::kSeats; ++i) {
    const int64_t uid = ctx_.uid_of ? ctx_.uid_of(i) : 0;
    const int64_t d = deltas[static_cast<size_t>(i)];
    if (uid && ctx_.wallet) {
      ctx_.wallet->Adjust(uid, Currency::kGold, d, "game_settle", phz::IdemSettleKey(round_id_, uid));
    }
    proto_wire::SettleEntry e;
    e.uid = uid;
    e.seat_id = i;
    e.delta_gold = d;
    entries.push_back(e);
    players.push_back({{"uid", uid},
                       {"seat_id", i},
                       {"delta", d},
                       {"hu_xi", plan.hu_xi},
                       {"tun", plan.tun},
                       {"fan", plan.fan},
                       {"ming_tang_mask", plan.ming_tang_mask}});
  }
  if (ctx_.carry_banker) *ctx_.carry_banker = t->banker_seat();
  const int32_t hu_tile = t->last_hu_tile() == phz::kTileInvalid ? -1 : static_cast<int32_t>(t->last_hu_tile());
  Broadcast(ctx_, MsgId::kS2C_PhzSettle,
            proto_wire::EncodeS2C_PhzSettle(round_id_, winner, hu_tile, t->last_hu_draw(), plan.hu_xi, plan.tun,
                                            plan.fan, plan.ming_tang_mask, ctx_.cfg.base_score, entries));
  LogRound(round_id_, "server", "phz", ctx_.room_id, winner >= 0 && ctx_.uid_of ? ctx_.uid_of(winner) : 0, winner,
           "settle", "xi=" + std::to_string(plan.hu_xi) + " tun=" + std::to_string(plan.tun) +
                         " fan=" + std::to_string(plan.fan));
  if (ctx_.admin) {
    ctx_.admin->RecordRound(round_id_, ctx_.room_id, ctx_.template_id, players.dump(), ctx_.cfg.base_score, plan.fan,
                            GameId::kPhz);
  }
  if (ctx_.social) {
    try {
      ctx_.social->OnRoundSettled(round_id_, ctx_.template_id, players.dump(), ctx_.cfg.base_score, plan.fan);
    } catch (...) {
    }
  }
  if (ctx_.activity) {
    for (int i = 0; i < ctx_.seat_count; ++i) {
      const int64_t uid = ctx_.uid_of ? ctx_.uid_of(i) : 0;
      if (!uid) continue;
      try {
        ctx_.activity->OnGameSettled(uid, ctx_.template_id);
      } catch (...) {
      }
    }
  }
  FinishRound();
}

void PhzRoomGame::FinishRound() {
  done_ = true;
  table_.reset();
  if (ctx_.on_round_finished) ctx_.on_round_finished();
}

void PhzRoomGame::Tick(std::chrono::steady_clock::time_point now) {
  if (!table_ || done_) return;
  auto* t = table_.get();
  if (t->phase() != phz::Phase::kPlay) return;

  auto maybe_timeout = [&](int seat) {
    if (seat < 0 || seat >= ctx_.seat_count) return;
    if (now >= deadline_) {
      const int64_t uid = ctx_.uid_of ? ctx_.uid_of(seat) : 0;
      LogRound(round_id_, "server", "phz", ctx_.room_id, uid, seat, "timeout",
               t->sub() == phz::PlaySub::kClaimWindow ? "claim" : "discard");
      t->OnTimeout(seat);
    }
  };

  if (t->sub() == phz::PlaySub::kClaimWindow) {
    for (int i = 0; i < ctx_.seat_count; ++i) {
      if (t->SeatNeedsClaimInput(i)) maybe_timeout(i);
      if (!table_ || done_) return;
    }
  } else {
    maybe_timeout(t->turn_seat());
  }
}

bool PhzRoomGame::Handle(int64_t uid, uint32_t msg_id, const uint8_t* body, size_t len) {
  if (!table_ || done_) return false;
  const int seat = ctx_.seat_of ? ctx_.seat_of(uid) : -1;
  if (seat < 0) return false;

  if (msg_id == MsgId::kC2S_PhzDiscard) {
    int32_t tile = 0;
    proto_wire::DecodeC2S_PhzDiscard(body, len, tile);
    if (!table_->OnDiscard(seat, tile)) {
      SendError(ctx_, uid, Err::kPhzIllegalDiscard, MsgId::kC2S_PhzDiscard);
    }
    return true;
  }

  if (msg_id == MsgId::kC2S_PhzAction) {
    int32_t action = 0;
    std::vector<int32_t> chi_hand;
    proto_wire::DecodeC2S_PhzAction(body, len, action, chi_hand);
    phz::ActionKind act = phz::ActionKind::kPass;
    if (action == 1) act = phz::ActionKind::kChi;
    else if (action == 2) act = phz::ActionKind::kPeng;
    else if (action == 4) act = phz::ActionKind::kHu;
    phz::ChiOption chi;
    if (chi_hand.size() >= 2) {
      chi.hand_tiles = {chi_hand[0], chi_hand[1]};
    }
    if (!table_->OnAction(seat, act, chi_hand.size() >= 2 ? &chi : nullptr)) {
      SendError(ctx_, uid, Err::kPhzIllegalAction, MsgId::kC2S_PhzAction);
    }
    return true;
  }

  return false;
}

void PhzRoomGame::OnDisconnect(int64_t uid) {
  if (!table_ || done_) return;
  const int seat = ctx_.seat_of ? ctx_.seat_of(uid) : -1;
  if (seat < 0) return;
  LogRound(round_id_, "server", "phz", ctx_.room_id, uid, seat, "disconnect",
           table_->sub() == phz::PlaySub::kClaimWindow ? "claim" : "play");
  if (table_->phase() == phz::Phase::kPlay && table_->sub() == phz::PlaySub::kClaimWindow &&
      table_->SeatNeedsClaimInput(seat)) {
    table_->OnTimeout(seat);
  }
}

void PhzRoomGame::OnReconnect(int64_t uid) {
  if (!table_ || done_) return;
  const int seat = ctx_.seat_of ? ctx_.seat_of(uid) : -1;
  if (seat < 0 || !ctx_.send) return;
  auto* t = table_.get();
  auto hand = PhzHandToList(t->seat(seat).hand);
  ctx_.send(uid, MsgId::kS2C_PhzGameStart,
            proto_wire::EncodeS2C_PhzGameStart(round_id_, ctx_.room_id, ctx_.template_id, t->banker_seat(), hand,
                                                t->wall_remain(), seat, ctx_.cfg.base_score,
                                                phz::ConfigSnapshotJson(phz::PhzConfig{})));
  for (int s = 0; s < phz::kSeats; ++s) {
    for (const auto& meld : t->seat(s).melds) {
      const int meld_kind = static_cast<int>(meld.kind);
      int act = meld_kind;
      if (meld.kind == phz::MeldKind::kChi) act = 1;
      else if (meld.kind == phz::MeldKind::kPeng) act = 2;
      else if (meld.kind == phz::MeldKind::kWei || meld.kind == phz::MeldKind::kChouWei) act = 3;
      else if (meld.kind == phz::MeldKind::kPao) act = 5;
      else if (meld.kind == phz::MeldKind::kTi) act = 6;
      ctx_.send(uid, MsgId::kS2C_PhzActionBroadcast,
                proto_wire::EncodeS2C_PhzActionBroadcast(s, act, meld.tile, meld.tiles, meld.from_seat, meld_kind));
    }
  }
  if (t->last_discard() != phz::kTileInvalid && t->last_discard_seat() >= 0) {
    ctx_.send(uid, MsgId::kS2C_PhzDiscardBroadcast,
              proto_wire::EncodeS2C_PhzDiscardBroadcast(t->last_discard_seat(), t->last_discard()));
  }
  if (t->last_reveal() != phz::kTileInvalid && t->last_reveal_seat() >= 0 && t->sub() == phz::PlaySub::kClaimWindow) {
    ctx_.send(uid, MsgId::kS2C_PhzReveal, proto_wire::EncodeS2C_PhzReveal(t->last_reveal_seat(), t->last_reveal()));
  }
  std::string sub = "discard";
  int turn = t->turn_seat();
  if (t->sub() == phz::PlaySub::kClaimWindow) {
    sub = "claim";
    turn = t->has_pending_reveal() ? t->last_reveal_seat() : t->last_discard_seat();
  }
  std::vector<int32_t> sync_hand;
  if (sub == "discard" && seat == turn) sync_hand = hand;
  const int left = SecondsLeft(deadline_);
  const bool can_hu = sub == "claim" ? t->SeatCanHu(seat) : (sub == "discard" && seat == turn && t->can_hu());
  LogRound(round_id_, "server", "phz", ctx_.room_id, uid, seat, "reconnect", "sub=" + sub + " left=" + std::to_string(left));
  ctx_.send(uid, MsgId::kS2C_PhzTurn,
            proto_wire::EncodeS2C_PhzTurn(turn, sub, left, t->wall_remain(), sync_hand, can_hu));
}

bool PhzRoomGame::InProgress() const { return table_ && !done_; }

}  // namespace pandora
