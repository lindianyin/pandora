#include "lobby/lobby_service.hpp"

#include "common/errors.hpp"
#include "common/log.hpp"

namespace pandora {

LobbyService::LobbyService(SessionHub& hub, MemoryStore& store, GameConfig cfg)
    : hub_(hub), store_(store), cfg_(std::move(cfg)) {
  proto_wire::LobbyTemplate t;
  t.id = 1;
  t.name = "Novice";
  t.base_score = cfg_.base_score;
  t.min_gold = cfg_.min_gold;
  t.max_gold = cfg_.max_gold;
  t.enabled = true;
  t.game_id = 1;
  t.players = 3;
  templates_.push_back(t);
  rake_bp_[1] = cfg_.rake_bp;

  proto_wire::LobbyTemplate hz;
  hz.id = 2;
  hz.name = "HangzhouMJ";
  hz.base_score = cfg_.base_score;
  hz.min_gold = cfg_.min_gold;
  hz.max_gold = cfg_.max_gold;
  hz.enabled = true;
  hz.game_id = 2;
  hz.players = 4;
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
  for (const auto& row : *rows) {
    proto_wire::LobbyTemplate t;
    t.id = row.Int("id");
    if (t.id <= 0) continue;
    t.game_id = row.Int("game_id");
    if (t.game_id <= 0) t.game_id = 1;
    t.players = (t.game_id == 2) ? 4 : 3;
    t.name = row.Str("name");
    t.base_score = row.Int("base_score");
    rake_bp_[t.id] = row.Int("rake_bp");
    t.min_gold = row.I64("min_gold");
    t.max_gold = row.I64("max_gold");
    t.enabled = row.Bool("enabled");
    templates_.push_back(t);
    if (t.game_id == 2) has_hzmj = true;
  }
  // Ensure game_id=2 template exists even if DB seed missing (dev/smoke).
  if (!has_hzmj) {
    proto_wire::LobbyTemplate hz;
    hz.id = 2;
    hz.name = "HangzhouMJ";
    hz.base_score = cfg_.base_score;
    hz.min_gold = cfg_.min_gold;
    hz.max_gold = cfg_.max_gold;
    hz.enabled = true;
    hz.game_id = 2;
    hz.players = 4;
    templates_.push_back(hz);
    rake_bp_[2] = cfg_.rake_bp;
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
  t.game_id = (id == 2) ? 2 : 1;
  t.players = (t.game_id == 2) ? 4 : 3;
  templates_.push_back(t);
}

void LobbyService::HandleGetLobby(int64_t uid) {
  auto p = store_.GetPlayer(uid);
  int64_t gold = p ? p->gold : 0;
  int64_t diamond = p ? p->diamond : 0;
  std::vector<proto_wire::LobbyTemplate> visible;
  {
    std::lock_guard<std::mutex> lk(mu_);
    for (const auto& t : templates_)
      if (t.enabled) visible.push_back(t);
  }
  hub_.Send(uid, MsgId::kS2C_LobbyInfo, proto_wire::EncodeS2C_LobbyInfo(visible, gold, diamond));
  PLOG_INFO("lobby sent uid=" << uid);
}

}  // namespace pandora

