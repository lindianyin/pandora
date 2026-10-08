#include "game/phz/hu.hpp"

#include <algorithm>
#include <cmath>

namespace pandora {
namespace phz {

static bool RemoveTriple(HandCount& h, TileId t) { return RemoveTile(h, t, 3); }

static bool RemoveSeq(HandCount& h, TileId a, TileId b, TileId c) {
  HandCount tmp = h;
  if (!RemoveTile(tmp, a, 1) || !RemoveTile(tmp, b, 1) || !RemoveTile(tmp, c, 1)) return false;
  h = tmp;
  return true;
}

static int XiChi(TileId a, TileId b, TileId c) {
  std::vector<TileId> s{a, b, c};
  std::sort(s.begin(), s.end());
  if (s[0] == 0 && s[1] == 1 && s[2] == 2) return 3;
  if (s[0] == 10 && s[1] == 11 && s[2] == 12) return 6;
  if (s[0] == 1 && s[1] == 6 && s[2] == 9) return 3;
  if (s[0] == 11 && s[1] == 16 && s[2] == 19) return 6;
  return 0;
}

static bool Dfs(HandCount h, bool need_jiang, bool has_jiang, int menzi, int xi, bool all_kezi, int* best_xi,
                int* best_menzi, bool* best_dui) {
  if (HandSize(h) == 0) {
    if (!need_jiang || has_jiang) {
      if (menzi == 7) {
        if (xi > *best_xi) {
          *best_xi = xi;
          *best_menzi = menzi;
          *best_dui = all_kezi && has_jiang;
        }
        return true;
      }
    }
    return false;
  }

  // find first tile present
  TileId t0 = kTileInvalid;
  for (TileId t = 0; t < kTileKinds; ++t) {
    if (h[static_cast<size_t>(t)] > 0) {
      t0 = t;
      break;
    }
  }
  if (t0 == kTileInvalid) return false;

  bool ok = false;

  // kan / kezi
  if (h[static_cast<size_t>(t0)] >= 3) {
    HandCount n = h;
    RemoveTriple(n, t0);
    ok |= Dfs(n, need_jiang, has_jiang, menzi + 1, xi + HuXiOfKan(t0), all_kezi, best_xi, best_menzi, best_dui);
  }

  // pair as jiang
  if (need_jiang && !has_jiang && h[static_cast<size_t>(t0)] >= 2) {
    HandCount n = h;
    RemoveTile(n, t0, 2);
    ok |= Dfs(n, need_jiang, true, menzi + 1, xi, all_kezi, best_xi, best_menzi, best_dui);
  }

  // consecutive chi same size
  if (IsSmall(t0) || IsBig(t0)) {
    const int r = Rank(t0);
    const int base = IsSmall(t0) ? 0 : 10;
    if (r <= 7) {
      TileId a = static_cast<TileId>(base + r);
      TileId b = static_cast<TileId>(base + r + 1);
      TileId c = static_cast<TileId>(base + r + 2);
      HandCount n = h;
      if (RemoveSeq(n, a, b, c)) {
        ok |= Dfs(n, need_jiang, has_jiang, menzi + 1, xi + XiChi(a, b, c), false, best_xi, best_menzi, best_dui);
      }
    }
  }

  // special 二七十 / 贰柒拾
  if (IsSmall(t0)) {
    HandCount n = h;
    if (RemoveSeq(n, 1, 6, 9)) {
      ok |= Dfs(n, need_jiang, has_jiang, menzi + 1, xi + 3, false, best_xi, best_menzi, best_dui);
    }
  } else if (IsBig(t0)) {
    HandCount n = h;
    if (RemoveSeq(n, 11, 16, 19)) {
      ok |= Dfs(n, need_jiang, has_jiang, menzi + 1, xi + 6, false, best_xi, best_menzi, best_dui);
    }
  }

  return ok;
}

bool TrySplitSevenMenzi(HandCount hand, const std::vector<Meld>& table_melds, bool has_pao_or_ti, int* out_xi,
                        int* out_menzi, bool* out_dui_dui) {
  int table_menzi = static_cast<int>(table_melds.size());
  int table_xi = 0;
  bool table_all_kezi = true;
  for (const auto& m : table_melds) {
    table_xi += HuXiOfMeld(m);
    if (m.kind == MeldKind::kChi || m.kind == MeldKind::kJiao) table_all_kezi = false;
  }

  // Extract in-hand kans as fixed melds first (optional path: leave in hand for DFS)
  int best_xi = -1;
  int best_menzi = 0;
  bool best_dui = false;

  const bool need_jiang = has_pao_or_ti;
  // Without pao/ti, classic needs 7 melds with no free pair — use need_jiang=false and exact 7.
  // With pao/ti, one pair counts as menzi (jiang).

  // Also try peeling explicit kans from hand into table count
  HandCount work = hand;
  int peeled_menzi = 0;
  int peeled_xi = 0;
  for (TileId t = 0; t < kTileKinds; ++t) {
    while (work[static_cast<size_t>(t)] >= 3) {
      // try both keep and peel — peel first for xi
      break;
    }
  }

  auto run = [&](HandCount h, int base_m, int base_x, bool all_k) {
    int bx = -1, bm = 0;
    bool bd = false;
    Dfs(h, need_jiang, false, base_m, base_x, all_k, &bx, &bm, &bd);
    if (bx > best_xi) {
      best_xi = bx;
      best_menzi = bm;
      best_dui = bd;
    }
  };

  run(hand, table_menzi, table_xi, table_all_kezi);

  // Peel all kans
  HandCount peeled = hand;
  int pm = table_menzi;
  int px = table_xi;
  for (TileId t = 0; t < kTileKinds; ++t) {
    while (peeled[static_cast<size_t>(t)] >= 3) {
      RemoveTile(peeled, t, 3);
      ++pm;
      px += HuXiOfKan(t);
    }
  }
  run(peeled, pm, px, table_all_kezi);

  (void)peeled_menzi;
  (void)peeled_xi;
  (void)work;

  if (best_menzi == 7 && best_xi >= 0) {
    if (out_xi) *out_xi = best_xi;
    if (out_menzi) *out_menzi = best_menzi;
    if (out_dui_dui) *out_dui_dui = best_dui;
    return true;
  }
  return false;
}

static void CountTilesWin(const HandCount& hand, const std::vector<Meld>& melds, TileId win_tile, int& red,
                          int& small) {
  HandCount h = hand;
  if (IsValidTile(win_tile)) AddTile(h, win_tile, 1);
  red = 0;
  small = 0;
  for (TileId t = 0; t < kTileKinds; ++t) {
    const int c = h[static_cast<size_t>(t)];
    if (c <= 0) continue;
    if (IsRed(t)) red += c;
    if (IsSmall(t)) small += c;
  }
  for (const auto& m : melds) {
    for (TileId t : m.tiles) {
      if (IsRed(t)) ++red;
      if (IsSmall(t)) ++small;
    }
  }
}

int CountRedInWin(const HandCount& hand, const std::vector<Meld>& melds, TileId win_tile) {
  int r = 0, s = 0;
  CountTilesWin(hand, melds, win_tile, r, s);
  return r;
}

int CountSmallInWin(const HandCount& hand, const std::vector<Meld>& melds, TileId win_tile) {
  int r = 0, s = 0;
  CountTilesWin(hand, melds, win_tile, r, s);
  return s;
}

int ComputeFan(const HuResult& hu, const PhzConfig& cfg) {
  std::vector<int> candidates;
  if (hu.red_count == 1) candidates.push_back(cfg.fan_dian_hu);
  if (hu.red_count == 0) candidates.push_back(cfg.fan_wu_hu);
  if (hu.red_count >= 10 && hu.red_count <= 12) candidates.push_back(cfg.fan_xiao_hong);
  if (hu.red_count >= 13) {
    int fan = cfg.fan_da_hong;
    if (cfg.hong_hu_progressive) fan += (hu.red_count - 13);
    candidates.push_back(fan);
  }
  if (hu.small_count >= 18) candidates.push_back(cfg.fan_shi_ba_xiao);
  if (hu.dui_dui) candidates.push_back(cfg.fan_dui_dui);

  int f = 1;
  if (cfg.ming_tang_combine == "max_plus_zimo" || cfg.ming_tang_combine.empty()) {
    for (int c : candidates) f = std::max(f, c);
  } else if (cfg.ming_tang_combine == "multiply") {
    f = 1;
    for (int c : candidates) f *= c;
  }
  return std::max(f, 1);
}

HuResult CheckHu(const HandCount& hand, const std::vector<Meld>& melds, TileId win_tile, HuFrom from,
                 const PhzConfig& cfg, bool has_pao_or_ti) {
  HuResult r;
  if (from == HuFrom::kDiscard && !cfg.dian_pao) return r;

  HandCount h = hand;
  if (IsValidTile(win_tile)) AddTile(h, win_tile, 1);

  // san ti wu kan: count ti/kan
  int ti_kan = 0;
  for (const auto& m : melds) {
    if (m.kind == MeldKind::kTi || m.kind == MeldKind::kKan) ++ti_kan;
  }
  for (TileId t = 0; t < kTileKinds; ++t) {
    if (h[static_cast<size_t>(t)] >= 3) ++ti_kan;  // rough for exception
  }
  // Better: count exact kans in hand + ti melds
  int kan_n = 0, ti_n = 0;
  for (const auto& m : melds) {
    if (m.kind == MeldKind::kTi) ++ti_n;
    if (m.kind == MeldKind::kKan || m.kind == MeldKind::kWei) ++kan_n;
  }
  HandCount hk = hand;
  if (IsValidTile(win_tile)) AddTile(hk, win_tile, 1);
  for (TileId t = 0; t < kTileKinds; ++t) {
    if (hk[static_cast<size_t>(t)] >= 3) ++kan_n;
  }
  const bool stwk = cfg.san_ti_wu_kan && (ti_n >= 3 || kan_n >= 5);

  int xi = 0, menzi = 0;
  bool dui = false;
  const bool pao_ti = has_pao_or_ti;
  if (!TrySplitSevenMenzi(h, melds, pao_ti, &xi, &menzi, &dui) && !stwk) return r;

  if (stwk && menzi != 7) {
    // allow exception with computed xi from melds+kans
    xi = 0;
    for (const auto& m : melds) xi += HuXiOfMeld(m);
    for (TileId t = 0; t < kTileKinds; ++t) {
      if (hk[static_cast<size_t>(t)] >= 3) xi += HuXiOfKan(t);
    }
    menzi = 7;
  }

  if (xi < cfg.min_hu_xi && !stwk) return r;

  r.ok = true;
  r.xi = xi;
  r.menzi_count = menzi;
  r.is_draw_win = (from == HuFrom::kDraw);
  r.dui_dui = dui;
  r.san_ti_wu_kan = stwk;
  CountTilesWin(hand, melds, win_tile, r.red_count, r.small_count);

  if (r.red_count == 1) r.ming_tang_mask |= kMtDianHu;
  if (r.red_count == 0) r.ming_tang_mask |= kMtWuHu;
  if (r.red_count >= 10 && r.red_count <= 12) r.ming_tang_mask |= kMtXiaoHong;
  if (r.red_count >= 13) r.ming_tang_mask |= kMtDaHong;
  if (r.small_count >= 18) r.ming_tang_mask |= kMtShiBaXiao;
  if (r.dui_dui) r.ming_tang_mask |= kMtDuiDui;
  if (stwk) r.ming_tang_mask |= kMtSanTiWuKan;

  r.fan = ComputeFan(r, cfg);
  (void)ti_kan;
  return r;
}

}  // namespace phz
}  // namespace pandora
