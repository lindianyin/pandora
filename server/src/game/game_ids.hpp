#pragma once

#include <cstdint>

namespace pandora {
namespace GameId {

// Four-digit game ids: msg_id = game_id * 100 + slot, err = game_id * 1000 + slot.
constexpr int32_t kMin = 1000;
constexpr int32_t kMax = 9999;

constexpr int32_t kDdz = 2000;
constexpr int32_t kHzmj = 3000;
constexpr int32_t kPhz = 4000;
constexpr int32_t kFish = 5000;
constexpr int32_t kBiji = 6000;

}  // namespace GameId

inline constexpr uint32_t kPlayMsgMin = 100000u;

inline uint32_t MsgBase(int32_t game_id) { return static_cast<uint32_t>(game_id) * 100u; }

inline uint32_t MsgOf(int32_t game_id, uint32_t slot) { return MsgBase(game_id) + slot; }

inline int32_t GameIdOfMsg(uint32_t msg_id) {
  if (msg_id < kPlayMsgMin) return 0;
  return static_cast<int32_t>(msg_id / 100u);
}

inline bool IsPlayMsg(uint32_t msg_id) { return msg_id >= kPlayMsgMin; }

inline int32_t DefaultSeatsForGame(int32_t game_id) {
  if (game_id == GameId::kHzmj || game_id == GameId::kFish || game_id == GameId::kBiji) return 4;
  return 3;
}

}  // namespace pandora
