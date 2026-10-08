#pragma once

#include "game/phz/config.hpp"
#include "game/phz/meld.hpp"
#include "game/phz/tiles.hpp"

#include <vector>

namespace pandora {
namespace phz {

enum class HuFrom { kDraw, kDiscard, kReveal };

struct HuResult {
  bool ok{false};
  int xi{0};
  int menzi_count{0};
  int red_count{0};
  int small_count{0};
  bool is_draw_win{false};
  bool dui_dui{false};
  bool san_ti_wu_kan{false};
  int ming_tang_mask{0};
  int fan{1};
};

bool TrySplitSevenMenzi(HandCount hand, const std::vector<Meld>& table_melds, bool has_pao_or_ti,
                        int* out_xi, int* out_menzi, bool* out_dui_dui);

HuResult CheckHu(const HandCount& hand, const std::vector<Meld>& melds, TileId win_tile, HuFrom from,
                 const PhzConfig& cfg, bool has_pao_or_ti);

int ComputeFan(const HuResult& hu, const PhzConfig& cfg);
int CountRedInWin(const HandCount& hand, const std::vector<Meld>& melds, TileId win_tile);
int CountSmallInWin(const HandCount& hand, const std::vector<Meld>& melds, TileId win_tile);

}  // namespace phz
}  // namespace pandora
