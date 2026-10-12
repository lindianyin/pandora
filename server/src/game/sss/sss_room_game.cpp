#include "game/sss/sss_room_game.hpp"

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

int PhaseInt(sss::Phase p) {
  switch (p) {
    case sss::Phase::kWaitReady:
      return 0;
    case sss::Phase::kDeal:
      return 1;
    case sss::Phase::kArrange:
      return 2;
    case sss::Phase::kCompare:
      return 3;
    case sss::Phase::kSettle:
      return 4;
  }
  return 0;
}

std::string FormatCards(const int32_t* cards, int n) {
  std::ostringstream os;
  for (int i = 0; i < n; ++i) {
    if (i) os << ',';
    os << cards[i];
  }
  return os.str();
}

std::string FormatHand13(const sss::Hand13& hand) { return FormatCards(hand.data(), 13); }

proto_wire::SssDunData MakeHeadDun(const sss::HeadCards& cards) {
  proto_wire::SssDunData d;
  d.cards = {cards[0], cards[1], cards[2]};
  d.type = static_cast<int32_t>(sss::EvalHead(cards).level);
  return d;
}

proto_wire::SssDunData MakeFiveDun(const sss::FiveCards& cards) {
  proto_wire::SssDunData d;
  for (auto c : cards) d.cards.push_back(c);
  d.type = static_cast<int32_t>(sss::EvalFive(cards).level);
  return d;
}

}  // namespace

SssRoomGame::SssRoomGame(RoomCtx& ctx) : ctx_(ctx) {}

int64_t SssRoomGame::NowMs() const {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

int64_t SssRoomGame::SeatGold(int64_t uid) const {
  if (!ctx_.store || !uid) return 0;
  auto p = ctx_.store->GetPlayer(uid);
  return p ? p->gold : 0;
}

void SssRoomGame::Start() {
  sss::SssConfig cfg = sss::DefaultConfig(ctx_.seat_count);
  cfg.base_score = ctx_.cfg.base_score > 0 ? ctx_.cfg.base_score : cfg.base_score;
  cfg.rake_bp = ctx_.cfg.rake_bp;
  cfg.players = ctx_.seat_count;
  if (ctx_.cfg.play_timeout_s > 0) cfg.arrange_timeout_s = ctx_.cfg.play_timeout_s;

  table_ = std::make_unique<sss::SssTable>(cfg, [this]() { return static_cast<uint32_t>(rng_()); });
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
  for (int i = 0; i < ctx_.seat_count; ++i) {
    const int64_t uid = ctx_.uid_of ? ctx_.uid_of(i) : 0;
    const auto det = sss::DetectSpecial(table_->seat(i).hand, table_->cfg().enable_special);
    LogRound(round_id_, "server", "sss", ctx_.room_id, uid, i, "deal",
             "n=" + std::to_string(ctx_.seat_count) + " special=" +
                 std::to_string(static_cast<int>(det)) + " hand=" + FormatHand13(table_->seat(i).hand));
  }
}

void SssRoomGame::BroadcastStart() {
  if (!table_ || !ctx_.send) return;
  for (int i = 0; i < ctx_.seat_count; ++i) {
    const int64_t uid = ctx_.uid_of ? ctx_.uid_of(i) : 0;
    if (!uid) continue;
    proto_wire::SssGameStartData data;
    data.round_id = round_id_;
    data.room_id = ctx_.room_id;
    data.template_id = ctx_.template_id;
    data.self_seat = i;
    data.players = table_->n();
    data.deal_start = table_->deal_start();
    data.base_score = static_cast<int32_t>(ctx_.cfg.base_score);
    data.arrange_timeout_s = table_->cfg().arrange_timeout_s;
    data.enable_special = table_->cfg().enable_special;
    data.enable_shoot = table_->cfg().enable_shoot;
    for (auto c : table_->seat(i).hand) data.hand.push_back(c);
    ctx_.send(uid, MsgId::kS2C_SssGameStart, proto_wire::EncodeS2C_SssGameStart(data));
  }
}

void SssRoomGame::BroadcastArrangeState() {
  if (!table_) return;
  proto_wire::SssArrangeStateData st;
  const int64_t now = NowMs();
  int64_t remain = 0;
  if (table_->arrange_deadline_ms() > now) remain = table_->arrange_deadline_ms() - now;
  st.remain_s = static_cast<int32_t>((remain + 999) / 1000);
  for (int i = 0; i < table_->n(); ++i) {
    const auto& seat = table_->seat(i);
    st.locked.push_back(seat.locked);
    st.trusteeship.push_back(seat.trusteeship);
    st.reported.push_back(seat.report_special);
  }
  Broadcast(ctx_, MsgId::kS2C_SssArrangeState, proto_wire::EncodeS2C_SssArrangeState(st));
}

void SssRoomGame::ApplySettlementIfAny() {
  if (!table_ || done_) return;
  sss::SettlePlan plan;
  if (!table_->TakeSettlement(&plan)) return;
  last_plan_ = plan;
  has_last_compare_ = true;

  proto_wire::SssCompareData cmp;
  cmp.round_id = round_id_;
  for (int i = 0; i < table_->n(); ++i) {
    const auto& seat = table_->seat(i);
    const auto& ps = plan.seats[static_cast<size_t>(i)];
    proto_wire::SssSeatCompareData s;
    s.seat_id = i;
    s.uid = ps.uid;
    s.head = MakeHeadDun(seat.final_head);
    s.mid = MakeFiveDun(seat.final_mid);
    s.tail = MakeFiveDun(seat.final_tail);
    s.special_type = static_cast<int32_t>(ps.special);
    s.home_run = ps.home_run;
    cmp.seats.push_back(s);
  }
  for (const auto& sh : plan.shoots) {
    cmp.shoots.push_back({sh.first, sh.second});
  }
  Broadcast(ctx_, MsgId::kS2C_SssCompare, proto_wire::EncodeS2C_SssCompare(cmp));

  proto_wire::SssSettleData settle;
  settle.round_id = round_id_;
  for (int i = 0; i < static_cast<int>(plan.seats.size()); ++i) {
    const auto& ps = plan.seats[static_cast<size_t>(i)];
    if (ctx_.wallet && ps.uid && ps.net != 0) {
      std::ostringstream key;
      key << "sss:settle:" << round_id_ << ":" << ps.uid;
      ctx_.wallet->Adjust(ps.uid, Currency::kGold, ps.net, "game_settle", key.str());
    }
    proto_wire::SssSeatSettleData s;
    s.seat_id = i;
    s.uid = ps.uid;
    s.gross_water = ps.gross_water;
    s.gross_gold = ps.gross_gold;
    s.rake = ps.rake;
    s.net = ps.net;
    s.gold = SeatGold(ps.uid);
    s.special_type = static_cast<int32_t>(ps.special);
    s.home_run = ps.home_run;
    settle.seats.push_back(s);
    LogRound(round_id_, "server", "sss", ctx_.room_id, ps.uid, i, "settle",
             "net=" + std::to_string(ps.net) + " water=" + std::to_string(ps.gross_water) +
                 " special=" + std::to_string(static_cast<int>(ps.special)) +
                 (ps.home_run ? " home_run=1" : ""));
  }
  Broadcast(ctx_, MsgId::kS2C_SssSettle, proto_wire::EncodeS2C_SssSettle(settle));

  done_ = true;
  if (ctx_.set_phase) ctx_.set_phase("WaitReady");
  if (ctx_.on_round_finished) ctx_.on_round_finished();
}

void SssRoomGame::Tick(std::chrono::steady_clock::time_point /*now*/) {
  if (!table_ || done_) return;
  if (table_->phase() == sss::Phase::kArrange) {
    const int64_t now = NowMs();
    if (now >= table_->arrange_deadline_ms()) {
      table_->OnArrangeDeadline(now);
      ApplySettlementIfAny();
    }
  }
}

bool SssRoomGame::Handle(int64_t uid, uint32_t msg_id, const uint8_t* body, size_t len) {
  if (!table_ || done_) return false;
  if (msg_id != MsgId::kC2S_SssArrange) return false;
  const int seat = ctx_.seat_of ? ctx_.seat_of(uid) : -1;
  if (seat < 0) {
    SendError(ctx_, uid, Err::kSssBadSeat, msg_id);
    return true;
  }
  std::vector<int32_t> head, mid, tail;
  bool report_special = false;
  bool confirm = false;
  if (!proto_wire::DecodeC2S_SssArrange(body, len, head, mid, tail, report_special, confirm) ||
      head.size() != 3 || mid.size() != 5 || tail.size() != 5) {
    SendError(ctx_, uid, Err::kSssIllegalArrange, msg_id);
    return true;
  }
  sss::HeadCards h{head[0], head[1], head[2]};
  sss::FiveCards m{};
  sss::FiveCards t{};
  for (int i = 0; i < 5; ++i) {
    m[static_cast<size_t>(i)] = mid[static_cast<size_t>(i)];
    t[static_cast<size_t>(i)] = tail[static_cast<size_t>(i)];
  }
  const int err = table_->SetArrange(seat, h, m, t, report_special, confirm);
  if (err != 0) {
    SendError(ctx_, uid, static_cast<Err>(err), msg_id);
    if (ctx_.send) {
      ctx_.send(uid, MsgId::kS2C_SssArrangeAck,
                proto_wire::EncodeS2C_SssArrangeAck(err, ErrMessage(static_cast<Err>(err)), false));
    }
    return true;
  }
  if (ctx_.send) {
    ctx_.send(uid, MsgId::kS2C_SssArrangeAck,
              proto_wire::EncodeS2C_SssArrangeAck(0, "ok", confirm));
  }
  BroadcastArrangeState();
  const auto& seat_state = table_->seat(seat);
  const char* ev = confirm ? (report_special ? "confirm_special" : "confirm")
                           : (report_special ? "special" : "arrange");
  std::ostringstream detail;
  detail << "report=" << (report_special ? 1 : 0)
         << " special=" << static_cast<int>(seat_state.special) << " locked=" << (confirm ? 1 : 0)
         << " head=" << FormatCards(head.data(), 3) << " mid=" << FormatCards(mid.data(), 5)
         << " tail=" << FormatCards(tail.data(), 5);
  LogRound(round_id_, "server", "sss", ctx_.room_id, uid, seat, ev, detail.str());
  ApplySettlementIfAny();
  return true;
}

void SssRoomGame::OnDisconnect(int64_t uid) {
  if (!table_) return;
  const int seat = ctx_.seat_of ? ctx_.seat_of(uid) : -1;
  if (seat >= 0) {
    table_->OnDisconnect(seat);
    if (ctx_.set_trusteeship) ctx_.set_trusteeship(seat, true);
    BroadcastArrangeState();
  }
}

void SssRoomGame::OnReconnect(int64_t uid) {
  if (!table_) return;
  const int seat = ctx_.seat_of ? ctx_.seat_of(uid) : -1;
  if (seat >= 0) {
    table_->OnReconnect(seat);
    if (ctx_.set_trusteeship) ctx_.set_trusteeship(seat, false);
  }
  SendSnapshot(uid);
}

void SssRoomGame::SendSnapshot(int64_t uid) {
  if (!table_ || !ctx_.send) return;
  const int seat = ctx_.seat_of ? ctx_.seat_of(uid) : -1;
  if (seat < 0) return;
  const auto sn = table_->BuildSnapshot(seat, NowMs());
  proto_wire::SssSnapshotData data;
  data.phase = PhaseInt(sn.phase);
  data.deal_start = sn.deal_start;
  data.remain_s = static_cast<int32_t>((sn.remain_ms + 999) / 1000);
  for (auto c : sn.hand) data.hand.push_back(c);
  if (sn.has_draft) {
    for (int i = 0; i < 3; ++i) data.draft_head.push_back(sn.draft_head[static_cast<size_t>(i)]);
    for (int i = 0; i < 5; ++i) data.draft_mid.push_back(sn.draft_mid[static_cast<size_t>(i)]);
    for (int i = 0; i < 5; ++i) data.draft_tail.push_back(sn.draft_tail[static_cast<size_t>(i)]);
  }
  data.has_draft = sn.has_draft;
  data.locked = sn.locked;
  data.report_special = sn.report_special;
  data.others_locked = sn.others_locked;
  data.others_reported = sn.others_reported;
  ctx_.send(uid, MsgId::kS2C_SssSnapshot, proto_wire::EncodeS2C_SssSnapshot(data));
}

bool SssRoomGame::InProgress() const { return table_ && !done_; }

}  // namespace pandora
