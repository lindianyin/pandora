#include "game/biji/biji_room_game.hpp"

#include <sstream>

#include "common/errors.hpp"
#include "common/log.hpp"
#include "common/proto_wire.hpp"
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

int PhaseInt(biji::Phase p) {
  switch (p) {
    case biji::Phase::kWaitReady:
      return 0;
    case biji::Phase::kDeal:
      return 1;
    case biji::Phase::kArrange:
      return 2;
    case biji::Phase::kCompare:
      return 3;
    case biji::Phase::kSettle:
      return 4;
  }
  return 0;
}

proto_wire::BijiDunData MakeDun(const biji::DunCards& cards, int type, int place, int64_t delta) {
  proto_wire::BijiDunData d;
  d.cards = {cards[0], cards[1], cards[2]};
  d.type = type;
  d.place = place;
  d.delta = delta;
  return d;
}

}  // namespace

BijiRoomGame::BijiRoomGame(RoomCtx& ctx) : ctx_(ctx) {}

int64_t BijiRoomGame::NowMs() const {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

int64_t BijiRoomGame::SeatGold(int64_t uid) const {
  if (!ctx_.store || !uid) return 0;
  auto p = ctx_.store->GetPlayer(uid);
  return p ? p->gold : 0;
}

void BijiRoomGame::Start() {
  biji::BijiConfig cfg = biji::DefaultConfig(ctx_.seat_count);
  cfg.base_score = ctx_.cfg.base_score > 0 ? ctx_.cfg.base_score : cfg.base_score;
  cfg.rake_bp = ctx_.cfg.rake_bp;
  cfg.players = ctx_.seat_count;
  if (ctx_.cfg.play_timeout_s > 0) cfg.arrange_timeout_s = ctx_.cfg.play_timeout_s;

  table_ = std::make_unique<biji::BijiTable>(cfg, [this]() { return static_cast<uint32_t>(rng_()); });
  for (int i = 0; i < ctx_.seat_count; ++i) {
    table_->SetSeatUid(i, ctx_.uid_of ? ctx_.uid_of(i) : 0);
    table_->OnReady(i, true);
  }
  round_id_ = NowMs();
  done_ = false;
  has_last_compare_ = false;
  if (ctx_.set_phase) ctx_.set_phase("Arrange");
  table_->TryStartDeal(NowMs());
  BroadcastStart();
  BroadcastArrangeState();
  if (ctx_.push_room_state) ctx_.push_room_state();
  LogRound(round_id_, "server", "biji", ctx_.room_id, ctx_.uid_of ? ctx_.uid_of(0) : 0, 0, "deal",
           "n=" + std::to_string(ctx_.seat_count));
}

void BijiRoomGame::BroadcastStart() {
  if (!table_ || !ctx_.send) return;
  for (int i = 0; i < ctx_.seat_count; ++i) {
    const int64_t uid = ctx_.uid_of ? ctx_.uid_of(i) : 0;
    if (!uid) continue;
    proto_wire::BijiGameStartData data;
    data.round_id = round_id_;
    data.room_id = ctx_.room_id;
    data.template_id = ctx_.template_id;
    data.self_seat = i;
    data.players = table_->n();
    data.deal_start = table_->deal_start();
    data.base_score = static_cast<int32_t>(ctx_.cfg.base_score);
    data.arrange_timeout_s = table_->cfg().arrange_timeout_s;
    data.enable_chixi = table_->cfg().enable_chixi;
    for (auto c : table_->seat(i).hand) data.hand.push_back(c);
    ctx_.send(uid, MsgId::kS2C_BijiGameStart, proto_wire::EncodeS2C_BijiGameStart(data));
  }
}

void BijiRoomGame::BroadcastArrangeState() {
  if (!table_) return;
  proto_wire::BijiArrangeStateData st;
  const int64_t now = NowMs();
  int64_t remain = 0;
  if (table_->arrange_deadline_ms() > now) remain = table_->arrange_deadline_ms() - now;
  st.remain_s = static_cast<int32_t>((remain + 999) / 1000);
  for (int i = 0; i < table_->n(); ++i) {
    st.locked.push_back(table_->seat(i).locked);
    st.trusteeship.push_back(table_->seat(i).trusteeship);
  }
  Broadcast(ctx_, MsgId::kS2C_BijiArrangeState, proto_wire::EncodeS2C_BijiArrangeState(st));
}

void BijiRoomGame::ApplySettlementIfAny() {
  if (!table_ || done_) return;
  biji::SettlePlan plan;
  if (!table_->TakeSettlement(&plan)) return;
  last_plan_ = plan;
  has_last_compare_ = true;

  proto_wire::BijiCompareData cmp;
  cmp.round_id = round_id_;
  for (int i = 0; i < table_->n(); ++i) {
    proto_wire::BijiSeatCompareData s;
    s.seat_id = i;
    s.uid = plan.seats[static_cast<size_t>(i)].uid;
    const auto& seat = table_->seat(i);
    // After settle table cleared drafts; use plan places + reconstruct from plan only.
    // Final cards were cleared - need to keep them. Fix: read from plan isn't enough.
    // Use seat final_* before clear - but FinishCompare cleared locked/draft, not final_*.
    s.head = MakeDun(seat.final_head, biji::EvalDun(seat.final_head).type, plan.seats[static_cast<size_t>(i)].place[0],
                     plan.seats[static_cast<size_t>(i)].dun_delta[0]);
    s.mid = MakeDun(seat.final_mid, biji::EvalDun(seat.final_mid).type, plan.seats[static_cast<size_t>(i)].place[1],
                    plan.seats[static_cast<size_t>(i)].dun_delta[1]);
    s.tail = MakeDun(seat.final_tail, biji::EvalDun(seat.final_tail).type, plan.seats[static_cast<size_t>(i)].place[2],
                     plan.seats[static_cast<size_t>(i)].dun_delta[2]);
    cmp.seats.push_back(s);
  }
  Broadcast(ctx_, MsgId::kS2C_BijiCompare, proto_wire::EncodeS2C_BijiCompare(cmp));

  proto_wire::BijiSettleData settle;
  settle.round_id = round_id_;
  for (int i = 0; i < static_cast<int>(plan.seats.size()); ++i) {
    const auto& ps = plan.seats[static_cast<size_t>(i)];
    if (ctx_.wallet && ps.uid && ps.net != 0) {
      std::ostringstream key;
      key << "biji:settle:" << round_id_ << ":" << ps.uid;
      ctx_.wallet->Adjust(ps.uid, Currency::kGold, ps.net, "game_settle", key.str());
    }
    proto_wire::BijiSeatSettleData s;
    s.seat_id = i;
    s.uid = ps.uid;
    s.dun_delta_head = ps.dun_delta[0];
    s.dun_delta_mid = ps.dun_delta[1];
    s.dun_delta_tail = ps.dun_delta[2];
    s.chixi_delta = ps.chixi_delta;
    s.gross = ps.gross;
    s.rake = ps.rake;
    s.net = ps.net;
    s.gold = SeatGold(ps.uid);
    for (const auto& c : ps.chixi) {
      s.chixi.push_back({static_cast<int32_t>(c.type), c.mult});
    }
    settle.seats.push_back(s);
    LogRound(round_id_, "server", "biji", ctx_.room_id, ps.uid, i, "settle",
             "net=" + std::to_string(ps.net));
  }
  Broadcast(ctx_, MsgId::kS2C_BijiSettle, proto_wire::EncodeS2C_BijiSettle(settle));

  done_ = true;
  if (ctx_.set_phase) ctx_.set_phase("WaitReady");
  if (ctx_.on_round_finished) ctx_.on_round_finished();
}

void BijiRoomGame::Tick(std::chrono::steady_clock::time_point /*now*/) {
  if (!table_ || done_) return;
  if (table_->phase() == biji::Phase::kArrange) {
    const int64_t now = NowMs();
    if (now >= table_->arrange_deadline_ms()) {
      table_->OnArrangeDeadline(now);
      ApplySettlementIfAny();
    }
  }
}

bool BijiRoomGame::Handle(int64_t uid, uint32_t msg_id, const uint8_t* body, size_t len) {
  if (!table_ || done_) return false;
  if (msg_id != MsgId::kC2S_BijiArrange) return false;
  const int seat = ctx_.seat_of ? ctx_.seat_of(uid) : -1;
  if (seat < 0) {
    SendError(ctx_, uid, Err::kBijiBadSeat, msg_id);
    return true;
  }
  std::vector<int32_t> head, mid, tail;
  bool confirm = false;
  if (!proto_wire::DecodeC2S_BijiArrange(body, len, head, mid, tail, confirm) || head.size() != 3 ||
      mid.size() != 3 || tail.size() != 3) {
    SendError(ctx_, uid, Err::kBijiIllegalArrange, msg_id);
    return true;
  }
  biji::DunCards h{head[0], head[1], head[2]};
  biji::DunCards m{mid[0], mid[1], mid[2]};
  biji::DunCards t{tail[0], tail[1], tail[2]};
  const int err = table_->SetArrange(seat, h, m, t, confirm);
  if (err != 0) {
    SendError(ctx_, uid, static_cast<Err>(err), msg_id);
    if (ctx_.send) {
      ctx_.send(uid, MsgId::kS2C_BijiArrangeAck,
                proto_wire::EncodeS2C_BijiArrangeAck(err, ErrMessage(static_cast<Err>(err)), false));
    }
    return true;
  }
  // FinishCompare may clear seat.locked; Ack uses confirm intent when ok.
  if (ctx_.send) {
    ctx_.send(uid, MsgId::kS2C_BijiArrangeAck,
              proto_wire::EncodeS2C_BijiArrangeAck(0, "ok", confirm));
  }
  BroadcastArrangeState();
  LogRound(round_id_, "server", "biji", ctx_.room_id, uid, seat, confirm ? "confirm" : "arrange", "");
  ApplySettlementIfAny();
  return true;
}

void BijiRoomGame::OnDisconnect(int64_t uid) {
  if (!table_) return;
  const int seat = ctx_.seat_of ? ctx_.seat_of(uid) : -1;
  if (seat >= 0) {
    table_->OnDisconnect(seat);
    if (ctx_.set_trusteeship) ctx_.set_trusteeship(seat, true);
    BroadcastArrangeState();
  }
}

void BijiRoomGame::OnReconnect(int64_t uid) {
  if (!table_) return;
  const int seat = ctx_.seat_of ? ctx_.seat_of(uid) : -1;
  if (seat >= 0) {
    table_->OnReconnect(seat);
    if (ctx_.set_trusteeship) ctx_.set_trusteeship(seat, false);
  }
  SendSnapshot(uid);
}

void BijiRoomGame::SendSnapshot(int64_t uid) {
  if (!table_ || !ctx_.send) return;
  const int seat = ctx_.seat_of ? ctx_.seat_of(uid) : -1;
  if (seat < 0) return;
  const auto sn = table_->BuildSnapshot(seat, NowMs());
  proto_wire::BijiSnapshotData data;
  data.phase = PhaseInt(sn.phase);
  data.deal_start = sn.deal_start;
  data.remain_s = static_cast<int32_t>((sn.remain_ms + 999) / 1000);
  for (auto c : sn.hand) data.hand.push_back(c);
  if (sn.has_draft) {
    data.draft_head = {sn.draft_head[0], sn.draft_head[1], sn.draft_head[2]};
    data.draft_mid = {sn.draft_mid[0], sn.draft_mid[1], sn.draft_mid[2]};
    data.draft_tail = {sn.draft_tail[0], sn.draft_tail[1], sn.draft_tail[2]};
  }
  data.has_draft = sn.has_draft;
  data.locked = sn.locked;
  data.others_locked = sn.others_locked;
  ctx_.send(uid, MsgId::kS2C_BijiSnapshot, proto_wire::EncodeS2C_BijiSnapshot(data));
}

bool BijiRoomGame::InProgress() const { return table_ && !done_; }

}  // namespace pandora
