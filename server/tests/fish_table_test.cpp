#include "game/fish/table.hpp"

#include <iostream>
#include <string>
#include <vector>

using namespace pandora::fish;

static int fails = 0;
void Expect(bool cond, const char* msg) {
  if (!cond) {
    std::cerr << "FAIL: " << msg << "\n";
    ++fails;
  } else {
    std::cout << "OK: " << msg << "\n";
  }
}

FishConfig TestCfg() {
  auto c = DefaultFishConfig();
  c.base_score = 100;
  c.rake_bp = 500;
  c.fire_rate_hz = 8;
  c.bullet_speed = 2000.f;
  c.default_ttl_ms = 5000;
  c.waves[0].spawn_interval_ms = 200;
  c.waves[0].max_alive = 10;
  return c;
}

// RNG that returns values cycling from queue; for odds, roll = (r%1e6)/1e6
struct ScriptRng {
  std::vector<uint32_t> vals;
  size_t i{0};
  uint32_t operator()() {
    if (vals.empty()) return 0;
    uint32_t v = vals[i % vals.size()];
    ++i;
    return v;
  }
};

void TestT10() {
  ScriptRng rng{{0, 1, 2, 3, 4, 5}};
  FishTable t(TestCfg(), [&]() { return rng(); });
  t.Start({1, 0, 0, 0});
  Expect(t.AliveFishCount() == 0, "T10 start empty");
  for (int i = 0; i < 20; ++i) t.Tick(200);
  Expect(t.AliveFishCount() > 0, "T10 spawned");
  Expect(t.AliveFishCount() <= 10, "T10 max alive");
}

void TestT11() {
  ScriptRng rng{{0}};
  FishTable t(TestCfg(), [&]() { return rng(); });
  std::vector<std::string> evs;
  t.SetSink([&](const OutEvent& e) { evs.push_back(e.type); });
  t.Start({1});
  FishInst f;
  f.fish_id = 99;
  f.type_id = 1;
  f.x = 100;
  f.y = 500;
  f.vx = -500;
  f.vy = 0;
  f.radius = 30;
  f.kind = FishKind::kOdds;
  f.score = 10;
  f.ttl_ms = 1000;
  f.born_ms = 0;
  t.SpawnFishForTest(f);
  bool saw_despawn = false;
  bool saw_catch = false;
  for (int i = 0; i < 30; ++i) {
    t.Tick(100);
    for (const auto& e : evs) {
      if (e == "despawn") saw_despawn = true;
      if (e == "catch") saw_catch = true;
    }
  }
  Expect(saw_despawn, "T11 despawn");
  Expect(!saw_catch, "T11 no catch on oob/ttl");
  Expect(t.FindFish(99) == nullptr || !t.FindFish(99)->alive, "T11 fish dead");
}

void TestT12() {
  ScriptRng rng{{0}};
  FishTable t(TestCfg(), [&]() { return rng(); });
  std::vector<std::string> evs;
  t.SetSink([&](const OutEvent& e) { evs.push_back(e.type); });
  t.Start({42});
  FireCostPlan plan{};
  FireRequest req;
  req.seat = 0;
  req.uid = 42;
  req.mult = 10;
  req.aim_x = 480;
  req.aim_y = 800;
  req.client_seq = 1;
  Expect(t.TryFire(req, &plan), "T12 fire ok");
  Expect(plan.cost == 1000, "T12 cost");
  Expect(plan.rake == 50, "T12 rake");
  Expect(t.AliveBulletCount() == 1, "T12 bullet");
  bool saw_fire = false;
  for (const auto& e : evs)
    if (e == "fire") saw_fire = true;
  Expect(saw_fire, "T12 fire event");
}

void TestT13() {
  ScriptRng rng{{0}};
  FishTable t(TestCfg(), [&]() { return rng(); });
  t.Start({1});
  FireCostPlan plan{};
  FireRequest req;
  req.seat = 0;
  req.uid = 1;
  req.mult = 7;  // not in list
  req.aim_x = 500;
  req.aim_y = 500;
  req.client_seq = 1;
  Expect(!t.TryFire(req, &plan), "T13 illegal mult");
}

void TestT14() {
  auto cfg = TestCfg();
  cfg.fire_rate_hz = 2;  // 500ms
  ScriptRng rng{{0}};
  FishTable t(cfg, [&]() { return rng(); });
  t.Start({1});
  FireCostPlan plan{};
  FireRequest a;
  a.seat = 0;
  a.uid = 1;
  a.mult = 1;
  a.aim_x = 500;
  a.aim_y = 500;
  a.client_seq = 1;
  Expect(t.TryFire(a, &plan), "T14 first fire");
  FireRequest b = a;
  b.client_seq = 2;
  Expect(!t.TryFire(b, &plan), "T14 second too fast");
  t.Tick(500);
  Expect(t.TryFire(b, &plan), "T14 after interval");
}

void TestT15() {
  // Force catch: roll=0 < any p
  ScriptRng rng{{0, 0, 0, 0}};
  auto cfg = TestCfg();
  cfg.p_min = 0.01;
  cfg.p_max = 0.95;
  FishTable t(cfg, [&]() { return rng(); });
  std::vector<OutEvent> evs;
  t.SetSink([&](const OutEvent& e) { evs.push_back(e); });
  t.Start({1});
  FishInst f;
  f.fish_id = 1;
  f.type_id = 1;
  f.x = kSeatCannonPos[0].x;
  f.y = kSeatCannonPos[0].y + 50;
  f.vx = 0;
  f.vy = 0;
  f.radius = 80;
  f.kind = FishKind::kOdds;
  f.score = 10;
  f.ttl_ms = 60000;
  t.SpawnFishForTest(f);
  FireCostPlan plan{};
  FireRequest req;
  req.seat = 0;
  req.uid = 1;
  req.mult = 10;
  req.aim_x = f.x;
  req.aim_y = f.y;
  req.client_seq = 1;
  Expect(t.TryFire(req, &plan), "T15 fire");
  t.Tick(50);
  bool caught = false;
  for (const auto& e : evs)
    if (e.type == "catch" && e.fish_id == 1) caught = true;
  Expect(caught, "T15 catch");
  Expect(t.FindFish(1) && !t.FindFish(1)->alive, "T15 fish dead");
}

void TestT16() {
  // Force miss: roll >= 1.0 effectively — use 999999 so roll~0.999999 > p_max
  ScriptRng rng{{999999u, 999999u, 999999u}};
  auto cfg = TestCfg();
  cfg.p_max = 0.95;
  FishTable t(cfg, [&]() { return rng(); });
  std::vector<OutEvent> evs;
  t.SetSink([&](const OutEvent& e) { evs.push_back(e); });
  t.Start({1});
  FishInst f;
  f.fish_id = 2;
  f.type_id = 1;
  f.x = kSeatCannonPos[0].x;
  f.y = kSeatCannonPos[0].y + 40;
  f.vx = 0;
  f.vy = 0;
  f.radius = 80;
  f.kind = FishKind::kOdds;
  f.score = 10;
  f.ttl_ms = 60000;
  t.SpawnFishForTest(f);
  FireCostPlan plan{};
  FireRequest req;
  req.seat = 0;
  req.uid = 1;
  req.mult = 10;
  req.aim_x = f.x;
  req.aim_y = f.y;
  req.client_seq = 1;
  Expect(t.TryFire(req, &plan), "T16 fire");
  t.Tick(50);
  bool caught = false;
  for (const auto& e : evs)
    if (e.type == "catch") caught = true;
  Expect(!caught, "T16 no catch");
  Expect(t.AliveBulletCount() == 0, "T16 bullet dead");
  Expect(t.FindFish(2) && t.FindFish(2)->alive, "T16 fish alive");
}

void TestT17() {
  ScriptRng rng{{0}};
  FishTable t(TestCfg(), [&]() { return rng(); });
  std::vector<OutEvent> evs;
  t.SetSink([&](const OutEvent& e) { evs.push_back(e); });
  t.Start({1});
  FishInst f;
  f.fish_id = 3;
  f.type_id = 3;
  f.x = kSeatCannonPos[0].x;
  f.y = kSeatCannonPos[0].y + 40;
  f.vx = 0;
  f.vy = 0;
  f.radius = 80;
  f.kind = FishKind::kHp;
  f.score = 200;
  f.hp = 25;
  f.hp_max = 25;
  f.ttl_ms = 60000;
  t.SpawnFishForTest(f);

  int hits = 0;
  bool caught = false;
  for (int seq = 1; seq <= 5; ++seq) {
    t.Tick(200);
    FireCostPlan plan{};
    FireRequest req;
    req.seat = 0;
    req.uid = 1;
    req.mult = 10;
    req.aim_x = f.x;
    req.aim_y = f.y;
    req.client_seq = seq;
    Expect(t.TryFire(req, &plan), "T17 fire");
    t.Tick(50);
  }
  for (const auto& e : evs) {
    if (e.type == "hit") ++hits;
    if (e.type == "catch") caught = true;
  }
  Expect(hits >= 2, "T17 hit events");
  Expect(caught, "T17 final catch");
  Expect(t.FindFish(3) && !t.FindFish(3)->alive, "T17 fish dead");
}

void TestT18() {
  ScriptRng rng{{0, 0, 0}};
  FishTable t(TestCfg(), [&]() { return rng(); });
  int catch_count = 0;
  t.SetSink([&](const OutEvent& e) {
    if (e.type == "catch") ++catch_count;
  });
  t.Start({1});
  FishInst f;
  f.fish_id = 8;
  f.type_id = 1;
  f.x = kSeatCannonPos[0].x;
  f.y = kSeatCannonPos[0].y + 40;
  f.vx = 0;
  f.vy = 0;
  f.radius = 80;
  f.kind = FishKind::kOdds;
  f.score = 10;
  f.ttl_ms = 60000;
  t.SpawnFishForTest(f);
  FireCostPlan plan{};
  FireRequest req;
  req.seat = 0;
  req.uid = 1;
  req.mult = 10;
  req.aim_x = f.x;
  req.aim_y = f.y;
  req.client_seq = 1;
  t.TryFire(req, &plan);
  t.Tick(50);
  Expect(catch_count == 1, "T18 first catch");
  // Try to catch same fish again via second bullet — fish already dead
  t.Tick(200);
  req.client_seq = 2;
  t.TryFire(req, &plan);
  t.Tick(50);
  Expect(catch_count == 1, "T18 no second catch");
  Expect(t.pending_rewards().size() == 1, "T18 one reward plan");
}

void TestT19() {
  ScriptRng rng{{0}};
  FishTable t(TestCfg(), [&]() { return rng(); });
  t.Start({1});
  t.OnLeave(0);
  FireCostPlan plan{};
  FireRequest req;
  req.seat = 0;
  req.uid = 1;
  req.mult = 1;
  req.aim_x = 500;
  req.aim_y = 500;
  req.client_seq = 1;
  Expect(!t.TryFire(req, &plan), "T19 leave cannot fire");
}

void TestT20() {
  ScriptRng rng{{0}};
  FishTable t(TestCfg(), [&]() { return rng(); });
  t.Start({1});
  t.SetOnline(0, false);
  FireCostPlan plan{};
  FireRequest req;
  req.seat = 0;
  req.uid = 1;
  req.mult = 1;
  req.aim_x = 500;
  req.aim_y = 500;
  req.client_seq = 1;
  Expect(!t.TryFire(req, &plan), "T20 offline cannot fire");
}

int main() {
  TestT10();
  TestT11();
  TestT12();
  TestT13();
  TestT14();
  TestT15();
  TestT16();
  TestT17();
  TestT18();
  TestT19();
  TestT20();
  if (fails) {
    std::cerr << fails << " failed\n";
    return 1;
  }
  std::cout << "all fish_table tests passed\n";
  return 0;
}
