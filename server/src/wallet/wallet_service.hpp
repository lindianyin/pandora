#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "common/config.hpp"
#include "store/memory_store.hpp"

namespace pandora {

struct AdjustResult {
  bool ok{false};
  int64_t balance{0};
  std::string error;
};

class WalletService {
 public:
  WalletService(MemoryStore& store, int64_t diamond_to_gold);

  std::optional<PlayerRecord> Profile(int64_t uid);
  AdjustResult Adjust(int64_t uid, Currency currency, int64_t delta, const std::string& biz_type,
                      const std::string& idem_key, const std::string& ref_id = {});
  AdjustResult ExchangeDiamondToGold(int64_t uid, int64_t diamond, const std::string& client_order_id);

  int64_t DiamondToGoldRate() const { return diamond_to_gold_; }

 private:
  MemoryStore& store_;
  int64_t diamond_to_gold_{1000};
};

}  // namespace pandora
