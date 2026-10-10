#include "game/biji/table.hpp"

#include "common/errors.hpp"

#include <algorithm>
#include <set>

namespace pandora {
namespace biji {

BijiTable::BijiTable(BijiConfig cfg, Rng rng) : cfg_(cfg), rng_(std::move(rng)) {
  n_ = cfg_.players;
  if (n_ < 2) n_ = 2;
  if (n_ > 4) n_ = 4;
  cfg_.players = n_;
}

void BijiTable::SetSeatUid(int seat, int64_t uid) {
  if (seat < 0 || seat >= n_) return;
  seats_[static_cast<size_t>(seat)].uid = uid;
}

void BijiTable::OnReady(int seat, bool ready) {
  if (seat < 0 || seat >= n_) return;
  if (phase_ != Phase::kWaitReady) return;
  seats_[static_cast<size_t>(seat)].ready = ready;
}

bool BijiTable::AllLocked() const {
  for (int i = 0; i < n_; ++i) {
    if (!seats_[static_cast<size_t>(i)].locked) return false;
  }
  return true;
}

bool BijiTable::HandContainsArrange(const Hand9& hand, const DunCards& head, const DunCards& mid,
                                    const DunCards& tail) const {
  std::multiset<CardId> need;
  for (auto c : hand) need.insert(c);
  auto take = [&](const DunCards& d) {
    for (auto c : d) {
      auto it = need.find(c);
      if (it == need.end()) return false;
      need.erase(it);
    }
    return true;
  };
  return take(head) && take(mid) && take(tail) && need.empty();
}

void BijiTable::DealCards() {
  std::vector<CardId> deck;
  deck.reserve(52);
  for (int s = 0; s < 4; ++s)
    for (int r = 0; r < 13; ++r) deck.push_back(MakeCard(s, r));
  for (int i = static_cast<int>(deck.size()) - 1; i > 0; --i) {
    const uint32_t x = rng_ ? rng_() : static_cast<uint32_t>(i);
    const int j = static_cast<int>(x % static_cast<uint32_t>(i + 1));
    std::swap(deck[static_cast<size_t>(i)], deck[static_cast<size_t>(j)]);
  }
  if (last_tail_winner_ < 0) {
    deal_start_ = static_cast<int>((rng_ ? rng_() : 0u) % static_cast<uint32_t>(n_));
  } else {
    deal_start_ = (last_tail_winner_ + 1) % n_;
  }
  int idx = 0;
  for (int round = 0; round < 9; ++round) {
    for (int k = 0; k < n_; ++k) {
      const int seat = (deal_start_ + k) % n_;
      seats_[static_cast<size_t>(seat)].hand[static_cast<size_t>(round)] = deck[static_cast<size_t>(idx++)];
    }
  }
  for (int i = 0; i < n_; ++i) {
    auto& s = seats_[static_cast<size_t>(i)];
    s.locked = false;
    s.has_draft = false;
    s.ready = false;
    s.trusteeship = false;
  }
}

bool BijiTable::TryStartDeal(int64_t now_ms) {
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

int BijiTable::SetArrange(int seat, const DunCards& head, const DunCards& mid, const DunCards& tail,
                          bool confirm) {
  if (seat < 0 || seat >= n_) return static_cast<int>(Err::kBijiBadSeat);
  if (phase_ != Phase::kArrange) return static_cast<int>(Err::kBijiBadPhase);
  auto& s = seats_[static_cast<size_t>(seat)];
  if (s.locked) return static_cast<int>(Err::kBijiAlreadyLocked);
  if (!HandContainsArrange(s.hand, head, mid, tail))
    return static_cast<int>(Err::kBijiIllegalArrange);
  if (!IsLegalArrange(head, mid, tail)) return static_cast<int>(Err::kBijiDaoShui);
  s.draft_head = head;
  s.draft_mid = mid;
  s.draft_tail = tail;
  s.has_draft = true;
  if (confirm) {
    s.final_head = head;
    s.final_mid = mid;
    s.final_tail = tail;
    s.locked = true;
    if (AllLocked()) FinishCompareAndSettle();
  }
  return 0;
}

void BijiTable::OnDisconnect(int seat) {
  if (seat < 0 || seat >= n_) return;
  seats_[static_cast<size_t>(seat)].online = false;
  seats_[static_cast<size_t>(seat)].trusteeship = true;
}

void BijiTable::OnReconnect(int seat) {
  if (seat < 0 || seat >= n_) return;
  seats_[static_cast<size_t>(seat)].online = true;
}

void BijiTable::OnArrangeDeadline(int64_t /*now_ms*/) {
  if (phase_ != Phase::kArrange) return;
  for (int i = 0; i < n_; ++i) {
    auto& s = seats_[static_cast<size_t>(i)];
    if (s.locked) continue;
    Arrange a;
    if (s.has_draft && IsLegalArrange(s.draft_head, s.draft_mid, s.draft_tail) &&
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

void BijiTable::FinishCompareAndSettle() {
  phase_ = Phase::kCompare;
  std::vector<SeatArrange> arr(static_cast<size_t>(n_));
  std::vector<int64_t> uids(static_cast<size_t>(n_));
  for (int i = 0; i < n_; ++i) {
    arr[static_cast<size_t>(i)] = {seats_[static_cast<size_t>(i)].final_head,
                                   seats_[static_cast<size_t>(i)].final_mid,
                                   seats_[static_cast<size_t>(i)].final_tail};
    uids[static_cast<size_t>(i)] = seats_[static_cast<size_t>(i)].uid;
  }
  plan_ = ComputeSettle(arr, uids, cfg_.base_score, cfg_.rake_bp, cfg_.enable_chixi);

  // Tail winner for next deal_start.
  std::vector<DunCards> tails(static_cast<size_t>(n_));
  for (int i = 0; i < n_; ++i) tails[static_cast<size_t>(i)] = seats_[static_cast<size_t>(i)].final_tail;
  std::vector<int64_t> deltas;
  std::vector<int> places;
  SettleOneDun(tails, cfg_.base_score, &deltas, &places);
  last_tail_winner_ = 0;
  for (int i = 0; i < n_; ++i) {
    if (places[static_cast<size_t>(i)] == 1) {
      last_tail_winner_ = i;
      break;
    }
  }

  phase_ = Phase::kSettle;
  settlement_pending_ = true;
  // Ready for next round after consumer reads plan.
  phase_ = Phase::kWaitReady;
  for (int i = 0; i < n_; ++i) {
    seats_[static_cast<size_t>(i)].ready = false;
    seats_[static_cast<size_t>(i)].locked = false;
    seats_[static_cast<size_t>(i)].has_draft = false;
  }
}

bool BijiTable::TakeSettlement(SettlePlan* out) {
  if (!settlement_pending_ || !out) return false;
  *out = plan_;
  settlement_pending_ = false;
  return true;
}

Snapshot BijiTable::BuildSnapshot(int seat, int64_t now_ms) const {
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
  sn.others_locked.resize(static_cast<size_t>(n_));
  for (int i = 0; i < n_; ++i) sn.others_locked[static_cast<size_t>(i)] = seats_[static_cast<size_t>(i)].locked;
  return sn;
}

}  // namespace biji
}  // namespace pandora
