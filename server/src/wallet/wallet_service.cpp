#include "wallet/wallet_service.hpp"

#include "common/errors.hpp"

namespace pandora {

WalletService::WalletService(MemoryStore& store, int64_t diamond_to_gold)
    : store_(store), diamond_to_gold_(diamond_to_gold > 0 ? diamond_to_gold : 1000) {}

std::optional<PlayerRecord> WalletService::Profile(int64_t uid) { return store_.GetPlayer(uid); }

AdjustResult WalletService::Adjust(int64_t uid, Currency currency, int64_t delta, const std::string& biz_type,
                                   const std::string& idem_key, const std::string& ref_id) {
  AdjustResult r;
  auto bal = store_.Adjust(uid, currency, delta, biz_type, idem_key, ref_id);
  if (!bal) {
    r.ok = false;
    r.error = "insufficient or unknown player";
    return r;
  }
  r.ok = true;
  r.balance = *bal;
  return r;
}

AdjustResult WalletService::ExchangeDiamondToGold(int64_t uid, int64_t diamond, const std::string& client_order_id) {
  AdjustResult r;
  if (diamond <= 0) {
    r.error = "bad diamond amount";
    return r;
  }
  const std::string idem = "ex:" + std::to_string(uid) + ":" +
                           (client_order_id.empty() ? std::to_string(diamond) : client_order_id);
  auto player = store_.GetPlayer(uid);
  if (!player) {
    r.error = "player not found";
    return r;
  }
  // Check idempotent: if key exists, return current gold
  auto burn = store_.Adjust(uid, Currency::kDiamond, -diamond, "exchange", idem + ":d", client_order_id);
  if (!burn) {
    r.error = "insufficient diamond";
    return r;
  }
  const int64_t gold_gain = diamond * diamond_to_gold_;
  auto credit = store_.Adjust(uid, Currency::kGold, gold_gain, "exchange", idem + ":g", client_order_id);
  if (!credit) {
    // Should not happen for credit; rollback diamond best-effort
    store_.Adjust(uid, Currency::kDiamond, diamond, "exchange_rollback", idem + ":rb", client_order_id);
    r.error = "credit gold failed";
    return r;
  }
  r.ok = true;
  r.balance = *credit;
  return r;
}

}  // namespace pandora
