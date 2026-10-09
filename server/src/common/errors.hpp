#pragma once

#include <cstdint>
#include <string>

namespace pandora {

// Platform err bands: 10xxx session, 11xxx lobby/match/room, 12xxx wallet, ...
// Play err: game_id * 1000 + slot (e.g. 2000001).
enum class Err : int32_t {
  kOk = 0,

  kUnauthorized = 10001,
  kForbidden = 10002,
  kBadParam = 10003,
  kNotFound = 10004,
  kMaintain = 10010,
  kBanned = 10011,

  kAlreadyInRoom = 11001,
  kTemplateNotFound = 11002,
  kPlayerNotFound = 11003,
  kGoldOutOfRange = 11004,
  kUnsupportedMsg = 11005,
  kNotInRoom = 11006,
  kGameNotRunning = 11007,

  kInsufficient = 12001,
  kIdempotentHit = 12002,

  kActivityCannotClaim = 13001,

  kFriendIllegal = 14001,
  kMailIllegal = 14002,
  kRankIllegal = 14003,

  kItemUnavailable = 15001,
  kItemInsufficient = 15002,
  kItemExpired = 15003,
  kItemBadKind = 15004,

  kPayOrderFailed = 16001,
  kPayOrderIllegal = 16002,

  kDdzIllegalPlay = 2000001,
  kDdzIllegalBid = 2000002,

  kHzmjIllegalDiscard = 3000001,
  kHzmjIllegalAction = 3000002,

  kPhzIllegalDiscard = 4000001,
  kPhzIllegalAction = 4000002,

  kFishIllegalMult = 5000001,
  kFishFireTooFast = 5000002,
  kFishInsufficient = 5000003,
  kFishBadSeat = 5000004,
  kFishBadLock = 5000005,
  kFishDupSeq = 5000006,

  kInternal = 90001,
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
    case Err::kMaintain:
      return "maintain";
    case Err::kBanned:
      return "banned";
    case Err::kAlreadyInRoom:
      return "already in room";
    case Err::kTemplateNotFound:
      return "template not found";
    case Err::kPlayerNotFound:
      return "player not found";
    case Err::kGoldOutOfRange:
      return "gold out of range";
    case Err::kUnsupportedMsg:
      return "unsupported msg";
    case Err::kNotInRoom:
      return "not in room";
    case Err::kGameNotRunning:
      return "game not running";
    case Err::kInsufficient:
      return "insufficient balance";
    case Err::kIdempotentHit:
      return "duplicate operation";
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
    case Err::kPayOrderFailed:
      return "pay order failed";
    case Err::kPayOrderIllegal:
      return "pay order illegal";
    case Err::kDdzIllegalPlay:
      return "illegal play";
    case Err::kDdzIllegalBid:
      return "illegal bid";
    case Err::kHzmjIllegalDiscard:
      return "illegal discard";
    case Err::kHzmjIllegalAction:
      return "illegal action";
    case Err::kPhzIllegalDiscard:
      return "illegal discard";
    case Err::kPhzIllegalAction:
      return "illegal action";
    case Err::kFishIllegalMult:
      return "illegal cannon mult";
    case Err::kFishFireTooFast:
      return "fire too fast";
    case Err::kFishInsufficient:
      return "insufficient balance";
    case Err::kFishBadSeat:
      return "bad seat";
    case Err::kFishBadLock:
      return "illegal lock target";
    case Err::kFishDupSeq:
      return "duplicate fire seq";
    case Err::kInternal:
    default:
      return "internal error";
  }
}

// msg_id = game_id * 100 + slot for play; platform stays < 100000.
namespace MsgId {
constexpr uint32_t kC2S_Auth = 1;
constexpr uint32_t kS2C_AuthResult = 2;
constexpr uint32_t kC2S_Heartbeat = 3;
constexpr uint32_t kS2C_HeartbeatAck = 4;
constexpr uint32_t kS2C_Kick = 5;

// S2C_Kick.reason
constexpr int32_t kKickHeartbeatTimeout = 1;
constexpr int32_t kKickLoggedInElsewhere = 2;
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

constexpr uint32_t kS2C_DdzGameStart = 200001;
constexpr uint32_t kS2C_DdzTurn = 200002;
constexpr uint32_t kC2S_DdzBid = 200003;
constexpr uint32_t kS2C_DdzBidBroadcast = 200004;
constexpr uint32_t kC2S_DdzPlay = 200005;
constexpr uint32_t kS2C_DdzPlayBroadcast = 200006;
constexpr uint32_t kS2C_DdzSettle = 200007;
constexpr uint32_t kS2C_DdzReconnect = 200008;

constexpr uint32_t kS2C_ActivityUpdate = 3001;

constexpr uint32_t kS2C_MailNotify = 4001;
constexpr uint32_t kS2C_FriendNotify = 4002;

constexpr uint32_t kS2C_BagUpdate = 5001;

constexpr uint32_t kS2C_HzmjGameStart = 300001;
constexpr uint32_t kS2C_HzmjTurn = 300002;
constexpr uint32_t kS2C_HzmjDraw = 300003;
constexpr uint32_t kS2C_HzmjReveal = 300004;  // reserved
constexpr uint32_t kC2S_HzmjDiscard = 300005;
constexpr uint32_t kS2C_HzmjDiscardBroadcast = 300006;
constexpr uint32_t kC2S_HzmjAction = 300007;
constexpr uint32_t kS2C_HzmjActionBroadcast = 300008;
constexpr uint32_t kS2C_HzmjSettle = 300009;
constexpr uint32_t kS2C_HzmjLiuJu = 300010;
constexpr uint32_t kC2S_HzmjGang = 300011;

constexpr uint32_t kS2C_PhzGameStart = 400001;
constexpr uint32_t kS2C_PhzTurn = 400002;
constexpr uint32_t kS2C_PhzDraw = 400003;
constexpr uint32_t kS2C_PhzReveal = 400004;
constexpr uint32_t kC2S_PhzDiscard = 400005;
constexpr uint32_t kS2C_PhzDiscardBroadcast = 400006;
constexpr uint32_t kC2S_PhzAction = 400007;
constexpr uint32_t kS2C_PhzActionBroadcast = 400008;
constexpr uint32_t kS2C_PhzSettle = 400009;
constexpr uint32_t kS2C_PhzLiuJu = 400010;

constexpr uint32_t kS2C_FishGameStart = 500001;
constexpr uint32_t kS2C_FishSeatUpdate = 500002;
constexpr uint32_t kS2C_FishSpawn = 500003;
constexpr uint32_t kS2C_FishDespawn = 500004;
constexpr uint32_t kS2C_FishSync = 500005;
constexpr uint32_t kC2S_FishFire = 500006;
constexpr uint32_t kS2C_FishFireBroadcast = 500007;
constexpr uint32_t kS2C_FishHit = 500008;
constexpr uint32_t kS2C_FishCatch = 500009;
constexpr uint32_t kC2S_FishSetMult = 500010;
constexpr uint32_t kC2S_FishLeave = 500011;
constexpr uint32_t kS2C_FishKickSeat = 500012;
constexpr uint32_t kS2C_FishWave = 500013;
constexpr uint32_t kC2S_FishLock = 500014;

constexpr uint32_t kC2S_ClientTrace = 9001;
}  // namespace MsgId

}  // namespace pandora
