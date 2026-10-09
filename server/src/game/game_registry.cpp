#include "game/game_registry.hpp"

#include "game/ddz/ddz_room_game.hpp"
#include "game/fish/fish_room_game.hpp"
#include "game/hzmj/hzmj_room_game.hpp"
#include "game/phz/phz_room_game.hpp"

namespace pandora {

void RegisterBuiltinGames() {
  auto& reg = GameRegistry::Instance();
  reg.Register({GameId::kDdz, "ddz", 3,
                [](RoomCtx& ctx) { return std::make_unique<DdzRoomGame>(ctx); }});
  reg.Register({GameId::kHzmj, "hzmj", 4,
                [](RoomCtx& ctx) { return std::make_unique<HzmjRoomGame>(ctx); }});
  reg.Register({GameId::kPhz, "phz", 3,
                [](RoomCtx& ctx) { return std::make_unique<PhzRoomGame>(ctx); }});
  reg.Register({GameId::kFish, "fish", 4,
                [](RoomCtx& ctx) { return std::make_unique<FishRoomGame>(ctx); }});
}

}  // namespace pandora
