#include "game/ddz/ddz_room_game.hpp"

#include <algorithm>
#include <random>
#include <sstream>

#include <nlohmann/json.hpp>

#include "activity/activity_service.hpp"
#include "admin/admin_service.hpp"
#include "common/errors.hpp"
#include "common/log.hpp"
#include "common/proto_wire.hpp"
#include "common/send_error.hpp"
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

void SendToUid(RoomCtx& ctx, int64_t uid, uint32_t msg_id, const std::vector<uint8_t>& body) {
  if (ctx.send) ctx.send(uid, msg_id, body);
}

}  // namespace

DdzClassicSimple::DdzClassicSimple(RoomCtx& ctx, std::array<int64_t, 3> uids)
    : ctx_(ctx), room_id_(ctx.room_id), uids_(uids), cfg_(ctx.cfg) {
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
    SendToUid(ctx_, uids_[i], MsgId::kS2C_DdzGameStart,
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
  for (int64_t uid : AllUids()) SendToUid(ctx_, uid, MsgId::kS2C_DdzTurn, body);
  LogRound(round_id_, "server", "ddz", room_id_, uids_[current_seat_], current_seat_, "turn",
           "phase=" + phase_ + " timeout=" + std::to_string(timeout));
  deadline_ = std::chrono::steady_clock::now() + std::chrono::seconds(timeout);
}

void DdzClassicSimple::OnBid(int seat, int score) {
  if (finished_ || phase_ != "Bid" || seat != current_seat_) return;
  if (score < 0 || score > 3) {
    SendToUid(ctx_, uids_[seat], MsgId::kS2C_Error,
              proto_wire::EncodeS2C_Error(static_cast<int>(Err::kDdzIllegalBid), ErrMessage(Err::kDdzIllegalBid),
                                          MsgId::kC2S_DdzBid));
    return;
  }
  if (score > 0 && score <= bid_score_) {
    SendToUid(ctx_, uids_[seat], MsgId::kS2C_Error,
              proto_wire::EncodeS2C_Error(static_cast<int>(Err::kDdzIllegalBid), "bid too low", MsgId::kC2S_DdzBid));
    return;
  }
  LogRound(round_id_, "server", "ddz", room_id_, uids_[seat], seat, "bid", "score=" + std::to_string(score));
  const auto body = proto_wire::EncodeS2C_DdzBidBroadcast(seat, score);
  for (int64_t uid : AllUids()) SendToUid(ctx_, uid, MsgId::kS2C_DdzBidBroadcast, body);

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
    SendToUid(ctx_, uids_[i], MsgId::kS2C_DdzGameStart,
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
      SendToUid(ctx_, uids_[seat], MsgId::kS2C_Error,
                proto_wire::EncodeS2C_Error(static_cast<int>(Err::kDdzIllegalPlay), "cannot pass on lead",
                                            MsgId::kC2S_DdzPlay));
      return;
    }
    LogRound(round_id_, "server", "ddz", room_id_, uids_[seat], seat, "pass", "");
    const auto body =
        proto_wire::EncodeS2C_DdzPlayBroadcast(seat, true, {}, static_cast<int32_t>(hands_[seat].size()));
    for (int64_t uid : AllUids()) SendToUid(ctx_, uid, MsgId::kS2C_DdzPlayBroadcast, body);
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
    SendToUid(ctx_, uids_[seat], MsgId::kS2C_Error,
              proto_wire::EncodeS2C_Error(static_cast<int>(Err::kDdzIllegalPlay), "cards not in hand",
                                          MsgId::kC2S_DdzPlay));
    return;
  }
  auto pat = ddz::Identify(cards);
  if (pat.type == ddz::PatternType::kInvalid) {
    SendToUid(ctx_, uids_[seat], MsgId::kS2C_Error,
              proto_wire::EncodeS2C_Error(static_cast<int>(Err::kDdzIllegalPlay), "invalid pattern",
                                          MsgId::kC2S_DdzPlay));
    return;
  }
  if (!ddz::CanBeat(pat, last_pattern_)) {
    SendToUid(ctx_, uids_[seat], MsgId::kS2C_Error,
              proto_wire::EncodeS2C_Error(static_cast<int>(Err::kDdzIllegalPlay), "cannot beat", MsgId::kC2S_DdzPlay));
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
  for (int64_t uid : AllUids()) SendToUid(ctx_, uid, MsgId::kS2C_DdzPlayBroadcast, body);

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

  if (ctx_.wallet) {
    for (int i = 0; i < 3; ++i) {
      const std::string key = "settle:" + std::to_string(round_id_) + ":" + std::to_string(uids_[i]);
      ctx_.wallet->Adjust(uids_[i], Currency::kGold, deltas[i], "game_settle", key);
    }
  }

  for (int i = 0; i < 3; ++i) {
    proto_wire::SettleEntry e;
    e.uid = uids_[i];
    e.seat_id = i;
    e.delta_gold = deltas[i];
    entries.push_back(e);
  }

  const auto body = proto_wire::EncodeS2C_DdzSettle(round_id_, cfg_.base_score, mult, entries);
  for (int64_t uid : AllUids()) SendToUid(ctx_, uid, MsgId::kS2C_DdzSettle, body);

  nlohmann::json players = nlohmann::json::array();
  for (int i = 0; i < 3; ++i) {
    players.push_back(
        {{"uid", uids_[i]}, {"seat_id", i}, {"delta", deltas[i]}, {"is_landlord", i == landlord_}});
  }
  const std::string players_json = players.dump();
  if (ctx_.admin) {
    ctx_.admin->RecordRound(round_id_, room_id_, ctx_.template_id, players_json, cfg_.base_score, mult,
                            GameId::kDdz);
  }
  if (ctx_.social) {
    try {
      ctx_.social->OnRoundSettled(round_id_, ctx_.template_id, players_json, cfg_.base_score, mult);
    } catch (...) {
    }
  }
  if (ctx_.activity) {
    for (int64_t uid : AllUids()) {
      try {
        ctx_.activity->OnGameSettled(uid, ctx_.template_id);
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
  if (ctx_.on_round_finished) ctx_.on_round_finished();
}

void DdzClassicSimple::Tick(std::chrono::steady_clock::time_point now) {
  if (finished_) return;
  if (ctx_.is_trusteeship && ctx_.is_trusteeship(current_seat_)) {
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
    detail << "phase=" << phase_ << " turn=" << current_seat_ << " left=" << timeout << " hand=" << JoinIds(hand);
    LogRound(round_id_, "server", "ddz", room_id_, uid, seat, "reconnect", detail.str());
  }
  SendToUid(ctx_, uid, MsgId::kS2C_DdzReconnect,
            proto_wire::EncodeS2C_DdzReconnect(seat, phase_, hand, landlord_, current_seat_, timeout));
}

DdzRoomGame::DdzRoomGame(RoomCtx& ctx) : ctx_(ctx) {
  std::array<int64_t, 3> uids{};
  for (int i = 0; i < 3; ++i) uids[static_cast<size_t>(i)] = ctx.uid_of ? ctx.uid_of(i) : 0;
  impl_ = std::make_unique<DdzClassicSimple>(ctx_, uids);
}

void DdzRoomGame::Start() {
  if (ctx_.set_phase) ctx_.set_phase("Deal");
  if (ctx_.push_room_state) ctx_.push_room_state();
  impl_->Start();
  if (ctx_.set_phase) ctx_.set_phase(impl_->Phase());
}

void DdzRoomGame::Tick(std::chrono::steady_clock::time_point now) {
  if (!impl_ || impl_->Finished()) return;
  impl_->Tick(now);
  if (ctx_.set_phase) ctx_.set_phase(impl_->Phase());
}

bool DdzRoomGame::Handle(int64_t uid, uint32_t msg_id, const uint8_t* body, size_t len) {
  if (!impl_ || impl_->Finished()) return false;
  const int seat = SeatOf(uid);
  if (seat < 0) return false;
  if (msg_id == MsgId::kC2S_DdzBid) {
    int32_t score = 0;
    proto_wire::DecodeC2S_DdzBid(body, len, score);
    impl_->OnBid(seat, score);
    if (ctx_.set_phase) ctx_.set_phase(impl_->Phase());
    return true;
  }
  if (msg_id == MsgId::kC2S_DdzPlay) {
    bool pass = false;
    std::vector<int32_t> cards32;
    proto_wire::DecodeC2S_DdzPlay(body, len, pass, cards32);
    std::vector<int> cards(cards32.begin(), cards32.end());
    impl_->OnPlay(seat, pass, cards);
    if (ctx_.set_phase) ctx_.set_phase(impl_->Phase());
    return true;
  }
  return false;
}

void DdzRoomGame::OnDisconnect(int64_t uid) {
  if (!impl_ || impl_->Finished()) return;
  const int seat = SeatOf(uid);
  if (seat < 0) return;
  LogRound(impl_->RoundId(), "server", "ddz", ctx_.room_id, uid, seat, "disconnect", impl_->Phase());
}

void DdzRoomGame::OnReconnect(int64_t uid) {
  if (!impl_ || impl_->Finished()) return;
  impl_->SendReconnectSnapshot(uid);
}

bool DdzRoomGame::InProgress() const { return impl_ && !impl_->Finished(); }

int DdzRoomGame::SeatOf(int64_t uid) const {
  if (!ctx_.seat_of) return -1;
  return ctx_.seat_of(uid);
}

}  // namespace pandora
