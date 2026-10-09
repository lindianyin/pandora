#pragma once

#include <memory>
#include <mutex>
#include <unordered_map>

#include "game/game_ids.hpp"
#include "game/i_room_game.hpp"

namespace pandora {

class GameRegistry {
 public:
  static GameRegistry& Instance() {
    static GameRegistry g;
    return g;
  }

  void Register(GameMeta meta) {
    std::lock_guard<std::mutex> lk(mu_);
    by_id_[meta.game_id] = std::move(meta);
  }

  const GameMeta* Find(int32_t game_id) const {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = by_id_.find(game_id);
    if (it == by_id_.end()) return nullptr;
    return &it->second;
  }

  int DefaultSeats(int32_t game_id) const {
    if (const auto* m = Find(game_id)) return m->default_seats;
    return DefaultSeatsForGame(game_id);
  }

  bool OwnsMsg(int32_t game_id, uint32_t msg_id) const {
    return IsPlayMsg(msg_id) && GameIdOfMsg(msg_id) == game_id && Find(game_id) != nullptr;
  }

  bool IsRegisteredPlayMsg(uint32_t msg_id) const {
    if (!IsPlayMsg(msg_id)) return false;
    return Find(GameIdOfMsg(msg_id)) != nullptr;
  }

  const char* ShortName(int32_t game_id) const {
    if (const auto* m = Find(game_id)) return m->short_name;
    return "game";
  }

  bool IsKnownShortName(const std::string& name) const {
    std::lock_guard<std::mutex> lk(mu_);
    for (const auto& kv : by_id_) {
      if (name == kv.second.short_name) return true;
    }
    return false;
  }

  std::unique_ptr<IRoomGame> Create(int32_t game_id, RoomCtx& ctx) const {
    const GameMeta* m = Find(game_id);
    if (!m || !m->factory) return nullptr;
    return m->factory(ctx);
  }

 private:
  GameRegistry() = default;
  mutable std::mutex mu_;
  std::unordered_map<int32_t, GameMeta> by_id_;
};

void RegisterBuiltinGames();

}  // namespace pandora
