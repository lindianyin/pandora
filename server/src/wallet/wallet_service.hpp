#pragma once

#include <cstdint>
#include <functional>
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
  using GoldChangedFn = std::function<void(int64_t uid, int64_t gold)>;

  WalletService(MemoryStore& store, int64_t diamond_to_gold);

  void SetOnGoldChanged(GoldChangedFn fn) { on_gold_changed_ = std::move(fn); }

  std::optional<PlayerRecord> Profile(int64_t uid);
  AdjustResult Adjust(int64_t uid, Currency currency, int64_t delta, const std::string& biz_type,
                      const std::string& idem_key, const std::string& ref_id = {});
  AdjustResult ExchangeDiamondToGold(int64_t uid, int64_t diamond, const std::string& client_order_id);

  int64_t DiamondToGoldRate() const { return diamond_to_gold_; }

 private:
  MemoryStore& store_;
  int64_t diamond_to_gold_{1000};
  GoldChangedFn on_gold_changed_;
};

}  // namespace pandora
