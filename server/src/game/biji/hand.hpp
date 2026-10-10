#pragma once

#include "game/biji/card.hpp"

#include <array>
#include <cstdint>

namespace pandora {
namespace biji {

enum class DunType : int8_t {
  kHigh = 0,
  kPair = 1,
  kStraight = 2,
  kFlush = 3,
  kStraightFlush = 4,
  kThree = 5,
};

struct DunKey {
  int8_t type{0};
  int8_t primary{0};
  int8_t secondary{0};
  int8_t tertiary{0};
  int8_t suit0{0};
  int8_t suit1{0};
  int8_t suit2{0};
};

using DunCards = std::array<CardId, 3>;
using Hand9 = std::array<CardId, 9>;

struct Arrange {
  DunCards head{};
  DunCards mid{};
  DunCards tail{};
};

DunKey EvalDun(const DunCards& cards);
int CompareDun(const DunKey& a, const DunKey& b);
int CompareDun(const DunCards& a, const DunCards& b);
bool IsLegalArrange(const DunCards& head, const DunCards& mid, const DunCards& tail);
Arrange AutoArrange(const Hand9& hand);

}  // namespace biji
}  // namespace pandora
