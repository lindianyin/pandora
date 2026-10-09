#pragma once

#include <mutex>

#include "game/fish/config.hpp"
#include "store/mysql_client.hpp"

namespace pandora {
namespace fish {

class FishConfigStore {
 public:
  static FishConfigStore& Instance();

  FishConfig Get() const;
  bool Reload(MysqlClient& mysql);
  void SetMemory(FishConfig cfg);

 private:
  mutable std::mutex mu_;
  FishConfig cfg_{DefaultFishConfig()};
};

}  // namespace fish
}  // namespace pandora
