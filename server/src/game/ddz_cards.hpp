#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace pandora {
namespace ddz {

// Card encoding: 0-51 = 4 suits * 13 ranks (rank 0=3 ... 11=A, 12=2), 52=SJ, 53=BJ
inline int CardRank(int card) {
  if (card == 52) return 13;
  if (card == 53) return 14;
  return card % 13;
}

enum class PatternType {
  kInvalid = 0,
  kSingle,
  kPair,
  kTriple,
  kTripleOne,
  kTripleTwo,
  kStraight,
  kDoubleStraight,
  kPlane,
  kPlaneSingle,
  kPlanePair,
  kFourTwo,
  kBomb,
  kRocket,
};

struct Pattern {
  PatternType type{PatternType::kInvalid};
  int main_rank{-1};  // primary compare rank
  int length{0};      // for sequences / plane count
};

Pattern Identify(std::vector<int> cards);
bool CanBeat(const Pattern& cur, const Pattern& prev);
bool ContainsAll(const std::vector<int>& hand, const std::vector<int>& play);
void RemoveCards(std::vector<int>& hand, const std::vector<int>& play);
std::map<int, int> CountByRank(const std::vector<int>& cards);

}  // namespace ddz
}  // namespace pandora

