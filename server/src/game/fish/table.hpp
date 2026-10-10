#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "game/fish/config.hpp"

namespace pandora {
namespace fish {

constexpr int kMaxSeats = 4;

struct SeatCannon {
  float x{0};
  float y{0};
};

// Seat layout (W=1920,H=1080, origin bottom-left): bottom x2, top x2, no overlap.
// 0 bottom-left, 1 bottom-right, 2 top-left, 3 top-right.
inline const SeatCannon kSeatCannonPos[kMaxSeats] = {
    {420.f, 70.f},
    {1500.f, 70.f},
    {420.f, 1050.f},
    {1500.f, 1050.f},
};

struct FireRequest {
  int seat{0};
  int64_t uid{0};
  int mult{1};
  float aim_x{0};
  float aim_y{0};
  int64_t lock_fish_id{0};
  int64_t client_seq{0};
};

struct FireCostPlan {
  int64_t uid{0};
  int seat{0};
  int64_t cost{0};
  int64_t rake{0};
  int64_t client_seq{0};
  int64_t bullet_id{0};
  int mult{0};
};

struct CatchRewardPlan {
  int64_t uid{0};
  int seat{0};
  int64_t fish_id{0};
  int32_t type_id{0};
  int64_t reward{0};
};

struct OutEvent {
  std::string type;  // spawn|despawn|fire|hit|catch|seat_update|start
  int64_t fish_id{0};
  int64_t bullet_id{0};
  int32_t type_id{0};
  int seat{-1};
  int64_t uid{0};
  int32_t hp{0};
  int32_t hp_max{0};
  float x{0};
  float y{0};
  float vx{0};
  float vy{0};
  float radius{0};
  int64_t reward{0};
  int mult{0};
  int64_t client_seq{0};
  std::string detail;
};

struct FishInst {
  int64_t fish_id{0};
  int32_t type_id{0};
  float x{0};
  float y{0};
  float vx{0};
  float vy{0};
  float radius{40.f};
  int32_t hp{0};
  int32_t hp_max{0};
  FishKind kind{FishKind::kOdds};
  int32_t score{10};
  bool alive{true};
  int64_t born_ms{0};
  int64_t ttl_ms{20000};
};

struct Bullet {
  int64_t bullet_id{0};
  int seat{0};
  int64_t uid{0};
  int mult{1};
  float x{0};
  float y{0};
  float vx{0};
  float vy{0};
  float radius{8.f};
  int64_t lock_fish_id{0};
  bool alive{true};
};

struct SeatState {
  int64_t uid{0};
  int mult{1};
  bool online{true};
  bool left{false};
  int64_t last_fire_ms{-1000000};
  std::unordered_set<int64_t> seen_seq;
  int64_t max_client_seq{0};
};

class FishTable {
 public:
  FishTable(FishConfig cfg, std::function<uint32_t()> rng);

  void SetSink(std::function<void(const OutEvent&)> sink);
  void Start(const std::vector<int64_t>& seat_uids);
  void Tick(int dt_ms);

  bool SetCannonMult(int seat, int mult);
  bool TryFire(const FireRequest& req, FireCostPlan* cost);
  void OnLeave(int seat);
  void SetOnline(int seat, bool online);

  // Test helpers
  void SpawnFishForTest(const FishInst& f);
  void ClearFishForTest();
  int AliveFishCount() const;
  const FishInst* FindFish(int64_t fish_id) const;
  std::vector<FishInst> SnapshotFish() const;
  int AliveBulletCount() const;
  int64_t now_ms() const { return now_ms_; }
  const SeatState& seat(int i) const { return seats_[static_cast<size_t>(i)]; }
  bool started() const { return started_; }
  const std::vector<CatchRewardPlan>& pending_rewards() const { return pending_rewards_; }
  void ClearPendingRewards() { pending_rewards_.clear(); }

 private:
  const FishTypeDef* PickType();
  void SpawnOne();
  void MoveEntities(int dt_ms);
  void Collide();
  void ResolveHit(Bullet& b, FishInst& f);
  void KillFish(FishInst& f, const char* reason, int64_t catch_uid, int catch_seat, int64_t reward);
  void Emit(const OutEvent& ev);
  bool ValidMult(int mult) const;
  int MinFireIntervalMs() const;

  FishConfig cfg_;
  std::function<uint32_t()> rng_;
  std::function<void(const OutEvent&)> sink_;
  SeatState seats_[kMaxSeats]{};
  std::vector<FishInst> fish_;
  std::vector<Bullet> bullets_;
  std::vector<CatchRewardPlan> pending_rewards_;
  std::unordered_set<int64_t> caught_fish_ids_;
  int64_t next_fish_id_{1};
  int64_t next_bullet_id_{1};
  int64_t now_ms_{0};
  int64_t spawn_accum_ms_{0};
  int spawn_interval_ms_{500};
  int max_alive_{20};
  bool started_{false};
};

}  // namespace fish
}  // namespace pandora
