#pragma once

#include "game/sss/card.hpp"

#include <array>
#include <cstdint>

namespace pandora {
namespace sss {

// Five-card levels (SPEC §4.1). Head uses subset {THREE=3, PAIR=1, HIGH=0}.
enum class FiveLevel : int8_t {
  kHigh = 0,
  kPair = 1,
  kTwoPair = 2,
  kThree = 3,
  kStraight = 4,
  kFlush = 5,
  kFullHouse = 6,
  kFour = 7,
  kStraightFlush = 8,
};

struct FiveKey {
  int8_t level{0};
  int8_t p0{-128};
  int8_t p1{-128};
  int8_t p2{-128};
  int8_t p3{-128};
  int8_t p4{-128};
};

struct HeadKey {
  int8_t level{0};  // 0,1,3
  int8_t p0{-128};
  int8_t p1{-128};
  int8_t p2{-128};
};

using HeadCards = std::array<CardId, 3>;
using FiveCards = std::array<CardId, 5>;
using Hand13 = std::array<CardId, 13>;

struct Arrange {
  HeadCards head{};
  FiveCards mid{};
  FiveCards tail{};
};

FiveKey EvalFive(const FiveCards& cards);
HeadKey EvalHead(const HeadCards& cards);
int CompareFive(const FiveKey& a, const FiveKey& b);
int CompareFive(const FiveCards& a, const FiveCards& b);
int CompareHead(const HeadKey& a, const HeadKey& b);
int CompareHead(const HeadCards& a, const HeadCards& b);
int CmpOrderHeadFive(const HeadKey& head, const FiveKey& five);
int CmpOrderFiveFive(const FiveKey& a, const FiveKey& b);
bool IsLegalArrange(const HeadCards& head, const FiveCards& mid, const FiveCards& tail);
bool SameMultiset13(const Hand13& hand, const HeadCards& head, const FiveCards& mid,
                    const FiveCards& tail);
Arrange AutoArrange(const Hand13& hand);

}  // namespace sss
}  // namespace pandora
