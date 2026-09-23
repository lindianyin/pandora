#include "game/ddz_cards.hpp"

#include <cassert>
#include <iostream>

using namespace pandora::ddz;

static int fails = 0;

void Expect(bool cond, const char* msg) {
  if (!cond) {
    std::cerr << "FAIL: " << msg << "\n";
    ++fails;
  } else {
    std::cout << "OK: " << msg << "\n";
  }
}

int main() {
  {
    auto p = Identify({5});
    Expect(p.type == PatternType::kSingle, "single");
  }
  {
    auto p = Identify({5, 18});  // same rank different suit: 5%13=5, 18%13=5
    Expect(p.type == PatternType::kPair, "pair");
  }
  {
    auto p = Identify({0, 1, 2, 3, 4});  // 3,4,5,6,7
    Expect(p.type == PatternType::kStraight && p.length == 5, "straight5");
  }
  {
    auto bomb = Identify({0, 13, 26, 39});  // four 3s
    Expect(bomb.type == PatternType::kBomb, "bomb");
    auto single = Identify({12});  // 2
    Expect(CanBeat(bomb, single), "bomb beats single");
  }
  {
    auto rocket = Identify({52, 53});
    Expect(rocket.type == PatternType::kRocket, "rocket");
    auto bomb = Identify({0, 13, 26, 39});
    Expect(CanBeat(rocket, bomb), "rocket beats bomb");
  }
  {
    auto a = Identify({4});
    auto b = Identify({5});
    Expect(CanBeat(b, a), "higher single beats");
    Expect(!CanBeat(a, b), "lower cannot beat");
  }
  {
    std::vector<int> hand{1, 2, 3, 4};
    Expect(ContainsAll(hand, {2, 4}), "contains");
    RemoveCards(hand, {2, 4});
    Expect(hand.size() == 2, "remove");
  }

  if (fails) {
    std::cerr << fails << " failures\n";
    return 1;
  }
  std::cout << "all passed\n";
  return 0;
}

