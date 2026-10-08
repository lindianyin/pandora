#include "game/phz/table.hpp"

#include <algorithm>
#include <random>

namespace pandora {
namespace phz {

PhzTable::PhzTable(PhzConfig cfg, std::array<int64_t, kSeats> uids, int banker_seat)
    : cfg_(std::move(cfg)), banker_seat_(banker_seat) {
  for (int i = 0; i < kSeats; ++i) {
    seats_[static_cast<size_t>(i)].uid = uids[static_cast<size_t>(i)];
    seats_[static_cast<size_t>(i)].hand = ZeroHand();
  }
  rng_ = [](int n) {
    static thread_local std::mt19937 gen{std::random_device{}()};
    if (n <= 0) return 0;
    return static_cast<int>(gen() % static_cast<unsigned>(n));
  };
}

void PhzTable::SetWallForTest(std::vector<TileId> wall) { wall_ = std::move(wall); }

void PhzTable::SetHandForTest(int seat, HandCount hand) {
  if (seat < 0 || seat >= kSeats) return;
  seats_[static_cast<size_t>(seat)].hand = hand;
}

void PhzTable::ApplyDebugQuickHuDeal() {
  banker_seat_ = 0;
  for (int s = 0; s < kSeats; ++s) {
    seats_[static_cast<size_t>(s)].hand = ZeroHand();
    seats_[static_cast<size_t>(s)].melds.clear();
    seats_[static_cast<size_t>(s)].pao_ti_count = 0;
    seats_[static_cast<size_t>(s)].lou_chi.clear();
  }

  // seat1: 6 small kans (二~七) + pair of 一 → draw 一 = 7 kans, xi=21, auto zimo
  constexpr TileId kWin = 0;  // 一
  for (TileId t = 1; t <= 6; ++t) AddTile(seats_[1].hand, t, 3);
  AddTile(seats_[1].hand, kWin, 2);

  HandCount used = ZeroHand();
  for (TileId t = 0; t < kTileKinds; ++t) used[static_cast<size_t>(t)] = seats_[1].hand[static_cast<size_t>(t)];

  std::vector<TileId> pool;
  pool.reserve(60);
  for (TileId t = 0; t < kTileKinds; ++t) {
    for (int n = used[static_cast<size_t>(t)]; n < 4; ++n) pool.push_back(t);
  }

  // Keep one winning tile for wall.back(); prefer banker discard 拾(19).
  std::vector<TileId> win_keep;
  std::vector<TileId> rest;
  rest.reserve(pool.size());
  for (TileId t : pool) {
    if (t == kWin && win_keep.empty()) win_keep.push_back(t);
    else rest.push_back(t);
  }

  auto take_n = [&](int seat, int n, TileId prefer_first) {
    if (prefer_first >= 0) {
      for (size_t i = 0; i < rest.size() && HandSize(seats_[static_cast<size_t>(seat)].hand) < n; ++i) {
        if (rest[i] != prefer_first) continue;
        AddTile(seats_[static_cast<size_t>(seat)].hand, rest[i], 1);
        rest.erase(rest.begin() + static_cast<std::ptrdiff_t>(i));
        break;
      }
    }
    while (HandSize(seats_[static_cast<size_t>(seat)].hand) < n && !rest.empty()) {
      AddTile(seats_[static_cast<size_t>(seat)].hand, rest.back(), 1);
      rest.pop_back();
    }
  };

  take_n(0, 21, 19);  // banker: include 拾 to discard
  take_n(2, 20, -1);

  // Seat2 must not peng banker 拾 — keep at most one 拾.
  while (seats_[2].hand[19] > 1) {
    RemoveTile(seats_[2].hand, 19, 1);
    rest.push_back(19);
  }
  while (HandSize(seats_[2].hand) < 20 && !rest.empty()) {
    size_t pick = rest.size() - 1;
    for (size_t i = 0; i < rest.size(); ++i) {
      if (rest[i] != 19 && rest[i] != kWin) {
        pick = i;
        break;
      }
    }
    AddTile(seats_[2].hand, rest[pick], 1);
    rest.erase(rest.begin() + static_cast<std::ptrdiff_t>(pick));
  }

  wall_ = std::move(rest);
  // Draw uses pop_back — winning 一 last.
  if (!win_keep.empty()) wall_.push_back(win_keep[0]);
}

void PhzTable::SetMeldsForTest(int seat, std::vector<Meld> melds) {
  if (seat < 0 || seat >= kSeats) return;
  seats_[static_cast<size_t>(seat)].melds = std::move(melds);
}

void PhzTable::SetPaoTiCountForTest(int seat, int n) {
  if (seat < 0 || seat >= kSeats) return;
  seats_[static_cast<size_t>(seat)].pao_ti_count = n;
}

void PhzTable::Emit(OutEvent e) {
  if (sink_) sink_(e);
}

void PhzTable::Deal() {
  if (wall_.empty()) {
    wall_ = BuildWall();
    for (int i = static_cast<int>(wall_.size()) - 1; i > 0; --i) {
      const int j = rng_(i + 1);
      std::swap(wall_[static_cast<size_t>(i)], wall_[static_cast<size_t>(j)]);
    }
  }
  for (int s = 0; s < kSeats; ++s) {
    seats_[static_cast<size_t>(s)].hand = ZeroHand();
    seats_[static_cast<size_t>(s)].melds.clear();
    seats_[static_cast<size_t>(s)].pao_ti_count = 0;
    seats_[static_cast<size_t>(s)].lou_chi.clear();
  }
  auto take = [&](int seat, int n) {
    for (int i = 0; i < n && !wall_.empty(); ++i) {
      AddTile(seats_[static_cast<size_t>(seat)].hand, wall_.back(), 1);
      wall_.pop_back();
    }
  };
  for (int s = 0; s < kSeats; ++s) take(s, 20);
  take(banker_seat_, 1);  // banker 21

  // Deal-time ti for 4-of-a-kind in hand
  for (int s = 0; s < kSeats; ++s) {
    for (TileId t = 0; t < kTileKinds; ++t) {
      if (seats_[static_cast<size_t>(s)].hand[static_cast<size_t>(t)] >= 4) {
        RemoveTile(seats_[static_cast<size_t>(s)].hand, t, 4);
        Meld m;
        m.kind = MeldKind::kTi;
        m.tile = t;
        m.tiles = {t, t, t, t};
        seats_[static_cast<size_t>(s)].melds.push_back(m);
        seats_[static_cast<size_t>(s)].pao_ti_count++;
        Emit(OutEvent{"Ti", s, t, "", m.tiles, -1, static_cast<int>(MeldKind::kTi), 6});
      }
    }
  }
}

void PhzTable::Start() {
  phase_ = Phase::kDeal;
  bool need_deal = true;
  for (const auto& s : seats_) {
    if (HandSize(s.hand) > 0) need_deal = false;
  }
  if (need_deal) {
    Deal();
  } else if (wall_.empty()) {
    // tests may leave empty wall for liuju
  }
  started_ = true;
  phase_ = Phase::kPlay;
  last_discard_ = kTileInvalid;
  last_reveal_ = kTileInvalid;
  Emit(OutEvent{"GameStart", banker_seat_, kTileInvalid, "", {}, -1, 0, 0});
  BeginDiscard(banker_seat_);
}

void PhzTable::BeginDiscard(int seat) {
  turn_seat_ = seat;
  sub_ = PlaySub::kDiscard;
  last_discard_ = kTileInvalid;
  last_reveal_ = kTileInvalid;
  can_hu_ = false;
  if (cfg_.auto_hu_on_draw) {
    // optional: check if already hu without new tile — skip
  }
  Emit(OutEvent{"Turn", seat, kTileInvalid, "discard", {}, -1, 0, 0});
}

bool PhzTable::HasPaoOrTi(int seat) const {
  for (const auto& m : seats_[static_cast<size_t>(seat)].melds) {
    if (m.kind == MeldKind::kPao || m.kind == MeldKind::kTi) return true;
  }
  return seats_[static_cast<size_t>(seat)].pao_ti_count > 0;
}

void PhzTable::DoWei(int seat, TileId tile, bool chou) {
  RemoveTile(seats_[static_cast<size_t>(seat)].hand, tile, 2);
  Meld m;
  m.kind = chou ? MeldKind::kChouWei : MeldKind::kWei;
  m.tile = tile;
  m.tiles = {tile, tile, tile};
  seats_[static_cast<size_t>(seat)].melds.push_back(m);
  Emit(OutEvent{"Wei", seat, tile, "", m.tiles, -1, static_cast<int>(m.kind), 3});
  BeginDiscard(seat);
}

void PhzTable::DoTi(int seat, TileId tile) {
  // from hand 3 + draw, or upgrade wei/kan
  auto& st = seats_[static_cast<size_t>(seat)];
  if (st.hand[static_cast<size_t>(tile)] >= 3) {
    RemoveTile(st.hand, tile, 3);
  } else {
    // upgrade wei
    for (auto& m : st.melds) {
      if ((m.kind == MeldKind::kWei || m.kind == MeldKind::kChouWei || m.kind == MeldKind::kKan) && m.tile == tile) {
        m.kind = MeldKind::kTi;
        m.tiles = {tile, tile, tile, tile};
        st.pao_ti_count++;
        Emit(OutEvent{"Ti", seat, tile, "", m.tiles, -1, static_cast<int>(MeldKind::kTi), 6});
        AfterPaoOrTi(seat);
        return;
      }
    }
  }
  Meld m;
  m.kind = MeldKind::kTi;
  m.tile = tile;
  m.tiles = {tile, tile, tile, tile};
  st.melds.push_back(m);
  st.pao_ti_count++;
  Emit(OutEvent{"Ti", seat, tile, "", m.tiles, -1, static_cast<int>(MeldKind::kTi), 6});
  AfterPaoOrTi(seat);
}

void PhzTable::DoPao(int seat, TileId tile, int from) {
  auto& st = seats_[static_cast<size_t>(seat)];
  // remove peng/wei/kan of tile and make pao
  for (auto it = st.melds.begin(); it != st.melds.end(); ++it) {
    if ((it->kind == MeldKind::kPeng || it->kind == MeldKind::kWei || it->kind == MeldKind::kChouWei ||
         it->kind == MeldKind::kKan) &&
        it->tile == tile) {
      it->kind = MeldKind::kPao;
      it->tiles = {tile, tile, tile, tile};
      it->from_seat = from;
      st.pao_ti_count++;
      Emit(OutEvent{"Pao", seat, tile, "", it->tiles, from, static_cast<int>(MeldKind::kPao), 5});
      AfterPaoOrTi(seat);
      return;
    }
  }
  // hand has 3 + claim
  if (st.hand[static_cast<size_t>(tile)] >= 3) {
    RemoveTile(st.hand, tile, 3);
    Meld m;
    m.kind = MeldKind::kPao;
    m.tile = tile;
    m.tiles = {tile, tile, tile, tile};
    m.from_seat = from;
    st.melds.push_back(m);
    st.pao_ti_count++;
    Emit(OutEvent{"Pao", seat, tile, "", m.tiles, from, static_cast<int>(MeldKind::kPao), 5});
    AfterPaoOrTi(seat);
  }
}

void PhzTable::AfterPaoOrTi(int seat) {
  last_discard_ = kTileInvalid;
  last_reveal_ = kTileInvalid;
  if (cfg_.first_pao_ti_discard && seats_[static_cast<size_t>(seat)].pao_ti_count == 1) {
    BeginDiscard(seat);
  } else {
    BeginDraw(NextSeat(seat));
  }
}

void PhzTable::FinishHu(int seat, TileId tile, HuFrom from) {
  const bool has_pt = HasPaoOrTi(seat);
  auto hu = CheckHu(seats_[static_cast<size_t>(seat)].hand, seats_[static_cast<size_t>(seat)].melds, tile, from, cfg_,
                    has_pt);
  if (!hu.ok) return;
  last_settle_ = BuildSettle(seat, hu, cfg_, banker_seat_);
  last_settle_.hu_tile = tile;
  last_hu_tile_ = tile;
  last_hu_draw_ = (from == HuFrom::kDraw);
  phase_ = Phase::kSettle;
  banker_seat_ = seat;  // winner becomes banker
  Emit(OutEvent{"Settle", seat, tile, "", {}, -1, 0, 0});
}

void PhzTable::FinishLiuJu() {
  phase_ = Phase::kLiuJu;
  Emit(OutEvent{"LiuJu", banker_seat_, kTileInvalid, "", {}, -1, 0, 0});
}

void PhzTable::BeginDraw(int seat) {
  if (wall_.empty()) {
    FinishLiuJu();
    return;
  }
  turn_seat_ = seat;
  sub_ = PlaySub::kDrawJudge;
  const TileId tile = wall_.back();
  wall_.pop_back();
  Emit(OutEvent{"Draw", seat, tile, "", {}, -1, 0, 0});

  auto& st = seats_[static_cast<size_t>(seat)];

  // Hu on draw
  if (cfg_.auto_hu_on_draw) {
    auto hu = CheckHu(st.hand, st.melds, tile, HuFrom::kDraw, cfg_, HasPaoOrTi(seat));
    if (hu.ok) {
      FinishHu(seat, tile, HuFrom::kDraw);
      return;
    }
  }

  // Ti: hand has 3 of tile
  if (cfg_.force_ti && st.hand[static_cast<size_t>(tile)] >= 3) {
    DoTi(seat, tile);
    return;
  }
  // Ti upgrade from wei
  if (cfg_.force_ti) {
    for (const auto& m : st.melds) {
      if ((m.kind == MeldKind::kWei || m.kind == MeldKind::kChouWei) && m.tile == tile) {
        DoTi(seat, tile);
        return;
      }
    }
  }

  // Wei: hand has 2
  if (cfg_.force_wei && st.hand[static_cast<size_t>(tile)] >= 2) {
    DoWei(seat, tile, false);
    return;
  }

  // Reveal (tile not into hand)
  last_reveal_ = tile;
  last_reveal_seat_ = seat;
  claim_from_reveal_ = true;
  last_discard_ = kTileInvalid;
  Emit(OutEvent{"Reveal", seat, tile, "", {}, -1, 0, 0});
  OpenClaimFromReveal(seat, tile);
}

void PhzTable::OpenClaimFromDiscard(int seat, TileId tile) {
  sub_ = PlaySub::kClaimWindow;
  claim_from_reveal_ = false;
  last_discard_ = tile;
  last_discard_seat_ = seat;

  // Force pao if someone has peng/wei of tile
  if (cfg_.force_pao) {
    for (int s = 0; s < kSeats; ++s) {
      if (s == seat) continue;
      for (const auto& m : seats_[static_cast<size_t>(s)].melds) {
        if ((m.kind == MeldKind::kPeng || m.kind == MeldKind::kWei || m.kind == MeldKind::kChouWei ||
             m.kind == MeldKind::kKan) &&
            m.tile == tile) {
          DoPao(s, tile, seat);
          return;
        }
      }
    }
  }

  for (int s = 0; s < kSeats; ++s) {
    claim_choice_[static_cast<size_t>(s)] = ActionKind::kPass;
    claim_responded_[static_cast<size_t>(s)] = true;  // provisional
  }
  // Resolve who needs input BEFORE Emit so RoomManager SeatNeedsClaimInput is correct.
  bool any = false;
  for (int s = 0; s < kSeats; ++s) {
    if (s == seat) continue;
    claim_responded_[static_cast<size_t>(s)] = false;
    if (SeatNeedsClaimInput(s)) {
      any = true;
    } else {
      claim_responded_[static_cast<size_t>(s)] = true;
    }
  }
  if (!any) {
    ResolveClaimPassAll();
    return;
  }
  Emit(OutEvent{"ClaimWindow", seat, tile, "discard", {}, -1, 0, 0});
}

void PhzTable::OpenClaimFromReveal(int seat, TileId tile) {
  sub_ = PlaySub::kClaimWindow;
  claim_from_reveal_ = true;

  if (cfg_.force_pao) {
    for (int s = 0; s < kSeats; ++s) {
      if (s == seat) continue;
      for (const auto& m : seats_[static_cast<size_t>(s)].melds) {
        if ((m.kind == MeldKind::kPeng || m.kind == MeldKind::kWei || m.kind == MeldKind::kChouWei ||
             m.kind == MeldKind::kKan) &&
            m.tile == tile) {
          DoPao(s, tile, seat);
          return;
        }
      }
    }
  }

  for (int s = 0; s < kSeats; ++s) {
    claim_choice_[static_cast<size_t>(s)] = ActionKind::kPass;
    claim_responded_[static_cast<size_t>(s)] = true;
  }
  bool any = false;
  for (int s = 0; s < kSeats; ++s) {
    if (s == seat) continue;
    claim_responded_[static_cast<size_t>(s)] = false;
    if (SeatNeedsClaimInput(s)) {
      any = true;
    } else {
      claim_responded_[static_cast<size_t>(s)] = true;
    }
  }
  if (!any) {
    ResolveClaimPassAll();
    return;
  }
  Emit(OutEvent{"ClaimWindow", seat, tile, "reveal", {}, -1, 0, 0});
}

bool PhzTable::SeatCanHu(int seat) const {
  if (sub_ != PlaySub::kClaimWindow || seat < 0 || seat >= kSeats) return false;
  const TileId tile = claim_from_reveal_ ? last_reveal_ : last_discard_;
  const int from = claim_from_reveal_ ? last_reveal_seat_ : last_discard_seat_;
  if (seat == from || tile == kTileInvalid) return false;
  if (!(cfg_.dian_pao || claim_from_reveal_)) return false;
  const auto& st = seats_[static_cast<size_t>(seat)];
  return CheckHu(st.hand, st.melds, tile, claim_from_reveal_ ? HuFrom::kReveal : HuFrom::kDiscard, cfg_,
                 HasPaoOrTi(seat))
      .ok;
}

bool PhzTable::SeatNeedsClaimInput(int seat) const {
  if (sub_ != PlaySub::kClaimWindow || seat < 0 || seat >= kSeats) return false;
  if (claim_responded_[static_cast<size_t>(seat)]) return false;
  const TileId tile = claim_from_reveal_ ? last_reveal_ : last_discard_;
  const int from = claim_from_reveal_ ? last_reveal_seat_ : last_discard_seat_;
  if (seat == from) return false;
  const auto& st = seats_[static_cast<size_t>(seat)];
  if (SeatCanHu(seat)) return true;
  auto empties_ok = [&](int hand_after, const Meld& probe) {
    if (hand_after > 0) return true;
    int xi = HuXiOfMeld(probe);
    for (const auto& mm : st.melds) xi += HuXiOfMeld(mm);
    return static_cast<int>(st.melds.size()) + 1 == 7 && xi >= cfg_.min_hu_xi;
  };
  if (CanPeng(st.hand, tile)) {
    Meld probe;
    probe.kind = MeldKind::kPeng;
    probe.tile = tile;
    probe.tiles = {tile, tile, tile};
    if (empties_ok(HandSize(st.hand) - 2, probe)) return true;
  }
  // chi only next seat (skip if lou_chi blocked this tile)
  if (seat == NextSeat(from) && !(cfg_.lou_chi && st.lou_chi.count(tile))) {
    ChiOption opt;
    if (CanFormChi(st.hand, tile, &opt)) {
      Meld probe;
      probe.kind = MeldKind::kChi;
      probe.tile = tile;
      probe.tiles = ChiTiles(tile, opt);
      if (empties_ok(HandSize(st.hand) - 2, probe)) return true;
    }
  }
  return false;
}

void PhzTable::ResolveClaimPassAll() {
  const int from = claim_from_reveal_ ? last_reveal_seat_ : last_discard_seat_;
  last_discard_ = kTileInvalid;
  last_reveal_ = kTileInvalid;
  BeginDraw(NextSeat(from));
}

bool PhzTable::OnDiscard(int seat, TileId tile) {
  if (phase_ != Phase::kPlay || sub_ != PlaySub::kDiscard || seat != turn_seat_) return false;
  if (!IsValidTile(tile)) return false;
  if (!RemoveTile(seats_[static_cast<size_t>(seat)].hand, tile, 1)) return false;
  seats_[static_cast<size_t>(seat)].lou_chi.clear();
  Emit(OutEvent{"Discard", seat, tile, "", {}, -1, 0, 0});
  OpenClaimFromDiscard(seat, tile);
  return true;
}

bool PhzTable::OnAction(int seat, ActionKind act, const ChiOption* chi) {
  if (phase_ != Phase::kPlay) return false;

  if (sub_ == PlaySub::kDiscard && act == ActionKind::kHu) {
    // manual hu without new tile not supported
    return false;
  }

  if (sub_ != PlaySub::kClaimWindow) return false;
  if (seat < 0 || seat >= kSeats) return false;
  if (claim_responded_[static_cast<size_t>(seat)]) return false;

  const TileId tile = claim_from_reveal_ ? last_reveal_ : last_discard_;
  const int from = claim_from_reveal_ ? last_reveal_seat_ : last_discard_seat_;
  auto& st = seats_[static_cast<size_t>(seat)];

  if (act == ActionKind::kHu) {
    const HuFrom hf = claim_from_reveal_ ? HuFrom::kReveal : HuFrom::kDiscard;
    auto hu = CheckHu(st.hand, st.melds, tile, hf, cfg_, HasPaoOrTi(seat));
    if (!hu.ok) return false;
    claim_responded_[static_cast<size_t>(seat)] = true;
    claim_choice_[static_cast<size_t>(seat)] = ActionKind::kHu;
  } else if (act == ActionKind::kPeng) {
    if (!CanPeng(st.hand, tile)) return false;
    // Peng uses 2 hand tiles; emptying hand requires completing a valid 7-meld hu.
    if (HandSize(st.hand) == 2) {
      Meld probe;
      probe.kind = MeldKind::kPeng;
      probe.tile = tile;
      probe.tiles = {tile, tile, tile};
      int xi = HuXiOfMeld(probe);
      for (const auto& mm : st.melds) xi += HuXiOfMeld(mm);
      if (static_cast<int>(st.melds.size()) + 1 != 7 || xi < cfg_.min_hu_xi) return false;
    }
    claim_responded_[static_cast<size_t>(seat)] = true;
    claim_choice_[static_cast<size_t>(seat)] = ActionKind::kPeng;
  } else if (act == ActionKind::kChi) {
    if (seat != NextSeat(from)) return false;
    if (cfg_.lou_chi && st.lou_chi.count(tile)) return false;
    ChiOption opt;
    HandCount after = st.hand;
    if (chi && chi->hand_tiles.size() == 2) {
      opt = *chi;
      if (!RemoveTile(after, opt.hand_tiles[0], 1) || !RemoveTile(after, opt.hand_tiles[1], 1)) return false;
    } else if (!CanFormChi(st.hand, tile, &opt)) {
      return false;
    } else {
      if (!RemoveTile(after, opt.hand_tiles[0], 1) || !RemoveTile(after, opt.hand_tiles[1], 1)) return false;
    }
    if (HandSize(after) == 0) {
      Meld probe;
      probe.kind = MeldKind::kChi;
      probe.tile = tile;
      probe.tiles = ChiTiles(tile, opt);
      int xi = HuXiOfMeld(probe);
      for (const auto& mm : st.melds) xi += HuXiOfMeld(mm);
      if (static_cast<int>(st.melds.size()) + 1 != 7 || xi < cfg_.min_hu_xi) return false;
    }
    claim_responded_[static_cast<size_t>(seat)] = true;
    claim_choice_[static_cast<size_t>(seat)] = ActionKind::kChi;
    claim_chi_[static_cast<size_t>(seat)] = opt;
  } else {
    claim_responded_[static_cast<size_t>(seat)] = true;
    claim_choice_[static_cast<size_t>(seat)] = ActionKind::kPass;
    if (cfg_.lou_chi && seat == NextSeat(from)) st.lou_chi.insert(tile);
  }

  // Wait until all who need input responded; others already marked
  for (int s = 0; s < kSeats; ++s) {
    if (SeatNeedsClaimInput(s)) return true;
    if (!claim_responded_[static_cast<size_t>(s)] && s != from) {
      // seats that never needed input: mark passed
      if (!SeatNeedsClaimInput(s)) claim_responded_[static_cast<size_t>(s)] = true;
    }
  }
  for (int s = 0; s < kSeats; ++s) {
    if (s == from) continue;
    if (!claim_responded_[static_cast<size_t>(s)]) return true;
  }

  // Resolve priority: Hu > Peng > Chi
  int hu_seat = -1;
  // next seat from from first for multi-hu
  for (int i = 0; i < kSeats; ++i) {
    const int s = (from + 1 + i) % kSeats;
    if (claim_choice_[static_cast<size_t>(s)] == ActionKind::kHu) {
      hu_seat = s;
      break;
    }
  }
  if (hu_seat >= 0) {
    FinishHu(hu_seat, tile, claim_from_reveal_ ? HuFrom::kReveal : HuFrom::kDiscard);
    return true;
  }

  int peng_seat = -1;
  for (int s = 0; s < kSeats; ++s) {
    if (claim_choice_[static_cast<size_t>(s)] == ActionKind::kPeng) {
      peng_seat = s;
      break;
    }
  }
  if (peng_seat >= 0) {
    auto& pst = seats_[static_cast<size_t>(peng_seat)];
    Meld m;
    m.kind = MeldKind::kPeng;
    m.tile = tile;
    m.tiles = {tile, tile, tile};
    m.from_seat = from;
    const int hand_n = HandSize(pst.hand);
    if (hand_n == 2) {
      int xi = HuXiOfMeld(m);
      for (const auto& mm : pst.melds) xi += HuXiOfMeld(mm);
      const int menzi = static_cast<int>(pst.melds.size()) + 1;
      if (menzi != 7 || xi < cfg_.min_hu_xi) {
        // Would empty hand without valid hu — ignore peng, continue resolve.
        peng_seat = -1;
      } else {
        RemoveTile(pst.hand, tile, 2);
        pst.melds.push_back(m);
        Emit(OutEvent{"Peng", peng_seat, tile, "", m.tiles, from, static_cast<int>(MeldKind::kPeng), 2});
        last_discard_ = kTileInvalid;
        last_reveal_ = kTileInvalid;
        FinishHu(peng_seat, tile, claim_from_reveal_ ? HuFrom::kReveal : HuFrom::kDiscard);
        return true;
      }
    }
    if (peng_seat >= 0) {
      RemoveTile(pst.hand, tile, 2);
      pst.melds.push_back(m);
      Emit(OutEvent{"Peng", peng_seat, tile, "", m.tiles, from, static_cast<int>(MeldKind::kPeng), 2});
      last_discard_ = kTileInvalid;
      last_reveal_ = kTileInvalid;
      BeginDiscard(peng_seat);
      return true;
    }
  }

  const int chi_seat = NextSeat(from);
  if (claim_choice_[static_cast<size_t>(chi_seat)] == ActionKind::kChi) {
    const auto& opt = claim_chi_[static_cast<size_t>(chi_seat)];
    auto& cst = seats_[static_cast<size_t>(chi_seat)];
    HandCount tmp = cst.hand;
    if (!RemoveTile(tmp, opt.hand_tiles[0], 1) || !RemoveTile(tmp, opt.hand_tiles[1], 1)) {
      ResolveClaimPassAll();
      return true;
    }
    Meld m;
    m.kind = MeldKind::kChi;
    m.tile = tile;
    m.tiles = ChiTiles(tile, opt);
    m.from_seat = from;
    if (HandSize(tmp) == 0) {
      int xi = HuXiOfMeld(m);
      for (const auto& mm : cst.melds) xi += HuXiOfMeld(mm);
      const int menzi = static_cast<int>(cst.melds.size()) + 1;
      if (menzi == 7 && xi >= cfg_.min_hu_xi) {
        cst.hand = tmp;
        cst.melds.push_back(m);
        Emit(OutEvent{"Chi", chi_seat, tile, "", m.tiles, from, static_cast<int>(MeldKind::kChi), 1});
        last_discard_ = kTileInvalid;
        last_reveal_ = kTileInvalid;
        FinishHu(chi_seat, tile, claim_from_reveal_ ? HuFrom::kReveal : HuFrom::kDiscard);
        return true;
      }
      // Chi would empty hand without valid hu — treat as pass.
      ResolveClaimPassAll();
      return true;
    }
    cst.hand = tmp;
    cst.melds.push_back(m);
    Emit(OutEvent{"Chi", chi_seat, tile, "", m.tiles, from, static_cast<int>(MeldKind::kChi), 1});
    last_discard_ = kTileInvalid;
    last_reveal_ = kTileInvalid;
    BeginDiscard(chi_seat);
    return true;
  }

  ResolveClaimPassAll();
  return true;
}

TileId PhzTable::SmallestHandTile(int seat) const {
  for (TileId t = 0; t < kTileKinds; ++t) {
    if (seats_[static_cast<size_t>(seat)].hand[static_cast<size_t>(t)] > 0) return t;
  }
  return kTileInvalid;
}

bool PhzTable::EmptyHandSevenMeldsHu(int seat) const {
  if (seat < 0 || seat >= kSeats) return false;
  const auto& st = seats_[static_cast<size_t>(seat)];
  if (HandSize(st.hand) != 0) return false;
  if (static_cast<int>(st.melds.size()) != 7) return false;
  int xi = 0;
  for (const auto& m : st.melds) xi += HuXiOfMeld(m);
  return xi >= cfg_.min_hu_xi;
}

void PhzTable::OnTimeout(int seat) {
  if (phase_ != Phase::kPlay) return;
  if (sub_ == PlaySub::kDiscard && seat == turn_seat_) {
    const TileId t = SmallestHandTile(seat);
    if (IsValidTile(t)) {
      OnDiscard(seat, t);
      return;
    }
    // Empty-hand stuck (illegal prior chi/peng): hu if possible, else liu ju escape.
    if (EmptyHandSevenMeldsHu(seat)) {
      const TileId win = seats_[static_cast<size_t>(seat)].melds.back().tile;
      FinishHu(seat, win, has_pending_reveal() ? HuFrom::kReveal : HuFrom::kDiscard);
    } else {
      FinishLiuJu();
    }
    return;
  }
  if (sub_ == PlaySub::kClaimWindow && SeatNeedsClaimInput(seat)) {
    OnAction(seat, ActionKind::kPass, nullptr);
  }
}

}  // namespace phz
}  // namespace pandora
