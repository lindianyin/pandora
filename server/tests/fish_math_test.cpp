#include "game/fish/math.hpp"

#include <cmath>
#include <iostream>

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

void TestT01() {
  Expect(FireCost(10, 100) == 1000, "T01 cost M*B");
}

void TestT02() {
  Expect(FireRake(1000, 500) == 50, "T02 rake floor");
  Expect(FireRake(1000, 0) == 0, "T02 rake zero bp");
}

void TestT03() {
  Expect(CatchReward(20, 100, 1000000) == 2000, "T03 reward S*B");
}

void TestT04() {
  Expect(CatchReward(20000, 100, 1000000) == 1000000, "T04 reward clamp");
}

void TestT05() {
  const double p = CatchProb(5, 100, 0.01, 0.95);
  Expect(std::abs(p - 0.05) < 1e-9, "T05 p=0.05");
}

void TestT06() {
  const double p = CatchProb(200, 10, 0.01, 0.95);
  Expect(std::abs(p - 0.95) < 1e-9, "T06 p=p_max");
}

void TestT07() {
  Expect(HpDamage(10) == 10, "T07 dmg=M");
  Expect(HpDamage(0) == 1, "T07 dmg min 1");
  int hp = 25;
  hp -= HpDamage(10);
  Expect(hp == 15, "T07 hp after first hit");
  hp -= HpDamage(10);
  Expect(hp == 5, "T07 hp after second");
  hp -= HpDamage(10);
  Expect(hp <= 0, "T07 hp depleted");
}

int main() {
  TestT01();
  TestT02();
  TestT03();
  TestT04();
  TestT05();
  TestT06();
  TestT07();
  if (fails) {
    std::cerr << fails << " failed\n";
    return 1;
  }
  std::cout << "all fish_math tests passed\n";
  return 0;
}
