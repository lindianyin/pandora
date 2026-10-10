#include "lobby/lobby_service.hpp"

#include "common/errors.hpp"
#include "common/log.hpp"
#include "game/game_ids.hpp"
#include "game/game_registry.hpp"

namespace pandora {

namespace {

int32_t NormalizeTemplateGameId(int32_t game_id) {
  if (game_id == 1) return GameId::kDdz;
  if (game_id == 2) return GameId::kHzmj;
  if (game_id == 3) return GameId::kPhz;
  if (game_id == 4) return GameId::kFish;
  if (game_id == 5) return GameId::kBiji;
  if (game_id < GameId::kMin) return GameId::kDdz;
  return game_id;
}

int SeatsFor(int32_t game_id) { return GameRegistry::Instance().DefaultSeats(game_id); }

// Match queue size: fish formal allows solo (1); others use seat count.
int MatchPlayersFor(int32_t game_id) {
  if (game_id == GameId::kFish) return 1;
  return SeatsFor(game_id);
}

}  // namespace

LobbyService::LobbyService(SessionHub& hub, MemoryStore& store, GameConfig cfg)
    : hub_(hub), store_(store), cfg_(std::move(cfg)) {
  proto_wire::LobbyTemplate t;
  t.id = 1;
  t.name = "Novice";
  t.base_score = cfg_.base_score;
  t.min_gold = cfg_.min_gold;
  t.max_gold = cfg_.max_gold;
  t.enabled = true;
  t.game_id = GameId::kDdz;
  t.players = SeatsFor(GameId::kDdz);
  templates_.push_back(t);
  rake_bp_[1] = cfg_.rake_bp;

  proto_wire::LobbyTemplate hz;
  hz.id = 2;
  hz.name = "HangzhouMJ";
  hz.base_score = cfg_.base_score;
  hz.min_gold = cfg_.min_gold;
  hz.max_gold = cfg_.max_gold;
  hz.enabled = true;
  hz.game_id = GameId::kHzmj;
  hz.players = SeatsFor(GameId::kHzmj);
  templates_.push_back(hz);
  rake_bp_[2] = cfg_.rake_bp;
}

std::vector<proto_wire::LobbyTemplate> LobbyService::Templates() const {
  std::lock_guard<std::mutex> lk(mu_);
  return templates_;
}

std::optional<proto_wire::LobbyTemplate> LobbyService::FindTemplate(int32_t id) const {
  std::lock_guard<std::mutex> lk(mu_);
  for (const auto& t : templates_) {
    if (t.id == id) return t;
  }
  return std::nullopt;
}

int LobbyService::RakeBp(int32_t id) const {
  std::lock_guard<std::mutex> lk(mu_);
  auto it = rake_bp_.find(id);
  return it == rake_bp_.end() ? cfg_.rake_bp : it->second;
}

void LobbyService::ReloadFromDb(MysqlClient& mysql) {
  auto rows = mysql.Query(
      "SELECT id,game_id,name,base_score,rake_bp,min_gold,max_gold,enabled FROM room_template ORDER BY id ASC");
  if (!rows || rows->empty()) return;
  std::lock_guard<std::mutex> lk(mu_);
  templates_.clear();
  rake_bp_.clear();
  bool has_hzmj = false;
  bool has_phz = false;
  bool has_fish = false;
  bool has_biji = false;
  for (const auto& row : *rows) {
    proto_wire::LobbyTemplate t;
    t.id = row.Int("id");
    if (t.id <= 0) continue;
    const int32_t raw_gid = row.Int("game_id");
    t.game_id = NormalizeTemplateGameId(raw_gid);
    if (t.game_id != raw_gid) {
      mysql.ExecBind("UPDATE room_template SET game_id=? WHERE id=?", {I64(t.game_id), I64(t.id)});
    }
    t.players = MatchPlayersFor(t.game_id);
    t.name = row.Str("name");
    t.base_score = row.Int("base_score");
    rake_bp_[t.id] = row.Int("rake_bp");
    t.min_gold = row.I64("min_gold");
    t.max_gold = row.I64("max_gold");
    t.enabled = row.Bool("enabled");
    templates_.push_back(t);
    if (t.game_id == GameId::kHzmj) has_hzmj = true;
    if (t.game_id == GameId::kPhz) has_phz = true;
    if (t.game_id == GameId::kFish) has_fish = true;
    if (t.game_id == GameId::kBiji) has_biji = true;
  }
  if (!has_hzmj) {
    proto_wire::LobbyTemplate hz;
    hz.id = 2;
    hz.name = "HangzhouMJ";
    hz.base_score = cfg_.base_score;
    hz.min_gold = cfg_.min_gold;
    hz.max_gold = cfg_.max_gold;
    hz.enabled = true;
    hz.game_id = GameId::kHzmj;
    hz.players = SeatsFor(GameId::kHzmj);
    templates_.push_back(hz);
    rake_bp_[2] = cfg_.rake_bp;
    mysql.ExecBind(
        "INSERT INTO room_template(id,game_id,name,base_score,rake_bp,min_gold,max_gold,enabled) "
        "VALUES(?,?,?,?,?,?,?,1) ON DUPLICATE KEY UPDATE game_id=VALUES(game_id)",
        {I64(2), I64(GameId::kHzmj), Str(hz.name), I64(hz.base_score), I64(cfg_.rake_bp), I64(hz.min_gold),
         I64(hz.max_gold)});
  }
  if (!has_phz) {
    proto_wire::LobbyTemplate phz;
    phz.id = 3;
    phz.name = "Paohuzi";
    phz.base_score = cfg_.base_score;
    phz.min_gold = cfg_.min_gold;
    phz.max_gold = cfg_.max_gold;
    phz.enabled = true;
    phz.game_id = GameId::kPhz;
    phz.players = MatchPlayersFor(GameId::kPhz);
    templates_.push_back(phz);
    rake_bp_[3] = cfg_.rake_bp;
    mysql.ExecBind(
        "INSERT INTO room_template(id,game_id,name,base_score,rake_bp,min_gold,max_gold,enabled) "
        "VALUES(?,?,?,?,?,?,?,1) ON DUPLICATE KEY UPDATE game_id=VALUES(game_id)",
        {I64(3), I64(GameId::kPhz), Str(phz.name), I64(phz.base_score), I64(cfg_.rake_bp), I64(phz.min_gold),
         I64(phz.max_gold)});
  }
  if (!has_fish) {
    proto_wire::LobbyTemplate fish;
    fish.id = 4;
    fish.name = "FishNovice";
    fish.base_score = cfg_.base_score;
    fish.min_gold = cfg_.min_gold;
    fish.max_gold = cfg_.max_gold;
    fish.enabled = true;
    fish.game_id = GameId::kFish;
    fish.players = MatchPlayersFor(GameId::kFish);
    templates_.push_back(fish);
    rake_bp_[4] = cfg_.rake_bp;
    mysql.ExecBind(
        "INSERT INTO room_template(id,game_id,name,base_score,rake_bp,min_gold,max_gold,enabled) "
        "VALUES(?,?,?,?,?,?,?,1) ON DUPLICATE KEY UPDATE game_id=VALUES(game_id)",
        {I64(4), I64(GameId::kFish), Str(fish.name), I64(fish.base_score), I64(cfg_.rake_bp), I64(fish.min_gold),
         I64(fish.max_gold)});
  }
  if (!has_biji) {
    proto_wire::LobbyTemplate biji;
    biji.id = 5;
    biji.name = "BijiNovice";
    biji.base_score = cfg_.base_score;
    biji.min_gold = cfg_.min_gold;
    biji.max_gold = cfg_.max_gold;
    biji.enabled = true;
    biji.game_id = GameId::kBiji;
    biji.players = MatchPlayersFor(GameId::kBiji);
    templates_.push_back(biji);
    rake_bp_[5] = cfg_.rake_bp;
    mysql.ExecBind(
        "INSERT INTO room_template(id,game_id,name,base_score,rake_bp,min_gold,max_gold,enabled) "
        "VALUES(?,?,?,?,?,?,?,1) ON DUPLICATE KEY UPDATE game_id=VALUES(game_id)",
        {I64(5), I64(GameId::kBiji), Str(biji.name), I64(biji.base_score), I64(cfg_.rake_bp), I64(biji.min_gold),
         I64(biji.max_gold)});
  }
  PLOG_INFO("lobby templates reloaded count=" << templates_.size());
}

void LobbyService::UpsertTemplate(int id, const std::string& name, int base_score, int rake_bp, int64_t min_gold,
                                  int64_t max_gold, bool enabled) {
  std::lock_guard<std::mutex> lk(mu_);
  rake_bp_[id] = rake_bp;
  for (auto& t : templates_) {
    if (t.id == id) {
      t.name = name;
      t.base_score = base_score;
      t.min_gold = min_gold;
      t.max_gold = max_gold;
      t.enabled = enabled;
      return;
    }
  }
  proto_wire::LobbyTemplate t;
  t.id = id;
  t.name = name;
  t.base_score = base_score;
  t.min_gold = min_gold;
  t.max_gold = max_gold;
  t.enabled = enabled;
  t.game_id = (id == 2)   ? GameId::kHzmj
              : (id == 3) ? GameId::kPhz
              : (id == 4) ? GameId::kFish
              : (id == 5) ? GameId::kBiji
                          : GameId::kDdz;
  t.players = MatchPlayersFor(t.game_id);
  templates_.push_back(t);
}

void LobbyService::HandleGetLobby(int64_t uid) {
  auto p = store_.GetPlayer(uid);
  int64_t gold = p ? p->gold : 0;
  int64_t diamond = p ? p->diamond : 0;
  std::vector<proto_wire::LobbyTemplate> visible;
  {
    std::lock_guard<std::mutex> lk(mu_);
    for (const auto& t : templates_) {
      if (t.enabled) visible.push_back(t);
    }
  }
  hub_.Send(uid, MsgId::kS2C_LobbyInfo, proto_wire::EncodeS2C_LobbyInfo(visible, gold, diamond));
}

}  // namespace pandora
