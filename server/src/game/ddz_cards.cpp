#include "game/ddz_cards.hpp"

#include <algorithm>

namespace pandora {
namespace ddz {

std::map<int, int> CountByRank(const std::vector<int>& cards) {
  std::map<int, int> m;
  for (int c : cards) m[CardRank(c)]++;
  return m;
}

bool ContainsAll(const std::vector<int>& hand, const std::vector<int>& play) {
  std::map<int, int> need;
  for (int c : play) need[c]++;
  std::map<int, int> have;
  for (int c : hand) have[c]++;
  for (const auto& kv : need) {
    if (have[kv.first] < kv.second) return false;
  }
  return true;
}

void RemoveCards(std::vector<int>& hand, const std::vector<int>& play) {
  for (int c : play) {
    auto it = std::find(hand.begin(), hand.end(), c);
    if (it != hand.end()) hand.erase(it);
  }
}

namespace {

bool IsConsecutive(const std::vector<int>& ranks_sorted_asc) {
  if (ranks_sorted_asc.empty()) return false;
  for (size_t i = 1; i < ranks_sorted_asc.size(); ++i) {
    if (ranks_sorted_asc[i] != ranks_sorted_asc[i - 1] + 1) return false;
  }
  // cannot include 2 (rank 12) or jokers in straights
  if (ranks_sorted_asc.back() >= 12) return false;
  return true;
}

std::vector<int> RanksWithCount(const std::map<int, int>& cnt, int n) {
  std::vector<int> out;
  for (const auto& kv : cnt) {
    if (kv.second == n) out.push_back(kv.first);
  }
  std::sort(out.begin(), out.end());
  return out;
}

}  // namespace

Pattern Identify(std::vector<int> cards) {
  Pattern p;
  if (cards.empty()) return p;
  std::sort(cards.begin(), cards.end());
  const auto cnt = CountByRank(cards);
  const int n = static_cast<int>(cards.size());

  if (n == 2 && cards[0] == 52 && cards[1] == 53) {
    p.type = PatternType::kRocket;
    p.main_rank = 14;
    p.length = 1;
    return p;
  }
  if (n == 4) {
    for (const auto& kv : cnt) {
      if (kv.second == 4) {
        p.type = PatternType::kBomb;
        p.main_rank = kv.first;
        p.length = 1;
        return p;
      }
    }
  }
  if (n == 1) {
    p.type = PatternType::kSingle;
    p.main_rank = CardRank(cards[0]);
    p.length = 1;
    return p;
  }
  if (n == 2 && cnt.size() == 1) {
    p.type = PatternType::kPair;
    p.main_rank = cnt.begin()->first;
    p.length = 1;
    return p;
  }
  if (n == 3 && cnt.size() == 1) {
    p.type = PatternType::kTriple;
    p.main_rank = cnt.begin()->first;
    p.length = 1;
    return p;
  }
  if (n == 4) {
    auto triples = RanksWithCount(cnt, 3);
    auto singles = RanksWithCount(cnt, 1);
    if (triples.size() == 1 && singles.size() == 1) {
      p.type = PatternType::kTripleOne;
      p.main_rank = triples[0];
      p.length = 1;
      return p;
    }
  }
  if (n == 5) {
    auto triples = RanksWithCount(cnt, 3);
    auto pairs = RanksWithCount(cnt, 2);
    if (triples.size() == 1 && pairs.size() == 1) {
      p.type = PatternType::kTripleTwo;
      p.main_rank = triples[0];
      p.length = 1;
      return p;
    }
  }

  // straight of singles >= 5
  if (n >= 5 && static_cast<int>(cnt.size()) == n) {
    std::vector<int> ranks;
    for (const auto& kv : cnt) ranks.push_back(kv.first);
    if (IsConsecutive(ranks)) {
      p.type = PatternType::kStraight;
      p.main_rank = ranks.back();
      p.length = n;
      return p;
    }
  }

  // double straight >= 3 pairs
  if (n >= 6 && n % 2 == 0) {
    auto pairs = RanksWithCount(cnt, 2);
    if (static_cast<int>(pairs.size()) == n / 2 && IsConsecutive(pairs)) {
      p.type = PatternType::kDoubleStraight;
      p.main_rank = pairs.back();
      p.length = n / 2;
      return p;
    }
  }

  // plane (consecutive triples) without wings
  if (n >= 6 && n % 3 == 0) {
    auto triples = RanksWithCount(cnt, 3);
    if (static_cast<int>(triples.size()) == n / 3 && IsConsecutive(triples)) {
      p.type = PatternType::kPlane;
      p.main_rank = triples.back();
      p.length = static_cast<int>(triples.size());
      return p;
    }
  }

  // plane + single wings
  if (n >= 8) {
    auto triples = RanksWithCount(cnt, 3);
    if (triples.size() >= 2 && IsConsecutive(triples)) {
      const int k = static_cast<int>(triples.size());
      if (n == k * 4) {
        // remaining are singles (or broken pairs counted as singles by card count)
        int wing_cards = n - k * 3;
        if (wing_cards == k) {
          p.type = PatternType::kPlaneSingle;
          p.main_rank = triples.back();
          p.length = k;
          return p;
        }
      }
      if (n == k * 5) {
        auto pairs = RanksWithCount(cnt, 2);
        // pairs that are not part of triples
        int wing_pairs = 0;
        for (int r : pairs) {
          bool in_tri = false;
          for (int t : triples)
            if (t == r) in_tri = true;
          if (!in_tri) ++wing_pairs;
        }
        // also count from map: cards not in triple ranks should form k pairs
        int non_tri = 0;
        for (const auto& kv : cnt) {
          bool in_tri = false;
          for (int t : triples)
            if (t == kv.first) in_tri = true;
          if (!in_tri) non_tri += kv.second;
        }
        if (non_tri == k * 2) {
          p.type = PatternType::kPlanePair;
          p.main_rank = triples.back();
          p.length = k;
          return p;
        }
        (void)wing_pairs;
      }
    }
  }

  // four with two (singles or pairs)
  if (n == 6) {
    auto fours = RanksWithCount(cnt, 4);
    if (fours.size() == 1) {
      p.type = PatternType::kFourTwo;
      p.main_rank = fours[0];
      p.length = 1;
      return p;
    }
  }
  if (n == 8) {
    auto fours = RanksWithCount(cnt, 4);
    auto pairs = RanksWithCount(cnt, 2);
    if (fours.size() == 1 && pairs.size() == 2) {
      p.type = PatternType::kFourTwo;
      p.main_rank = fours[0];
      p.length = 2;
      return p;
    }
  }

  return p;
}

bool CanBeat(const Pattern& cur, const Pattern& prev) {
  if (cur.type == PatternType::kInvalid) return false;
  if (prev.type == PatternType::kInvalid) return true;  // lead
  if (cur.type == PatternType::kRocket) return true;
  if (prev.type == PatternType::kRocket) return false;
  if (cur.type == PatternType::kBomb) {
    if (prev.type != PatternType::kBomb) return true;
    return cur.main_rank > prev.main_rank;
  }
  if (prev.type == PatternType::kBomb) return false;
  if (cur.type != prev.type) return false;
  if (cur.length != prev.length) return false;
  return cur.main_rank > prev.main_rank;
}

}  // namespace ddz
}  // namespace pandora

