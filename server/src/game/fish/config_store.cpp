#include "game/fish/config_store.hpp"

#include "common/log.hpp"

namespace pandora {
namespace fish {

FishConfigStore& FishConfigStore::Instance() {
  static FishConfigStore inst;
  return inst;
}

FishConfig FishConfigStore::Get() const {
  std::lock_guard<std::mutex> lk(mu_);
  return cfg_;
}

void FishConfigStore::SetMemory(FishConfig cfg) {
  std::lock_guard<std::mutex> lk(mu_);
  cfg_ = std::move(cfg);
}

bool FishConfigStore::Reload(MysqlClient& mysql) {
  auto type_rows = mysql.Query(
      "SELECT type_id,name,score,kind,hp,weight,radius,special,enabled FROM fish_type WHERE enabled=1");
  auto wave_rows =
      mysql.Query("SELECT id,name,duration_ms,spawn_interval_ms,max_alive,boss_type_id,weight,enabled FROM fish_wave "
                  "WHERE enabled=1");

  FishConfig cfg = DefaultFishConfig();
  if (type_rows && !type_rows->empty()) {
    cfg.types.clear();
    for (const auto& row : *type_rows) {
      FishTypeDef t;
      t.type_id = row.Int("type_id");
      t.name = row.Str("name");
      t.score = row.Int("score");
      const std::string kind = row.Str("kind");
      t.kind = (kind == "hp") ? FishKind::kHp : FishKind::kOdds;
      t.hp = row.Int("hp");
      t.weight = row.Int("weight");
      t.radius = static_cast<float>(row.Int("radius"));
      t.special = row.Str("special");
      t.enabled = row.Bool("enabled");
      cfg.types.push_back(t);
    }
  }
  if (wave_rows && !wave_rows->empty()) {
    cfg.waves.clear();
    for (const auto& row : *wave_rows) {
      FishWaveDef w;
      w.id = row.Int("id");
      w.name = row.Str("name");
      w.duration_ms = row.Int("duration_ms");
      w.spawn_interval_ms = row.Int("spawn_interval_ms");
      w.max_alive = row.Int("max_alive");
      w.boss_type_id = row.Int("boss_type_id");
      w.weight = row.Int("weight");
      w.enabled = row.Bool("enabled");
      cfg.waves.push_back(w);
    }
  }

  {
    std::lock_guard<std::mutex> lk(mu_);
    cfg_ = std::move(cfg);
  }
  const auto cur = Get();
  PLOG_INFO("fish config reloaded types=" << cur.types.size() << " waves=" << cur.waves.size());
  return true;
}

}  // namespace fish
}  // namespace pandora
