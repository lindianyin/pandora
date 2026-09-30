#pragma once

#include <cstdint>
#include <vector>

#include "common/config.hpp"
#include "common/errors.hpp"
#include "common/log.hpp"
#include "common/proto_wire.hpp"
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
      case MsgId::kC2S_DdzBid: {
        int32_t score = 0;
        proto_wire::DecodeC2S_DdzBid(body, len, score);
        rooms.OnBid(uid, score);
        break;
      }
      case MsgId::kC2S_DdzPlay: {
        bool pass = false;
        std::vector<int32_t> cards32;
        proto_wire::DecodeC2S_DdzPlay(body, len, pass, cards32);
        std::vector<int> cards(cards32.begin(), cards32.end());
        rooms.OnPlay(uid, pass, cards);
        break;
      }
      case MsgId::kC2S_HzmjDiscard: {
        int32_t tile = 0;
        proto_wire::DecodeC2S_HzmjDiscard(body, len, tile);
        rooms.OnHzmjDiscard(uid, tile);
        break;
      }
      case MsgId::kC2S_HzmjAction: {
        int32_t action = 0;
        std::vector<int32_t> chi;
        proto_wire::DecodeC2S_HzmjAction(body, len, action, chi);
        rooms.OnHzmjAction(uid, action, chi);
        break;
      }
      case MsgId::kC2S_HzmjGang: {
        int32_t kind = 0;
        int32_t tile = 0;
        proto_wire::DecodeC2S_HzmjGang(body, len, kind, tile);
        rooms.OnHzmjGang(uid, kind, tile);
        break;
      }
      case MsgId::kC2S_ClientTrace: {
        int64_t round_id = 0;
        int32_t seat = -1;
        std::string event;
        std::string detail;
        std::string game;
        if (!proto_wire::DecodeC2S_ClientTrace(body, len, round_id, seat, event, detail, game)) break;
        if (game != "hzmj" && game != "ddz") game = "game";
        const auto room = rooms.RoomOf(uid);
        LogRound(round_id, "client", game.c_str(), room ? *room : 0, uid, seat, event, detail);
        break;
      }
      default:
        hub.Send(uid, MsgId::kS2C_Error,
                 proto_wire::EncodeS2C_Error(static_cast<int>(Err::kBadParam), "unsupported msg", msg_id));
        break;
    }
  }
};

}  // namespace pandora

