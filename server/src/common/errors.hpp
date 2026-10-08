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
  kFriendIllegal = 2101,
  kMailIllegal = 2102,
  kRankIllegal = 2103,
  kItemUnavailable = 2201,
  kItemInsufficient = 2202,
  kItemExpired = 2203,
  kItemBadKind = 2204,
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
    case Err::kFriendIllegal:
      return "friend request illegal";
    case Err::kMailIllegal:
      return "mail missing or claimed";
    case Err::kRankIllegal:
      return "rank period illegal";
    case Err::kItemUnavailable:
      return "item unavailable";
    case Err::kItemInsufficient:
      return "insufficient item";
    case Err::kItemExpired:
      return "item expired";
    case Err::kItemBadKind:
      return "item kind mismatch";
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

constexpr uint32_t kS2C_MailNotify = 4001;
constexpr uint32_t kS2C_FriendNotify = 4002;

constexpr uint32_t kS2C_BagUpdate = 5001;

constexpr uint32_t kS2C_HzmjGameStart = 6001;
constexpr uint32_t kS2C_HzmjTurn = 6002;
constexpr uint32_t kS2C_HzmjDraw = 6003;
constexpr uint32_t kC2S_HzmjDiscard = 6004;
constexpr uint32_t kS2C_HzmjDiscardBroadcast = 6005;
constexpr uint32_t kC2S_HzmjAction = 6006;
constexpr uint32_t kS2C_HzmjActionBroadcast = 6007;
constexpr uint32_t kS2C_HzmjSettle = 6009;
constexpr uint32_t kS2C_HzmjLiuJu = 6010;
constexpr uint32_t kC2S_HzmjGang = 6012;

constexpr uint32_t kS2C_PhzGameStart = 7001;
constexpr uint32_t kS2C_PhzTurn = 7002;
constexpr uint32_t kS2C_PhzDraw = 7003;
constexpr uint32_t kS2C_PhzReveal = 7004;
constexpr uint32_t kC2S_PhzDiscard = 7005;
constexpr uint32_t kS2C_PhzDiscardBroadcast = 7006;
constexpr uint32_t kC2S_PhzAction = 7007;
constexpr uint32_t kS2C_PhzActionBroadcast = 7008;
constexpr uint32_t kS2C_PhzSettle = 7009;
constexpr uint32_t kS2C_PhzLiuJu = 7010;

constexpr uint32_t kC2S_ClientTrace = 9001;
}  // namespace MsgId

}  // namespace pandora

