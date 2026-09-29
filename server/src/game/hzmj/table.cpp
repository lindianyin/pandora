#include "game/hzmj/table.hpp"

#include "game/hzmj/config.hpp"

#include <algorithm>
#include <random>

namespace pandora {
namespace hzmj {

namespace {
int NextSeat(int s) { return (s + 1) % 4; }
}  // namespace

HzmjTable::HzmjTable(HzmjConfig cfg, std::array<int64_t, 4> uids, int banker_seat, int lian_zhuang)
    : cfg_(cfg), banker_seat_(banker_seat), lian_zhuang_(lian_zhuang) {
  for (int i = 0; i < 4; ++i) {
    seats_[static_cast<size_t>(i)].uid = uids[static_cast<size_t>(i)];
    seats_[static_cast<size_t>(i)].hand = ZeroHand();
    seats_[static_cast<size_t>(i)].tan_count.fill(0);
  }
  rng_ = [](int n) {
    static thread_local std::mt19937 gen{std::random_device{}()};
    if (n <= 0) return 0;
    return static_cast<int>(gen() % static_cast<unsigned>(n));
  };
}

void HzmjTable::Emit(OutEvent e) {
  if (sink_) sink_(std::move(e));
}

void HzmjTable::SetWallForTest(std::vector<TileId> wall) { wall_ = std::move(wall); }

void HzmjTable::SetHandForTest(int seat, HandCount hand) {
  if (seat < 0 || seat > 3) return;
  seats_[static_cast<size_t>(seat)].hand = hand;
}

void HzmjTable::ApplyDebugDianpaoDeal() {
  cfg_.start_as_sanlao = true;
  banker_seat_ = 1;
  for (auto& s : seats_) {
    s.hand = {};
    s.melds.clear();
    s.lou_hu.clear();
    s.piao_level = 0;
    s.gang_chain = 0;
    s.tan_count = {};
  }
  auto deal = [&](int seat, std::initializer_list<TileId> tiles) {
    for (TileId t : tiles) AddTile(seats_[static_cast<size_t>(seat)].hand, t);
  };
  // seat0：13 张听「东」(再来一张东即平胡)
  deal(0, {0, 1, 2, 3, 4, 5, 6, 7, 8, 32, 32, 32, 27});
  // seat1 庄：14 张，超时/托管会先打最小牌「东」
  deal(1, {27, 28, 28, 28, 28, 29, 29, 29, 29, 30, 30, 30, 30, 31});
  deal(2, {9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21});
  deal(3, {18, 19, 20, 21, 22, 23, 24, 25, 26, 22, 23, 24, 25});

  wall_ = BuildWall();
  HandCount used{};
  for (int s = 0; s < 4; ++s) {
    for (TileId t = 0; t < kTileKinds; ++t)
      used[static_cast<size_t>(t)] += seats_[static_cast<size_t>(s)].hand[static_cast<size_t>(t)];
  }
  std::vector<TileId> remain;
  remain.reserve(wall_.size());
  HandCount seen = used;
  for (TileId t : wall_) {
    if (seen[static_cast<size_t>(t)] > 0) {
      --seen[static_cast<size_t>(t)];
      continue;
    }
    remain.push_back(t);
  }
  wall_ = std::move(remain);
}

void HzmjTable::ShuffleWall() {
  if (wall_.empty()) wall_ = BuildWall();
  for (int i = static_cast<int>(wall_.size()) - 1; i > 0; --i) {
    const int j = rng_(i + 1);
    std::swap(wall_[static_cast<size_t>(i)], wall_[static_cast<size_t>(j)]);
  }
}

TileId HzmjTable::DrawOne() {
  if (wall_.empty()) return kTileInvalid;
  const TileId t = wall_.back();
  wall_.pop_back();
  return t;
}

void HzmjTable::DealTiles() {
  if (wall_.empty()) {
    wall_ = BuildWall();
    ShuffleWall();
  }
  for (int i = 0; i < 13; ++i) {
    for (int s = 0; s < 4; ++s) {
      const TileId t = DrawOne();
      AddTile(seats_[static_cast<size_t>(s)].hand, t);
    }
  }
  const TileId extra = DrawOne();
  AddTile(seats_[static_cast<size_t>(banker_seat_)].hand, extra);
}

void HzmjTable::Start() {
  phase_ = Phase::kDeal;
  if (wall_.empty()) {
    wall_ = BuildWall();
    ShuffleWall();
  }
  for (auto& s : seats_) {
    if (HandSize(s.hand) == 0) {
      /* will deal */
    }
  }
  bool need_deal = true;
  for (auto& s : seats_) {
    if (HandSize(s.hand) > 0) need_deal = false;
  }
  if (need_deal) DealTiles();
  phase_ = Phase::kPlay;
  turn_seat_ = banker_seat_;
  sub_ = PlaySub::kDiscard;
  piao_seat_ = -1;
  Emit({"GameStart", banker_seat_, kBai, "fixed_bai"});
  Emit({"Turn", turn_seat_, kTileInvalid, "discard"});
}

void HzmjTable::EnterDiscard(int seat, bool /*after_draw*/) {
  turn_seat_ = seat;
  sub_ = PlaySub::kDiscard;
  auto ting = ComputeTing(seats_[static_cast<size_t>(seat)].hand, seats_[static_cast<size_t>(seat)].melds);
  seats_[static_cast<size_t>(seat)].baotou_ting = ting.baotou_ting;
  Emit({"Turn", seat, kTileInvalid, "discard"});
}

bool HzmjTable::OnDiscard(int seat, TileId tile) {
  if (phase_ != Phase::kPlay) return false;
  if (sub_ != PlaySub::kDiscard && sub_ != PlaySub::kPiaoLock) return false;
  if (seat != turn_seat_) return false;
  if (!RemoveTile(seats_[static_cast<size_t>(seat)].hand, tile)) return false;

  last_discard_ = tile;
  last_discard_seat_ = seat;
  Emit({"Discard", seat, tile, ""});

  if (IsCaishen(tile) && seats_[static_cast<size_t>(seat)].baotou_ting) {
    int& lv = seats_[static_cast<size_t>(seat)].piao_level;
    lv = std::min(cfg_.max_piao, lv + 1);
    piao_seat_ = seat;
    sub_ = PlaySub::kPiaoLock;
    Emit({"Piao", seat, tile, std::to_string(lv)});
    // next: piao seat draws
    if (wall_.empty()) {
      FinishLiuJu();
      return true;
    }
    const TileId d = DrawOne();
    AddTile(seats_[static_cast<size_t>(seat)].hand, d);
    Emit({"Draw", seat, d, "piao"});
    // auto hu if can
    if (CheckHu(seats_[static_cast<size_t>(seat)].hand, seats_[static_cast<size_t>(seat)].melds, d, true).ok) {
      FinishHu(seat, d, true, -1);
      return true;
    }
    EnterDiscard(seat, true);
    return true;
  }

  // failed piao if was piao and discarded non-caishen
  if (piao_seat_ == seat) {
    piao_seat_ = -1;
    seats_[static_cast<size_t>(seat)].piao_level = 0;
  }

  OpenClaimWindow();
  return true;
}

void HzmjTable::OpenClaimWindow() {
  sub_ = PlaySub::kClaimWindow;
  const int N = ComputeN(lian_zhuang_, cfg_.start_as_sanlao);
  for (int i = 0; i < 4; ++i) {
    claims_[static_cast<size_t>(i)] = ActionKind::kPass;
    claims_ready_[i] = (i == last_discard_seat_);
  }
  bool need_wait = false;
  for (int s = 0; s < 4; ++s) {
    if (s == last_discard_seat_) continue;
    if (piao_seat_ >= 0) {
      claims_ready_[s] = true;
      continue;
    }
    const auto& h = seats_[static_cast<size_t>(s)].hand;
    const auto& ms = seats_[static_cast<size_t>(s)].melds;
    bool can_hu = WouldHu(h, ms, last_discard_, false);
    if (can_hu && cfg_.lou_hu) {
      for (TileId t : seats_[static_cast<size_t>(s)].lou_hu) {
        if (t == last_discard_) {
          can_hu = false;
          break;
        }
      }
    }
    if (can_hu) can_hu = CanDianpao(cfg_, N, last_discard_seat_, s, banker_seat_);
    const bool can_peng = CanPeng(h, last_discard_);
    const bool can_gang = CanMingGang(h, last_discard_);
    const bool can_chi = NextSeat(last_discard_seat_) == s && CanChi(h, last_discard_);
    if (!(can_hu || can_peng || can_gang || can_chi)) {
      claims_ready_[s] = true;  // 无牌权：自动过
    } else {
      need_wait = true;
    }
  }
  if (!need_wait || piao_seat_ >= 0) {
    for (int i = 0; i < 4; ++i) claims_ready_[i] = true;
    ResolveClaims();
    return;
  }
  Emit({"ClaimWindow", last_discard_seat_, last_discard_, ""});
}

bool HzmjTable::SeatNeedsClaimInput(int seat) const {
  if (sub_ != PlaySub::kClaimWindow) return false;
  if (seat < 0 || seat > 3) return false;
  return !claims_ready_[seat];
}

bool HzmjTable::OnAction(int seat, ActionKind act, const ChiOption* chi) {
  if (phase_ != Phase::kPlay || sub_ != PlaySub::kClaimWindow) return false;
  if (seat < 0 || seat > 3 || seat == last_discard_seat_) return false;
  if (piao_seat_ >= 0) return false;  // no claims during piao from others

  auto& h = seats_[static_cast<size_t>(seat)].hand;
  const auto& ms = seats_[static_cast<size_t>(seat)].melds;

  if (act == ActionKind::kHu) {
    if (!WouldHu(h, ms, last_discard_, false)) return false;
    if (cfg_.lou_hu) {
      for (TileId t : seats_[static_cast<size_t>(seat)].lou_hu) {
        if (t == last_discard_) return false;
      }
    }
    const int N = ComputeN(lian_zhuang_, cfg_.start_as_sanlao);
    if (!CanDianpao(cfg_, N, last_discard_seat_, seat, banker_seat_)) return false;
  } else if (act == ActionKind::kPeng) {
    if (!CanPeng(h, last_discard_)) return false;
  } else if (act == ActionKind::kGang) {
    if (!CanMingGang(h, last_discard_)) return false;
  } else if (act == ActionKind::kChi) {
    if (NextSeat(last_discard_seat_) != seat) return false;
    if (!CanChi(h, last_discard_)) return false;
    if (chi) claim_chi_[static_cast<size_t>(seat)] = *chi;
  } else if (act == ActionKind::kPass) {
    if (cfg_.lou_hu && WouldHu(h, ms, last_discard_, false)) {
      seats_[static_cast<size_t>(seat)].lou_hu.push_back(last_discard_);
    }
  } else {
    return false;
  }

  claims_[static_cast<size_t>(seat)] = act;
  claims_ready_[seat] = true;
  bool all = true;
  for (int i = 0; i < 4; ++i)
    if (!claims_ready_[i]) all = false;
  if (all) ResolveClaims();
  return true;
}

void HzmjTable::ResolveClaims() {
  // priority Hu > Gang > Peng > Chi; among Hu counterclockwise from discarder
  int winner = -1;
  for (int step = 1; step <= 3; ++step) {
    const int s = (last_discard_seat_ + step) % 4;
    if (claims_[static_cast<size_t>(s)] == ActionKind::kHu) {
      winner = s;
      break;
    }
  }
  if (winner >= 0) {
    AddTile(seats_[static_cast<size_t>(winner)].hand, last_discard_);
    FinishHu(winner, last_discard_, false, last_discard_seat_);
    return;
  }
  for (int step = 1; step <= 3; ++step) {
    const int s = (last_discard_seat_ + step) % 4;
    if (claims_[static_cast<size_t>(s)] == ActionKind::kGang) {
      seats_[static_cast<size_t>(s)].hand = ApplyMingGang(seats_[static_cast<size_t>(s)].hand, last_discard_);
      Meld m;
      m.type = MeldType::kMingGang;
      m.tile = last_discard_;
      m.from_seat = last_discard_seat_;
      seats_[static_cast<size_t>(s)].melds.push_back(m);
      seats_[static_cast<size_t>(s)].tan_count[static_cast<size_t>(last_discard_seat_)]++;
      seats_[static_cast<size_t>(s)].gang_chain++;
      {
        OutEvent ev{"MingGang", s, last_discard_, ""};
        ev.from_seat = last_discard_seat_;
        ev.tiles = {last_discard_, last_discard_, last_discard_, last_discard_};
        Emit(std::move(ev));
      }
      if (wall_.empty()) {
        FinishLiuJu();
        return;
      }
      const TileId d = DrawOne();
      AddTile(seats_[static_cast<size_t>(s)].hand, d);
      Emit({"Draw", s, d, "gang"});
      if (CheckHu(seats_[static_cast<size_t>(s)].hand, seats_[static_cast<size_t>(s)].melds, d, true).ok) {
        FinishHu(s, d, true, -1);
        return;
      }
      EnterDiscard(s, true);
      return;
    }
  }
  for (int step = 1; step <= 3; ++step) {
    const int s = (last_discard_seat_ + step) % 4;
    if (claims_[static_cast<size_t>(s)] == ActionKind::kPeng) {
      seats_[static_cast<size_t>(s)].hand = ApplyPeng(seats_[static_cast<size_t>(s)].hand, last_discard_);
      Meld m;
      m.type = MeldType::kPeng;
      m.tile = last_discard_;
      m.from_seat = last_discard_seat_;
      seats_[static_cast<size_t>(s)].melds.push_back(m);
      if (cfg_.peng_counts_tan) seats_[static_cast<size_t>(s)].tan_count[static_cast<size_t>(last_discard_seat_)]++;
      seats_[static_cast<size_t>(s)].gang_chain = 0;
      {
        OutEvent ev{"Peng", s, last_discard_, ""};
        ev.from_seat = last_discard_seat_;
        ev.tiles = {last_discard_, last_discard_, last_discard_};
        Emit(std::move(ev));
      }
      EnterDiscard(s, false);
      return;
    }
  }
  const int chi_seat = NextSeat(last_discard_seat_);
  if (claims_[static_cast<size_t>(chi_seat)] == ActionKind::kChi) {
    auto opts = ListChiOptions(seats_[static_cast<size_t>(chi_seat)].hand, last_discard_);
    ChiOption opt = claim_chi_[static_cast<size_t>(chi_seat)];
    if (opts.empty()) {
      NextTurnAfterDiscard();
      return;
    }
    bool ok = false;
    for (const auto& o : opts) {
      if (o.hand_tiles == opt.hand_tiles) {
        ok = true;
        opt = o;
        break;
      }
    }
    if (!ok) opt = opts.front();
    seats_[static_cast<size_t>(chi_seat)].hand = ApplyChi(seats_[static_cast<size_t>(chi_seat)].hand, opt);
    Meld m;
    m.type = MeldType::kChi;
    m.tile = opt.formed[0];
    m.chi_tiles = opt.formed;
    m.from_seat = last_discard_seat_;
    seats_[static_cast<size_t>(chi_seat)].melds.push_back(m);
    seats_[static_cast<size_t>(chi_seat)].tan_count[static_cast<size_t>(last_discard_seat_)]++;
    seats_[static_cast<size_t>(chi_seat)].gang_chain = 0;
    {
      OutEvent ev{"Chi", chi_seat, last_discard_, ""};
      ev.from_seat = last_discard_seat_;
      ev.tiles = {opt.formed[0], opt.formed[1], opt.formed[2]};
      Emit(std::move(ev));
    }
    EnterDiscard(chi_seat, false);
    return;
  }
  NextTurnAfterDiscard();
}

void HzmjTable::NextTurnAfterDiscard() {
  seats_[static_cast<size_t>(last_discard_seat_)].gang_chain = 0;
  const int ns = NextSeat(last_discard_seat_);
  if (wall_.empty()) {
    FinishLiuJu();
    return;
  }
  const TileId d = DrawOne();
  AddTile(seats_[static_cast<size_t>(ns)].hand, d);
  Emit({"Draw", ns, d, ""});
  if (CheckHu(seats_[static_cast<size_t>(ns)].hand, seats_[static_cast<size_t>(ns)].melds, d, true).ok) {
    // leave decision to player; for tests they call Finish via discard path or action;
    // keep in discard; auto-hu only on timeout if configured — not here
  }
  EnterDiscard(ns, true);
}

bool HzmjTable::OnAnGang(int seat, TileId tile) {
  if (phase_ != Phase::kPlay || sub_ != PlaySub::kDiscard) return false;
  if (seat != turn_seat_) return false;
  if (piao_seat_ >= 0 && cfg_.piao_block_an_gang && seat != piao_seat_) return false;
  if (!CanAnGang(seats_[static_cast<size_t>(seat)].hand, tile)) return false;
  seats_[static_cast<size_t>(seat)].hand = ApplyAnGang(seats_[static_cast<size_t>(seat)].hand, tile);
  Meld m;
  m.type = MeldType::kAnGang;
  m.tile = tile;
  seats_[static_cast<size_t>(seat)].melds.push_back(m);
  seats_[static_cast<size_t>(seat)].gang_chain++;
  {
    OutEvent ev{"AnGang", seat, tile, ""};
    ev.tiles = {tile, tile, tile, tile};
    Emit(std::move(ev));
  }
  if (wall_.empty()) {
    FinishLiuJu();
    return true;
  }
  const TileId d = DrawOne();
  AddTile(seats_[static_cast<size_t>(seat)].hand, d);
  Emit({"Draw", seat, d, "gang"});
  if (CheckHu(seats_[static_cast<size_t>(seat)].hand, seats_[static_cast<size_t>(seat)].melds, d, true).ok) {
    FinishHu(seat, d, true, -1);
    return true;
  }
  EnterDiscard(seat, true);
  return true;
}

bool HzmjTable::OnBuGang(int seat, TileId tile) {
  if (phase_ != Phase::kPlay || sub_ != PlaySub::kDiscard) return false;
  if (seat != turn_seat_) return false;
  if (!CanBuGang(seats_[static_cast<size_t>(seat)].hand, seats_[static_cast<size_t>(seat)].melds, tile)) return false;
  RemoveTile(seats_[static_cast<size_t>(seat)].hand, tile, 1);
  for (auto& m : seats_[static_cast<size_t>(seat)].melds) {
    if (m.type == MeldType::kPeng && m.tile == tile) {
      m.type = MeldType::kBuGang;
      break;
    }
  }
  seats_[static_cast<size_t>(seat)].gang_chain++;
  {
    OutEvent ev{"BuGang", seat, tile, ""};
    ev.tiles = {tile, tile, tile, tile};
    Emit(std::move(ev));
  }
  // qiang gang window simplified: check others would hu
  if (cfg_.qiang_gang_hu) {
    for (int step = 1; step <= 3; ++step) {
      const int s = (seat + step) % 4;
      if (WouldHu(seats_[static_cast<size_t>(s)].hand, seats_[static_cast<size_t>(s)].melds, tile, false)) {
        AddTile(seats_[static_cast<size_t>(s)].hand, tile);
        FinishHu(s, tile, false, seat);
        return true;
      }
    }
  }
  if (wall_.empty()) {
    FinishLiuJu();
    return true;
  }
  const TileId d = DrawOne();
  AddTile(seats_[static_cast<size_t>(seat)].hand, d);
  Emit({"Draw", seat, d, "gang"});
  if (CheckHu(seats_[static_cast<size_t>(seat)].hand, seats_[static_cast<size_t>(seat)].melds, d, true).ok) {
    FinishHu(seat, d, true, -1);
    return true;
  }
  EnterDiscard(seat, true);
  return true;
}

void HzmjTable::FinishHu(int winner, TileId win_tile, bool zimo, int shooter) {
  auto& ws = seats_[static_cast<size_t>(winner)];
  auto hu = CheckHu(ws.hand, ws.melds, win_tile, zimo);
  const int N = ComputeN(lian_zhuang_, cfg_.start_as_sanlao);
  const int M = ComputeM(hu, ws.piao_level, zimo ? ws.gang_chain : 0);
  SettleInput in;
  in.winner_seat = winner;
  in.banker_seat = banker_seat_;
  in.N = N;
  in.M = M;
  in.base_score = cfg_.base_score;
  in.is_zimo = zimo;
  in.shooter_seat = shooter;
  for (int a = 0; a < 4; ++a)
    for (int f = 0; f < 4; ++f) in.tan_count[static_cast<size_t>(a)][static_cast<size_t>(f)] = seats_[static_cast<size_t>(a)].tan_count[static_cast<size_t>(f)];
  last_settle_ = BuildSettle(in);
  phase_ = Phase::kSettle;
  if (winner == banker_seat_) ++lian_zhuang_;
  else {
    banker_seat_ = winner;
    lian_zhuang_ = 1;
  }
  Emit({"Settle", winner, win_tile, std::to_string(M) + "x" + std::to_string(N)});
}

void HzmjTable::FinishLiuJu() {
  phase_ = Phase::kLiuJu;
  ++lian_zhuang_;
  Emit({"LiuJu", banker_seat_, kTileInvalid, std::to_string(lian_zhuang_)});
}

void HzmjTable::OnTimeout(int seat) {
  if (phase_ != Phase::kPlay) return;
  if (sub_ == PlaySub::kClaimWindow) {
    OnAction(seat, ActionKind::kPass, nullptr);
    return;
  }
  if (sub_ == PlaySub::kDiscard && seat == turn_seat_) {
    // discard a tile: prefer last drawn — use any tile present
    auto& h = seats_[static_cast<size_t>(seat)].hand;
    // auto-hu if can after draw
    for (TileId t = 0; t < kTileKinds; ++t) {
      if (h[static_cast<size_t>(t)] <= 0) continue;
      HandCount tmp = h;
      RemoveTile(tmp, t);
      // if current hand already 14 and is hu, finish
    }
    if (HandSize(h) % 3 == 2) {
      auto hu = CheckHu(h, seats_[static_cast<size_t>(seat)].melds, kTileInvalid, true);
      if (hu.ok) {
        FinishHu(seat, kTileInvalid, true, -1);
        return;
      }
    }
    for (TileId t = 0; t < kTileKinds; ++t) {
      if (h[static_cast<size_t>(t)] > 0) {
        OnDiscard(seat, t);
        return;
      }
    }
  }
}

}  // namespace hzmj
}  // namespace pandora
