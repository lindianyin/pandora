#pragma once

#include "game/sss/hand.hpp"

#include <cstdint>

namespace pandora {
namespace sss {

enum class SpecialType : int8_t {
  kNone = 0,
  kThreeFlush = 1,
  kThreeStraight = 2,
  kSixPair = 3,
  kFivePairThree = 4,
  kFourThree = 5,
  kOneColor = 6,
  kAllSmall = 7,
  kAllBig = 8,
  kThreeFour = 9,
  kThreeSf = 10,
  kTwelveRoyals = 11,
  kDragon = 12,
  kNaturalDragon = 13,
};

int32_t SpecialWater(SpecialType t);
SpecialType DetectSpecial(const Hand13& hand, bool enable);

}  // namespace sss
}  // namespace pandora
