#include "game/hzmj/hzmj_room_game.hpp"

#include <sstream>

#include <nlohmann/json.hpp>

#include "activity/activity_service.hpp"
#include "admin/admin_service.hpp"
#include "common/errors.hpp"
#include "common/log.hpp"
#include "common/proto_wire.hpp"
#include "game/game_ids.hpp"
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

std::vector<int32_t> HandToList(const hzmj::HandCount& h) {
  std::vector<int32_t> out;
  for (int t = 0; t < hzmj::kTileKinds; ++t) {
    for (int n = 0; n < h[static_cast<size_t>(t)]; ++n) out.push_back(t);
  }
  return out;
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

HzmjRoomGame::HzmjRoomGame(RoomCtx& ctx) : ctx_(ctx) {}

void HzmjRoomGame::Start() {
  std::array<int64_t, 4> uids{};
  for (int i = 0; i < ctx_.seat_count; ++i) uids[static_cast<size_t>(i)] = ctx_.uid_of ? ctx_.uid_of(i) : 0;

  int banker = ctx_.carry_banker ? *ctx_.carry_banker : 0;
  int lian = ctx_.carry_lian ? *ctx_.carry_lian : 1;

  hzmj::HzmjConfig hcfg;
  hcfg.base_score = ctx_.cfg.base_score;
  if (ctx_.cfg.hzmj_debug_deal) {
    hcfg.start_as_sanlao = true;
    banker = 1;
  }

  table_ = std::make_unique<hzmj::HzmjTable>(hcfg, uids, banker, lian);
  round_id_ = NowMs();
  done_ = false;
  if (ctx_.set_phase) ctx_.set_phase("Deal");

  table_->SetSink([this](const hzmj::OutEvent& ev) { OnEvent(ev); });

  if (ctx_.cfg.hzmj_debug_deal) {
    table_->ApplyDebugDianpaoDeal();
    if (ctx_.carry_banker) *ctx_.carry_banker = table_->banker_seat();
    PLOG_INFO("hzmj debug deal: seat0 ting Dong, banker=1 discards Dong first, N=8");
  }

  if (ctx_.push_room_state) ctx_.push_room_state();
  table_->Start();
  if (table_->phase() == hzmj::Phase::kPlay && ctx_.set_phase) ctx_.set_phase("Play");
  ArmDeadline();
}

void HzmjRoomGame::ArmDeadline() {
  deadline_ = std::chrono::steady_clock::now() + std::chrono::seconds(ctx_.cfg.play_timeout_s);
}

void HzmjRoomGame::OnEvent(const hzmj::OutEvent& ev) {
  if (!table_) return;
  auto* t = table_.get();
  const int timeout_s = ctx_.cfg.play_timeout_s;
  auto uid_of = [&](int seat) -> int64_t {
    if (seat < 0 || seat >= ctx_.seat_count) return 0;
    return ctx_.uid_of ? ctx_.uid_of(seat) : 0;
  };

  if (ev.type == "GameStart") {
    const int N = t->CurrentN();
    std::ostringstream hands;
    hands << "banker=" << t->banker_seat() << " N=" << N << " lian=" << t->lian_zhuang() << " wall=" << t->wall_remain();
    for (int i = 0; i < ctx_.seat_count; ++i) {
      hands << " hand" << i << "=" << JoinIds(HandToList(t->seat(i).hand));
    }
    LogRound(round_id_, "server", "hzmj", ctx_.room_id, uid_of(t->banker_seat()), t->banker_seat(), "start", hands.str());
    std::vector<int32_t> caishen{hzmj::kBai};
    for (int i = 0; i < ctx_.seat_count; ++i) {
      const int64_t uid = ctx_.uid_of ? ctx_.uid_of(i) : 0;
      if (!uid || !ctx_.send) continue;
      auto hand = HandToList(t->seat(i).hand);
      auto body = proto_wire::EncodeS2C_HzmjGameStart(round_id_, ctx_.room_id, ctx_.template_id, t->banker_seat(),
                                                     t->lian_zhuang(), N, caishen, hand, t->wall_remain(), i,
                                                     ctx_.cfg.base_score);
      ctx_.send(uid, MsgId::kS2C_HzmjGameStart, body);
    }
    if (ctx_.set_phase) ctx_.set_phase("Play");
    ArmDeadline();
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
      LogRound(round_id_, "server", "hzmj", ctx_.room_id, uid_of(ev.seat), ev.seat,
               ev.type == "Turn" ? "turn" : sub, detail.str());
    }
    ArmDeadline();
    for (int i = 0; i < ctx_.seat_count; ++i) {
      const int64_t uid = ctx_.uid_of ? ctx_.uid_of(i) : 0;
      if (!uid || !ctx_.send) continue;
      if (ev.type == "ClaimWindow" && !t->SeatNeedsClaimInput(i)) continue;
      std::vector<int32_t> sync_hand;
      if (sub == "discard" && i == ev.seat) sync_hand = HandToList(t->seat(i).hand);
      const bool can_zimo = (sub == "discard" || sub == "piao") && t->can_zimo();
      auto body =
          proto_wire::EncodeS2C_HzmjTurn(ev.seat, sub, timeout_s, t->wall_remain(), piao, sync_hand, can_zimo);
      ctx_.send(uid, MsgId::kS2C_HzmjTurn, body);
    }
    return;
  }

  if (ev.type == "Draw") {
    {
      std::ostringstream detail;
      detail << "tile=" << static_cast<int>(ev.tile) << " wall=" << t->wall_remain();
      LogRound(round_id_, "server", "hzmj", ctx_.room_id, uid_of(ev.seat), ev.seat, "draw", detail.str());
    }
    for (int i = 0; i < ctx_.seat_count; ++i) {
      const int64_t uid = ctx_.uid_of ? ctx_.uid_of(i) : 0;
      if (!uid || !ctx_.send) continue;
      const int32_t tile = (i == ev.seat) ? static_cast<int32_t>(ev.tile) : -1;
      ctx_.send(uid, MsgId::kS2C_HzmjDraw, proto_wire::EncodeS2C_HzmjDraw(ev.seat, tile));
    }
    return;
  }

  if (ev.type == "Discard") {
    {
      std::ostringstream detail;
      detail << "tile=" << static_cast<int>(ev.tile);
      LogRound(round_id_, "server", "hzmj", ctx_.room_id, uid_of(ev.seat), ev.seat, "discard", detail.str());
    }
    Broadcast(ctx_, MsgId::kS2C_HzmjDiscardBroadcast, proto_wire::EncodeS2C_HzmjDiscardBroadcast(ev.seat, ev.tile));
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
      LogRound(round_id_, "server", "hzmj", ctx_.room_id, uid_of(ev.seat), ev.seat, ev.type, detail.str());
    }
    Broadcast(ctx_, MsgId::kS2C_HzmjActionBroadcast,
              proto_wire::EncodeS2C_HzmjActionBroadcast(ev.seat, act, ev.tile, tiles, ev.from_seat, meld_kind));
    ArmDeadline();
    return;
  }

  if (ev.type == "GangScore") {
    {
      std::ostringstream detail;
      detail << "kind=" << ev.detail;
      for (int i = 0; i < ctx_.seat_count; ++i) detail << " d" << i << "=" << ev.deltas[static_cast<size_t>(i)];
      LogRound(round_id_, "server", "hzmj", ctx_.room_id, uid_of(ev.seat), ev.seat, "gang_score", detail.str());
    }
    if (ctx_.wallet) {
      for (int i = 0; i < ctx_.seat_count; ++i) {
        const int64_t uid = ctx_.uid_of ? ctx_.uid_of(i) : 0;
        const int64_t d = ev.deltas[static_cast<size_t>(i)];
        if (!uid || d == 0) continue;
        const std::string key =
            "hzmj:gang:" + std::to_string(round_id_) + ":" + ev.detail + ":" + std::to_string(uid);
        ctx_.wallet->Adjust(uid, Currency::kGold, d, "hzmj_gang", key);
      }
    }
    return;
  }

  if (ev.type == "Settle") {
    ApplySettle();
    return;
  }

  if (ev.type == "LiuJu") {
    {
      std::ostringstream detail;
      detail << "lian=" << t->lian_zhuang();
      LogRound(round_id_, "server", "hzmj", ctx_.room_id, 0, -1, "liuju", detail.str());
    }
    if (ctx_.carry_banker) *ctx_.carry_banker = t->banker_seat();
    if (ctx_.carry_lian) *ctx_.carry_lian = t->lian_zhuang();
    Broadcast(ctx_, MsgId::kS2C_HzmjLiuJu, proto_wire::EncodeS2C_HzmjLiuJu(t->lian_zhuang()));
    FinishRound();
  }
}

void HzmjRoomGame::ApplySettle() {
  if (!table_) return;
  auto* t = table_.get();
  auto plan = t->last_settle();
  std::array<int64_t, 4> deltas = plan.deltas;

  for (int i = 0; i < ctx_.seat_count; ++i) {
    if (deltas[static_cast<size_t>(i)] > 0) {
      const int64_t rake = deltas[static_cast<size_t>(i)] * ctx_.cfg.rake_bp / 10000;
      deltas[static_cast<size_t>(i)] -= rake;
    }
  }

  int M = t->last_hu_M();
  int N = t->last_hu_N();
  if (M < 1) M = 1;
  if (N < 1) N = 2;

  std::vector<proto_wire::SettleEntry> entries;
  nlohmann::json players = nlohmann::json::array();
  for (int i = 0; i < ctx_.seat_count; ++i) {
    const int64_t uid = ctx_.uid_of ? ctx_.uid_of(i) : 0;
    const int64_t d = deltas[static_cast<size_t>(i)];
    if (uid && ctx_.wallet) {
      const std::string key = hzmj::IdemSettleKey(round_id_, uid);
      ctx_.wallet->Adjust(uid, Currency::kGold, d, "game_settle", key);
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
    for (int i = 1; i < ctx_.seat_count; ++i) {
      if (deltas[static_cast<size_t>(i)] > deltas[static_cast<size_t>(w)]) w = i;
    }
    return w;
  }();

  if (ctx_.carry_banker) *ctx_.carry_banker = t->banker_seat();
  if (ctx_.carry_lian) *ctx_.carry_lian = t->lian_zhuang();

  const int32_t hu_tile = t->last_hu_tile() == hzmj::kTileInvalid ? -1 : static_cast<int32_t>(t->last_hu_tile());
  {
    std::ostringstream detail;
    detail << "winner=" << winner << " hu=" << hu_tile << " zimo=" << (t->last_hu_zimo() ? 1 : 0)
           << " shooter=" << t->last_shooter_seat() << " M=" << M << " N=" << N << " contractor=" << plan.contractor_seat;
    for (int i = 0; i < ctx_.seat_count; ++i) detail << " d" << i << "=" << deltas[static_cast<size_t>(i)];
    const int64_t winner_uid = (winner >= 0 && winner < ctx_.seat_count && ctx_.uid_of) ? ctx_.uid_of(winner) : 0;
    LogRound(round_id_, "server", "hzmj", ctx_.room_id, winner_uid, winner, "settle", detail.str());
  }
  Broadcast(ctx_, MsgId::kS2C_HzmjSettle,
            proto_wire::EncodeS2C_HzmjSettle(round_id_, winner, hu_tile, t->last_hu_zimo(), t->last_shooter_seat(), M,
                                             N, plan.contractor_seat, ctx_.cfg.base_score, entries));

  const std::string players_json = players.dump();
  if (ctx_.admin) {
    ctx_.admin->RecordRound(round_id_, ctx_.room_id, ctx_.template_id, players_json, ctx_.cfg.base_score, M * N,
                            GameId::kHzmj);
  }
  if (ctx_.social) {
    try {
      ctx_.social->OnRoundSettled(round_id_, ctx_.template_id, players_json, ctx_.cfg.base_score, M * N);
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

void HzmjRoomGame::FinishRound() {
  done_ = true;
  table_.reset();
  if (ctx_.on_round_finished) ctx_.on_round_finished();
}

void HzmjRoomGame::Tick(std::chrono::steady_clock::time_point now) {
  if (!table_ || done_) return;
  auto* t = table_.get();
  if (t->phase() != hzmj::Phase::kPlay) return;

  auto maybe_timeout = [&](int seat) {
    if (seat < 0 || seat >= ctx_.seat_count) return;
    if (now >= deadline_) {
      const char* sub = t->sub() == hzmj::PlaySub::kClaimWindow ? "claim" : "discard";
      const int64_t uid = ctx_.uid_of ? ctx_.uid_of(seat) : 0;
      LogRound(round_id_, "server", "hzmj", ctx_.room_id, uid, seat, "timeout", sub);
      t->OnTimeout(seat);
    }
  };

  if (t->sub() == hzmj::PlaySub::kClaimWindow) {
    for (int i = 0; i < ctx_.seat_count; ++i) {
      if (i == t->last_discard_seat()) continue;
      maybe_timeout(i);
      if (!table_ || done_) return;
    }
  } else {
    maybe_timeout(t->turn_seat());
  }
}

bool HzmjRoomGame::Handle(int64_t uid, uint32_t msg_id, const uint8_t* body, size_t len) {
  if (!table_ || done_) return false;
  const int seat = ctx_.seat_of ? ctx_.seat_of(uid) : -1;
  if (seat < 0) return false;

  if (msg_id == MsgId::kC2S_HzmjDiscard) {
    int32_t tile = 0;
    proto_wire::DecodeC2S_HzmjDiscard(body, len, tile);
    {
      std::ostringstream detail;
      detail << "tile=" << tile;
      LogRound(round_id_, "server", "hzmj", ctx_.room_id, uid, seat, "cmd_discard", detail.str());
    }
    if (!table_->OnDiscard(seat, tile)) {
      SendError(ctx_, uid, Err::kHzmjIllegalDiscard, MsgId::kC2S_HzmjDiscard);
    }
    return true;
  }

  if (msg_id == MsgId::kC2S_HzmjAction) {
    int32_t action = 0;
    std::vector<int32_t> chi_hand;
    proto_wire::DecodeC2S_HzmjAction(body, len, action, chi_hand);
    {
      std::ostringstream detail;
      detail << "action=" << action << " chi=" << JoinIds(chi_hand);
      LogRound(round_id_, "server", "hzmj", ctx_.room_id, uid, seat, "cmd_action", detail.str());
    }
    hzmj::ActionKind act = hzmj::ActionKind::kPass;
    if (action >= 0 && action <= 4) act = static_cast<hzmj::ActionKind>(action);
    if (act == hzmj::ActionKind::kHu && table_->sub() == hzmj::PlaySub::kDiscard) {
      if (!table_->OnZimoHu(seat)) {
        const char* msg = table_->can_zimo() ? "cannot zimo hu" : "must discard after chi/peng";
        ctx_.send(uid, MsgId::kS2C_Error,
                  proto_wire::EncodeS2C_Error(static_cast<int>(Err::kHzmjIllegalAction), msg, MsgId::kC2S_HzmjAction));
      }
      return true;
    }
    hzmj::ChiOption chi{};
    const hzmj::ChiOption* chi_ptr = nullptr;
    if (act == hzmj::ActionKind::kChi && chi_hand.size() >= 2) {
      chi.hand_tiles[0] = chi_hand[0];
      chi.hand_tiles[1] = chi_hand[1];
      chi_ptr = &chi;
    }
    if (!table_->OnAction(seat, act, chi_ptr)) {
      SendError(ctx_, uid, Err::kHzmjIllegalAction, MsgId::kC2S_HzmjAction);
    }
    return true;
  }

  if (msg_id == MsgId::kC2S_HzmjGang) {
    int32_t kind = 0;
    int32_t tile = 0;
    proto_wire::DecodeC2S_HzmjGang(body, len, kind, tile);
    {
      std::ostringstream detail;
      detail << "kind=" << kind << " tile=" << tile;
      LogRound(round_id_, "server", "hzmj", ctx_.room_id, uid, seat, "cmd_gang", detail.str());
    }
    if (kind == 0) table_->OnAnGang(seat, tile);
    else table_->OnBuGang(seat, tile);
    return true;
  }

  return false;
}

void HzmjRoomGame::OnDisconnect(int64_t uid) {
  if (!table_ || done_) return;
  const int seat = ctx_.seat_of ? ctx_.seat_of(uid) : -1;
  if (seat < 0) return;
  LogRound(round_id_, "server", "hzmj", ctx_.room_id, uid, seat, "disconnect",
           table_->sub() == hzmj::PlaySub::kClaimWindow ? "claim" : "play");
  if (table_->phase() == hzmj::Phase::kPlay && table_->sub() == hzmj::PlaySub::kClaimWindow &&
      seat != table_->last_discard_seat()) {
    table_->OnTimeout(seat);
  }
}

void HzmjRoomGame::OnReconnect(int64_t uid) {
  if (!table_ || done_) return;
  const int seat = ctx_.seat_of ? ctx_.seat_of(uid) : -1;
  if (seat < 0 || !ctx_.send) return;
  auto* t = table_.get();
  const int N = t->CurrentN();
  std::vector<int32_t> caishen{hzmj::kBai};
  auto hand = HandToList(t->seat(seat).hand);
  ctx_.send(uid, MsgId::kS2C_HzmjGameStart,
            proto_wire::EncodeS2C_HzmjGameStart(round_id_, ctx_.room_id, ctx_.template_id, t->banker_seat(),
                                                t->lian_zhuang(), N, caishen, hand, t->wall_remain(), seat,
                                                ctx_.cfg.base_score));
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
      ctx_.send(uid, MsgId::kS2C_HzmjActionBroadcast,
                proto_wire::EncodeS2C_HzmjActionBroadcast(s, act, meld.tile, tiles, meld.from_seat, meld_kind));
    }
  }
  if (t->last_discard() != hzmj::kTileInvalid && t->last_discard_seat() >= 0) {
    ctx_.send(uid, MsgId::kS2C_HzmjDiscardBroadcast,
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
  const int left = SecondsLeft(deadline_);
  {
    std::ostringstream detail;
    detail << "sub=" << sub << " turn=" << turn << " left=" << left << " wall=" << t->wall_remain()
           << " hand=" << JoinIds(hand);
    LogRound(round_id_, "server", "hzmj", ctx_.room_id, uid, seat, "reconnect", detail.str());
  }
  const bool can_zimo = (sub == "discard" || sub == "piao") && t->can_zimo();
  ctx_.send(uid, MsgId::kS2C_HzmjTurn,
            proto_wire::EncodeS2C_HzmjTurn(turn, sub, left, t->wall_remain(), piao, sync_hand, can_zimo));
}

bool HzmjRoomGame::InProgress() const { return table_ && !done_; }

}  // namespace pandora
