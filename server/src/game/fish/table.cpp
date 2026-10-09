#include "game/fish/table.hpp"

#include <algorithm>
#include <cmath>

#include "game/fish/math.hpp"

namespace pandora {
namespace fish {

FishTable::FishTable(FishConfig cfg, std::function<uint32_t()> rng)
    : cfg_(std::move(cfg)), rng_(std::move(rng)) {
  if (cfg_.cannon_mults.empty()) cfg_.cannon_mults = {1, 2, 5, 10};
  if (!cfg_.waves.empty() && cfg_.waves[0].enabled) {
    spawn_interval_ms_ = cfg_.waves[0].spawn_interval_ms;
    max_alive_ = cfg_.waves[0].max_alive;
  }
  if (spawn_interval_ms_ <= 0) spawn_interval_ms_ = 500;
  if (max_alive_ <= 0) max_alive_ = 20;
  if (max_alive_ > cfg_.hard_max_alive) max_alive_ = cfg_.hard_max_alive;
}

void FishTable::SetSink(std::function<void(const OutEvent&)> sink) { sink_ = std::move(sink); }

void FishTable::Emit(const OutEvent& ev) {
  if (sink_) sink_(ev);
}

void FishTable::Start(const std::vector<int64_t>& seat_uids) {
  for (int i = 0; i < kMaxSeats; ++i) {
    seats_[static_cast<size_t>(i)] = SeatState{};
    if (i < static_cast<int>(seat_uids.size()) && seat_uids[static_cast<size_t>(i)] != 0) {
      seats_[static_cast<size_t>(i)].uid = seat_uids[static_cast<size_t>(i)];
      seats_[static_cast<size_t>(i)].mult = cfg_.cannon_mults.front();
      seats_[static_cast<size_t>(i)].online = true;
      seats_[static_cast<size_t>(i)].left = false;
    } else {
      seats_[static_cast<size_t>(i)].left = true;
      seats_[static_cast<size_t>(i)].online = false;
    }
  }
  fish_.clear();
  bullets_.clear();
  pending_rewards_.clear();
  caught_fish_ids_.clear();
  next_fish_id_ = 1;
  next_bullet_id_ = 1;
  now_ms_ = 0;
  spawn_accum_ms_ = 0;
  started_ = true;
  OutEvent ev;
  ev.type = "start";
  Emit(ev);
}

bool FishTable::ValidMult(int mult) const {
  for (int m : cfg_.cannon_mults)
    if (m == mult) return true;
  return false;
}

int FishTable::MinFireIntervalMs() const {
  const int hz = cfg_.fire_rate_hz > 0 ? cfg_.fire_rate_hz : 8;
  return std::max(1, 1000 / hz);
}

bool FishTable::SetCannonMult(int seat, int mult) {
  if (seat < 0 || seat >= kMaxSeats) return false;
  auto& s = seats_[static_cast<size_t>(seat)];
  if (s.left || !s.uid) return false;
  if (!ValidMult(mult)) return false;
  s.mult = mult;
  OutEvent ev;
  ev.type = "seat_update";
  ev.seat = seat;
  ev.uid = s.uid;
  ev.mult = mult;
  Emit(ev);
  return true;
}

void FishTable::OnLeave(int seat) {
  if (seat < 0 || seat >= kMaxSeats) return;
  auto& s = seats_[static_cast<size_t>(seat)];
  s.left = true;
  s.online = false;
  OutEvent ev;
  ev.type = "seat_update";
  ev.seat = seat;
  ev.uid = s.uid;
  ev.detail = "leave";
  Emit(ev);
}

void FishTable::SetOnline(int seat, bool online) {
  if (seat < 0 || seat >= kMaxSeats) return;
  auto& s = seats_[static_cast<size_t>(seat)];
  if (s.left) return;
  s.online = online;
  OutEvent ev;
  ev.type = "seat_update";
  ev.seat = seat;
  ev.uid = s.uid;
  ev.detail = online ? "online" : "offline";
  Emit(ev);
}

bool FishTable::TryFire(const FireRequest& req, FireCostPlan* cost) {
  if (!started_ || !cost) return false;
  if (req.seat < 0 || req.seat >= kMaxSeats) return false;
  auto& s = seats_[static_cast<size_t>(req.seat)];
  if (!s.uid || s.left || !s.online) return false;
  if (req.uid != 0 && req.uid != s.uid) return false;
  if (!ValidMult(req.mult)) return false;
  if (req.client_seq > 0) {
    if (s.seen_seq.count(req.client_seq)) return false;
  }
  if (now_ms_ - s.last_fire_ms < MinFireIntervalMs()) return false;

  const int64_t fire_cost = FireCost(req.mult, cfg_.base_score);
  const int64_t rake = FireRake(fire_cost, cfg_.rake_bp);

  const float cx = kSeatCannonPos[req.seat].x;
  const float cy = kSeatCannonPos[req.seat].y;
  float dx = req.aim_x - cx;
  float dy = req.aim_y - cy;
  const float len = std::sqrt(dx * dx + dy * dy);
  if (len < 1e-3f) {
    dx = 0.f;
    dy = 1.f;
  } else {
    dx /= len;
    dy /= len;
  }
  const float spd = cfg_.bullet_speed > 0 ? cfg_.bullet_speed : 1200.f;

  Bullet b;
  b.bullet_id = next_bullet_id_++;
  b.seat = req.seat;
  b.uid = s.uid;
  b.mult = req.mult;
  b.x = cx;
  b.y = cy;
  b.vx = dx * spd;
  b.vy = dy * spd;
  b.radius = cfg_.bullet_radius;
  b.lock_fish_id = req.lock_fish_id;
  b.alive = true;
  bullets_.push_back(b);

  s.last_fire_ms = now_ms_;
  if (req.client_seq > 0) s.seen_seq.insert(req.client_seq);
  s.mult = req.mult;

  cost->uid = s.uid;
  cost->seat = req.seat;
  cost->cost = fire_cost;
  cost->rake = rake;
  cost->client_seq = req.client_seq;
  cost->bullet_id = b.bullet_id;
  cost->mult = req.mult;

  OutEvent ev;
  ev.type = "fire";
  ev.seat = req.seat;
  ev.uid = s.uid;
  ev.bullet_id = b.bullet_id;
  ev.x = b.x;
  ev.y = b.y;
  ev.vx = b.vx;
  ev.vy = b.vy;
  ev.mult = req.mult;
  ev.client_seq = req.client_seq;
  Emit(ev);
  return true;
}

const FishTypeDef* FishTable::PickType() {
  int total = 0;
  for (const auto& t : cfg_.types)
    if (t.enabled) total += std::max(1, t.weight);
  if (total <= 0 || !rng_) return nullptr;
  int r = static_cast<int>(rng_() % static_cast<uint32_t>(total));
  for (const auto& t : cfg_.types) {
    if (!t.enabled) continue;
    r -= std::max(1, t.weight);
    if (r < 0) return &t;
  }
  return &cfg_.types.front();
}

void FishTable::SpawnOne() {
  if (AliveFishCount() >= max_alive_) return;
  const FishTypeDef* td = PickType();
  if (!td) return;

  FishInst f;
  f.fish_id = next_fish_id_++;
  f.type_id = td->type_id;
  f.kind = td->kind;
  f.score = td->score;
  f.radius = td->radius;
  f.hp = td->kind == FishKind::kHp ? td->hp : 0;
  f.hp_max = f.hp;
  f.born_ms = now_ms_;
  f.ttl_ms = cfg_.default_ttl_ms;
  f.alive = true;

  const bool from_left = (rng_() % 2u) == 0u;
  f.y = 200.f + static_cast<float>(rng_() % 600u);
  f.x = from_left ? -40.f : static_cast<float>(cfg_.scene_w) + 40.f;
  const float spd = 80.f + static_cast<float>(rng_() % 120u);
  f.vx = from_left ? spd : -spd;
  f.vy = (static_cast<int>(rng_() % 41u) - 20) * 0.5f;

  fish_.push_back(f);
  OutEvent ev;
  ev.type = "spawn";
  ev.fish_id = f.fish_id;
  ev.type_id = f.type_id;
  ev.x = f.x;
  ev.y = f.y;
  ev.vx = f.vx;
  ev.vy = f.vy;
  ev.radius = f.radius;
  ev.hp = f.hp;
  ev.hp_max = f.hp_max;
  Emit(ev);
}

void FishTable::SpawnFishForTest(const FishInst& f) {
  FishInst copy = f;
  if (copy.fish_id <= 0) copy.fish_id = next_fish_id_++;
  else if (copy.fish_id >= next_fish_id_) next_fish_id_ = copy.fish_id + 1;
  copy.alive = true;
  if (copy.born_ms == 0) copy.born_ms = now_ms_;
  fish_.push_back(copy);
  OutEvent ev;
  ev.type = "spawn";
  ev.fish_id = copy.fish_id;
  ev.type_id = copy.type_id;
  ev.x = copy.x;
  ev.y = copy.y;
  ev.vx = copy.vx;
  ev.vy = copy.vy;
  ev.radius = copy.radius;
  ev.hp = copy.hp;
  ev.hp_max = copy.hp_max;
  Emit(ev);
}

void FishTable::ClearFishForTest() { fish_.clear(); }

int FishTable::AliveFishCount() const {
  int n = 0;
  for (const auto& f : fish_)
    if (f.alive) ++n;
  return n;
}

int FishTable::AliveBulletCount() const {
  int n = 0;
  for (const auto& b : bullets_)
    if (b.alive) ++n;
  return n;
}

const FishInst* FishTable::FindFish(int64_t fish_id) const {
  for (const auto& f : fish_)
    if (f.fish_id == fish_id) return &f;
  return nullptr;
}

std::vector<FishInst> FishTable::SnapshotFish() const {
  std::vector<FishInst> out;
  for (const auto& f : fish_)
    if (f.alive) out.push_back(f);
  return out;
}

void FishTable::KillFish(FishInst& f, const char* reason, int64_t catch_uid, int catch_seat, int64_t reward) {
  if (!f.alive) return;
  f.alive = false;
  OutEvent ev;
  if (reward > 0 && catch_uid > 0) {
    if (caught_fish_ids_.count(f.fish_id)) return;
    caught_fish_ids_.insert(f.fish_id);
    CatchRewardPlan plan;
    plan.uid = catch_uid;
    plan.seat = catch_seat;
    plan.fish_id = f.fish_id;
    plan.type_id = f.type_id;
    plan.reward = reward;
    pending_rewards_.push_back(plan);
    ev.type = "catch";
    ev.fish_id = f.fish_id;
    ev.type_id = f.type_id;
    ev.uid = catch_uid;
    ev.seat = catch_seat;
    ev.reward = reward;
    ev.detail = reason ? reason : "";
  } else {
    ev.type = "despawn";
    ev.fish_id = f.fish_id;
    ev.type_id = f.type_id;
    ev.detail = reason ? reason : "";
  }
  Emit(ev);
}

void FishTable::ResolveHit(Bullet& b, FishInst& f) {
  b.alive = false;
  OutEvent hit;
  hit.type = "hit";
  hit.bullet_id = b.bullet_id;
  hit.fish_id = f.fish_id;
  hit.seat = b.seat;
  hit.uid = b.uid;
  hit.mult = b.mult;

  if (f.kind == FishKind::kHp) {
    const int dmg = HpDamage(b.mult);
    f.hp -= dmg;
    hit.hp = f.hp;
    hit.hp_max = f.hp_max;
    Emit(hit);
    if (f.hp <= 0) {
      const int64_t reward = CatchReward(f.score, cfg_.base_score, cfg_.max_catch_reward);
      KillFish(f, "hp", b.uid, b.seat, reward);
    }
    return;
  }

  // odds
  Emit(hit);
  const double p = CatchProb(b.mult, f.score, cfg_.p_min, cfg_.p_max);
  uint32_t r = rng_ ? rng_() : 0;
  const double roll = static_cast<double>(r % 1000000u) / 1000000.0;
  if (roll < p) {
    const int64_t reward = CatchReward(f.score, cfg_.base_score, cfg_.max_catch_reward);
    KillFish(f, "odds", b.uid, b.seat, reward);
  }
}

void FishTable::MoveEntities(int dt_ms) {
  const float dt = static_cast<float>(dt_ms) / 1000.f;
  for (auto& f : fish_) {
    if (!f.alive) continue;
    f.x += f.vx * dt;
    f.y += f.vy * dt;
  }
  for (auto& b : bullets_) {
    if (!b.alive) continue;
    if (b.lock_fish_id > 0) {
      for (auto& f : fish_) {
        if (f.alive && f.fish_id == b.lock_fish_id) {
          float dx = f.x - b.x;
          float dy = f.y - b.y;
          const float len = std::sqrt(dx * dx + dy * dy);
          if (len > 1e-3f) {
            const float spd = cfg_.bullet_speed > 0 ? cfg_.bullet_speed : 1200.f;
            b.vx = dx / len * spd;
            b.vy = dy / len * spd;
          }
          break;
        }
      }
    }
    b.x += b.vx * dt;
    b.y += b.vy * dt;
  }
}

void FishTable::Collide() {
  for (auto& b : bullets_) {
    if (!b.alive) continue;
    // out of scene
    if (b.x < -100.f || b.y < -100.f || b.x > cfg_.scene_w + 100.f || b.y > cfg_.scene_h + 100.f) {
      b.alive = false;
      continue;
    }
    for (auto& f : fish_) {
      if (!f.alive || !b.alive) continue;
      if (b.lock_fish_id > 0 && f.fish_id != b.lock_fish_id) continue;
      const float dx = b.x - f.x;
      const float dy = b.y - f.y;
      const float rr = b.radius + f.radius;
      if (dx * dx + dy * dy <= rr * rr) {
        ResolveHit(b, f);
        break;
      }
    }
  }
}

void FishTable::Tick(int dt_ms) {
  if (!started_ || dt_ms <= 0) return;
  now_ms_ += dt_ms;

  spawn_accum_ms_ += dt_ms;
  while (spawn_accum_ms_ >= spawn_interval_ms_) {
    spawn_accum_ms_ -= spawn_interval_ms_;
    SpawnOne();
  }

  MoveEntities(dt_ms);

  // despawn out of bounds / ttl
  for (auto& f : fish_) {
    if (!f.alive) continue;
    const bool oob = f.x < -200.f || f.x > cfg_.scene_w + 200.f || f.y < -200.f || f.y > cfg_.scene_h + 200.f;
    const bool ttl = f.ttl_ms > 0 && (now_ms_ - f.born_ms) >= f.ttl_ms;
    if (oob || ttl) KillFish(f, oob ? "oob" : "ttl", 0, -1, 0);
  }

  Collide();
}

}  // namespace fish
}  // namespace pandora
