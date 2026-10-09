#pragma once

#include <cstdint>
#include <vector>

#include "common/config.hpp"
#include "common/errors.hpp"
#include "common/log.hpp"
#include "common/proto_wire.hpp"
#include "common/send_error.hpp"
#include "game/game_registry.hpp"
#include "lobby/lobby_service.hpp"
#include "match/match_service.hpp"
#include "net/session_hub.hpp"
#include "room/room_manager.hpp"
#include "store/memory_store.hpp"
#include "wallet/wallet_service.hpp"

namespace pandora {

struct GameRuntime {
  SessionHub hub;
  LobbyService lobby;
  RoomManager rooms;
  MatchService match;

  GameRuntime(MemoryStore& store, WalletService& wallet, GameConfig cfg)
      : lobby(hub, store, cfg), rooms(hub, store, wallet, cfg), match(hub, store, lobby, rooms, cfg) {}

  void Tick() {
    match.Tick();
    rooms.Tick();
  }

  void OnDisconnect(int64_t uid) {
    match.OnDisconnect(uid);
    rooms.OnDisconnect(uid);
  }

  void OnReconnect(int64_t uid) { rooms.OnReconnect(uid); }

  void Dispatch(int64_t uid, uint32_t msg_id, const uint8_t* body, size_t len) {
    switch (msg_id) {
      case MsgId::kC2S_GetLobby:
        lobby.HandleGetLobby(uid);
        break;
      case MsgId::kC2S_QuickMatch: {
        int32_t tid = 1;
        proto_wire::DecodeC2S_QuickMatch(body, len, tid);
        match.QuickMatch(uid, tid);
        break;
      }
      case MsgId::kC2S_CancelMatch:
        match.CancelMatch(uid);
        break;
      case MsgId::kC2S_Ready: {
        bool ready = true;
        proto_wire::DecodeC2S_Ready(body, len, ready);
        rooms.SetReady(uid, ready);
        break;
      }
      case MsgId::kC2S_LeaveRoom:
        rooms.Leave(uid);
        break;
      case MsgId::kC2S_ClientTrace: {
        int64_t round_id = 0;
        int32_t seat = -1;
        std::string event;
        std::string detail;
        std::string game;
        if (!proto_wire::DecodeC2S_ClientTrace(body, len, round_id, seat, event, detail, game)) break;
        if (!game.empty() && !GameRegistry::Instance().IsKnownShortName(game)) game = "game";
        const auto room = rooms.RoomOf(uid);
        LogRound(round_id, "client", game.c_str(), room ? *room : 0, uid, seat, event, detail);
        break;
      }
      default:
        if (GameRegistry::Instance().IsRegisteredPlayMsg(msg_id)) {
          rooms.HandleGameMsg(uid, msg_id, body, len);
        } else {
          SendError(hub, uid, Err::kUnsupportedMsg, msg_id);
        }
        break;
    }
  }
};

}  // namespace pandora
