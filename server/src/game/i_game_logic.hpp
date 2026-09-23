#pragma once
#include <cstdint>
#include <functional>
#include <string>
namespace pandora {
using SeatId = int32_t;
// SPEC §3.2 stub for M2
struct IGameLogic {
  virtual ~IGameLogic() = default;
  virtual void OnPlayerEnter(SeatId) = 0;
  virtual void OnPlayerReady(SeatId, bool ready) = 0;
  virtual void OnGameStart() = 0;
  virtual void OnTimeout(SeatId) = 0;
  virtual void OnDestroy() = 0;
};
}  // namespace pandora
