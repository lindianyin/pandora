#pragma once

#include "game/hzmj/meld.hpp"
#include "game/hzmj/tiles.hpp"

#include <vector>

namespace pandora {
namespace hzmj {

enum class HuKind { kNone, kPing, kQiDui };

struct HuResult {
  bool ok{false};
  HuKind kind{HuKind::kNone};
  bool baotou{false};       // win uses caishen+real as pair
  bool baotou_ting{false};  // any draw would win via baotou pair
  int haohua{0};            // 0..3 for qi dui quads
  bool qing_qi_dui{false};  // qi dui without caishen in hand
};

// hand includes the winning tile already (14 tiles for closed, or 1+concealed after melds).
// melds size m => concealed tiles should be 14 - 3*m (or 14-3*m with win tile included).
HuResult CheckHu(const HandCount& hand, const std::vector<Meld>& melds, TileId win_tile, bool /*zimo*/);

// True if adding `tile` to hand (copy) would CheckHu.ok
bool WouldHu(HandCount hand, const std::vector<Meld>& melds, TileId tile, bool zimo);

// Waiting tiles (0..33) that would win; also sets baotou_ting if any-tile win via baotou.
struct TingInfo {
  std::vector<TileId> waits;
  bool baotou_ting{false};
};
TingInfo ComputeTing(const HandCount& hand, const std::vector<Meld>& melds);

}  // namespace hzmj
}  // namespace pandora
