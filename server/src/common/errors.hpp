#pragma once

#include <cstdint>
#include <string>

namespace pandora {

enum class Err : int32_t {
  kOk = 0,
  kUnauthorized = 1001,
  kForbidden = 1002,
  kBadParam = 1003,
  kNotFound = 1004,
  kInsufficient = 2001,
  kActivityCannotClaim = 2003,
  kMaintain = 5000,
  kBanned = 5001,
  kInternal = 9999,
};

inline const char* ErrMessage(Err e) {
  switch (e) {
    case Err::kOk:
      return "ok";
    case Err::kUnauthorized:
      return "unauthorized";
    case Err::kForbidden:
      return "forbidden";
    case Err::kBadParam:
      return "bad param";
    case Err::kNotFound:
      return "not found";
    case Err::kInsufficient:
      return "insufficient balance";
    case Err::kActivityCannotClaim:
      return "activity cannot claim";
    case Err::kMaintain:
      return "maintain";
    case Err::kBanned:
      return "banned";
    default:
      return "internal error";
  }
}

// SPEC msg_id
namespace MsgId {
constexpr uint32_t kC2S_Auth = 1;
constexpr uint32_t kS2C_AuthResult = 2;
constexpr uint32_t kC2S_Heartbeat = 3;
constexpr uint32_t kS2C_HeartbeatAck = 4;
constexpr uint32_t kS2C_Kick = 5;
constexpr uint32_t kS2C_Maintain = 6;
constexpr uint32_t kS2C_Error = 7;

constexpr uint32_t kC2S_GetLobby = 1001;
constexpr uint32_t kS2C_LobbyInfo = 1002;
constexpr uint32_t kC2S_QuickMatch = 1010;
constexpr uint32_t kC2S_CancelMatch = 1011;
constexpr uint32_t kS2C_MatchStatus = 1012;
constexpr uint32_t kC2S_JoinRoom = 1020;
constexpr uint32_t kC2S_LeaveRoom = 1021;
constexpr uint32_t kC2S_Ready = 1022;
constexpr uint32_t kS2C_RoomState = 1023;
constexpr uint32_t kC2S_Chat = 1030;

constexpr uint32_t kS2C_DdzGameStart = 2001;
constexpr uint32_t kS2C_DdzTurn = 2002;
constexpr uint32_t kC2S_DdzBid = 2003;
constexpr uint32_t kS2C_DdzBidBroadcast = 2004;
constexpr uint32_t kC2S_DdzPlay = 2005;
constexpr uint32_t kS2C_DdzPlayBroadcast = 2006;
constexpr uint32_t kS2C_DdzSettle = 2007;
constexpr uint32_t kS2C_DdzReconnect = 2008;

constexpr uint32_t kS2C_ActivityUpdate = 3001;
}  // namespace MsgId

}  // namespace pandora

