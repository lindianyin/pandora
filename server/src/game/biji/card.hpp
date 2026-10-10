#pragma once

#include <cstdint>

namespace pandora {
namespace biji {

// CardId = suit * 13 + rank; suit 0=D 1=C 2=H 3=S; rank 0=2 .. 12=A
using CardId = int32_t;

inline constexpr int SuitOf(CardId c) { return static_cast<int>(c / 13); }
inline constexpr int RankOf(CardId c) { return static_cast<int>(c % 13); }
inline constexpr CardId MakeCard(int suit, int rank) {
  return static_cast<CardId>(suit * 13 + rank);
}
inline constexpr bool IsRed(CardId c) {
  const int s = SuitOf(c);
  return s == 0 || s == 2;
}
inline constexpr bool IsBlack(CardId c) {
  const int s = SuitOf(c);
  return s == 1 || s == 3;
}

}  // namespace biji
}  // namespace pandora
