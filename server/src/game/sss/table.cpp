#include "game/sss/table.hpp"

#include "common/errors.hpp"

#include <algorithm>
#include <set>

namespace pandora {
namespace sss {

SssTable::SssTable(SssConfig cfg, Rng rng) : cfg_(cfg), rng_(std::move(rng)) {
  n_ = cfg_.players;
  if (n_ < 2) n_ = 2;
  if (n_ > 4) n_ = 4;
  cfg_.players = n_;
}

void SssTable::SetSeatUid(int seat, int64_t uid) {
  if (seat < 0 || seat >= n_) return;
  seats_[static_cast<size_t>(seat)].uid = uid;
}

void SssTable::OnReady(int seat, bool ready) {
  if (seat < 0 || seat >= n_) return;
  if (phase_ != Phase::kWaitReady) return;
  seats_[static_cast<size_t>(seat)].ready = ready;
}

bool SssTable::AllLocked() const {
  for (int i = 0; i < n_; ++i) {
    if (!seats_[static_cast<size_t>(i)].locked) return false;
  }
  return true;
}

bool SssTable::HandContainsArrange(const Hand13& hand, const HeadCards& head, const FiveCards& mid,
                                   const FiveCards& tail) const {
  return SameMultiset13(hand, head, mid, tail);
}

void SssTable::DealCards() {
  std::vector<CardId> deck;
  deck.reserve(52);
  for (int s = 0; s < 4; ++s)
    for (int r = 0; r < 13; ++r) deck.push_back(MakeCard(s, r));
  for (int i = static_cast<int>(deck.size()) - 1; i > 0; --i) {
    const uint32_t x = rng_ ? rng_() : static_cast<uint32_t>(i);
    const int j = static_cast<int>(x % static_cast<uint32_t>(i + 1));
    std::swap(deck[static_cast<size_t>(i)], deck[static_cast<size_t>(j)]);
  }
  if (last_water_winner_ < 0) {
    deal_start_ = static_cast<int>((rng_ ? rng_() : 0u) % static_cast<uint32_t>(n_));
  } else {
    deal_start_ = (last_water_winner_ + 1) % n_;
  }
  int idx = 0;
  for (int round = 0; round < 13; ++round) {
    for (int k = 0; k < n_; ++k) {
      const int seat = (deal_start_ + k) % n_;
      seats_[static_cast<size_t>(seat)].hand[static_cast<size_t>(round)] =
          deck[static_cast<size_t>(idx++)];
    }
  }
  for (int i = 0; i < n_; ++i) {
    auto& s = seats_[static_cast<size_t>(i)];
    s.locked = false;
    s.has_draft = false;
    s.ready = false;
    s.trusteeship = false;
    s.report_special = false;
    s.special = SpecialType::kNone;
  }
}

bool SssTable::TryStartDeal(int64_t now_ms) {
  if (phase_ != Phase::kWaitReady) return false;
  for (int i = 0; i < n_; ++i) {
    if (!seats_[static_cast<size_t>(i)].ready) return false;
  }
  phase_ = Phase::kDeal;
  DealCards();
  phase_ = Phase::kArrange;
  arrange_deadline_ms_ = now_ms + static_cast<int64_t>(cfg_.arrange_timeout_s) * 1000;
  return true;
}

int SssTable::SetArrange(int seat, const HeadCards& head, const FiveCards& mid, const FiveCards& tail,
                         bool report_special, bool confirm) {
  if (seat < 0 || seat >= n_) return static_cast<int>(Err::kSssBadSeat);
  if (phase_ != Phase::kArrange) return static_cast<int>(Err::kSssBadPhase);
  auto& s = seats_[static_cast<size_t>(seat)];
  if (s.locked) return static_cast<int>(Err::kSssAlreadyLocked);
  if (!HandContainsArrange(s.hand, head, mid, tail))
    return static_cast<int>(Err::kSssIllegalArrange);

  SpecialType st = SpecialType::kNone;
  if (report_special) {
    st = DetectSpecial(s.hand, cfg_.enable_special);
    if (st == SpecialType::kNone) return static_cast<int>(Err::kSssBadSpecial);
  } else {
    if (!IsLegalArrange(head, mid, tail)) return static_cast<int>(Err::kSssDaoShui);
  }

  s.draft_head = head;
  s.draft_mid = mid;
  s.draft_tail = tail;
  s.has_draft = true;
  s.report_special = report_special;
  s.special = st;
  if (confirm) {
    s.final_head = head;
    s.final_mid = mid;
    s.final_tail = tail;
    s.locked = true;
    if (AllLocked()) FinishCompareAndSettle();
  }
  return 0;
}

void SssTable::OnDisconnect(int seat) {
  if (seat < 0 || seat >= n_) return;
  seats_[static_cast<size_t>(seat)].online = false;
  seats_[static_cast<size_t>(seat)].trusteeship = true;
}

void SssTable::OnReconnect(int seat) {
  if (seat < 0 || seat >= n_) return;
  seats_[static_cast<size_t>(seat)].online = true;
}

void SssTable::OnArrangeDeadline(int64_t /*now_ms*/) {
  if (phase_ != Phase::kArrange) return;
  for (int i = 0; i < n_; ++i) {
    auto& s = seats_[static_cast<size_t>(i)];
    if (s.locked) continue;
    // Never auto special report.
    s.report_special = false;
    s.special = SpecialType::kNone;
    Arrange a;
    if (s.has_draft && !s.report_special && IsLegalArrange(s.draft_head, s.draft_mid, s.draft_tail) &&
        HandContainsArrange(s.hand, s.draft_head, s.draft_mid, s.draft_tail)) {
      a = {s.draft_head, s.draft_mid, s.draft_tail};
    } else {
      a = AutoArrange(s.hand);
    }
    s.final_head = a.head;
    s.final_mid = a.mid;
    s.final_tail = a.tail;
    s.draft_head = a.head;
    s.draft_mid = a.mid;
    s.draft_tail = a.tail;
    s.has_draft = true;
    s.locked = true;
  }
  FinishCompareAndSettle();
}

void SssTable::FinishCompareAndSettle() {
  phase_ = Phase::kCompare;
  std::vector<SeatArrange> arr(static_cast<size_t>(n_));
  std::vector<int64_t> uids(static_cast<size_t>(n_));
  for (int i = 0; i < n_; ++i) {
    auto& seat = seats_[static_cast<size_t>(i)];
    SeatArrange sa;
    sa.head = seat.final_head;
    sa.mid = seat.final_mid;
    sa.tail = seat.final_tail;
    sa.report_special = seat.report_special;
    sa.special = seat.special;
    sa.dao_shui = false;
    if (!seat.report_special && !IsLegalArrange(seat.final_head, seat.final_mid, seat.final_tail)) {
      sa.dao_shui = true;
    }
    arr[static_cast<size_t>(i)] = sa;
    uids[static_cast<size_t>(i)] = seat.uid;
  }
  plan_ = ComputeSettle(arr, uids, cfg_);

  // Next deal_start: max gross_water, tie break lower seat.
  last_water_winner_ = 0;
  int32_t best = plan_.seats.empty() ? 0 : plan_.seats[0].gross_water;
  for (int i = 1; i < n_; ++i) {
    if (plan_.seats[static_cast<size_t>(i)].gross_water > best) {
      best = plan_.seats[static_cast<size_t>(i)].gross_water;
      last_water_winner_ = i;
    }
  }

  phase_ = Phase::kSettle;
  settlement_pending_ = true;
  phase_ = Phase::kWaitReady;
  for (int i = 0; i < n_; ++i) {
    seats_[static_cast<size_t>(i)].ready = false;
    seats_[static_cast<size_t>(i)].locked = false;
    seats_[static_cast<size_t>(i)].has_draft = false;
    seats_[static_cast<size_t>(i)].report_special = false;
    seats_[static_cast<size_t>(i)].special = SpecialType::kNone;
  }
}

bool SssTable::TakeSettlement(SettlePlan* out) {
  if (!settlement_pending_ || !out) return false;
  *out = plan_;
  settlement_pending_ = false;
  return true;
}

Snapshot SssTable::BuildSnapshot(int seat, int64_t now_ms) const {
  Snapshot sn;
  sn.phase = phase_;
  sn.n = n_;
  sn.deal_start = deal_start_;
  sn.remain_ms = 0;
  if (phase_ == Phase::kArrange && arrange_deadline_ms_ > now_ms)
    sn.remain_ms = arrange_deadline_ms_ - now_ms;
  if (seat < 0 || seat >= n_) return sn;
  const auto& s = seats_[static_cast<size_t>(seat)];
  sn.hand = s.hand;
  sn.draft_head = s.draft_head;
  sn.draft_mid = s.draft_mid;
  sn.draft_tail = s.draft_tail;
  sn.has_draft = s.has_draft;
  sn.locked = s.locked;
  sn.report_special = s.report_special;
  sn.special = s.special;
  sn.others_locked.resize(static_cast<size_t>(n_));
  sn.others_reported.resize(static_cast<size_t>(n_));
  for (int i = 0; i < n_; ++i) {
    sn.others_locked[static_cast<size_t>(i)] = seats_[static_cast<size_t>(i)].locked;
    sn.others_reported[static_cast<size_t>(i)] = seats_[static_cast<size_t>(i)].report_special &&
                                                 seats_[static_cast<size_t>(i)].locked;
  }
  return sn;
}

}  // namespace sss
}  // namespace pandora
