#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "bag/bag_service.hpp"
#include "store/mysql_client.hpp"
#include "wallet/wallet_service.hpp"

namespace pandora {

struct PayProduct {
  int id{0};
  int amount_fen{0};
  int diamond{0};
  int gift_diamond{0};
  std::string gift_items_json{"[]"};
  bool enabled{true};
};

struct PayOrder {
  std::string order_id;
  int64_t uid{0};
  int product_id{0};
  int amount_fen{0};
  int diamond{0};
  int status{0};
  std::string alipay_trade_no;
  std::string created_at;
};

struct AlipayConfig {
  bool sandbox{true};
  std::string app_id{"REPLACE"};
  std::string merchant_private_key{"REPLACE"};
  std::string alipay_public_key{"REPLACE"};
  std::string gateway{"https://openapi.alipay.com/gateway.do"};
  std::string notify_url{"http://127.0.0.1:8080/api/v1/pay/alipay/notify"};
};

class PayService {
 public:
  PayService(WalletService& wallet, MysqlClient& mysql, BagService& bag, AlipayConfig cfg);

  std::vector<PayProduct> ListProducts(bool include_disabled = false) const;
  std::optional<PayOrder> CreateOrder(int64_t uid, int product_id);
  std::string BuildOrderStr(const PayOrder& order) const;
  bool HandleNotify(const std::string& order_id, const std::string& trade_no, int amount_fen);
  bool SandboxComplete(int64_t uid, const std::string& order_id);
  bool Sandbox() const { return cfg_.sandbox; }

  void ReloadFromDb(MysqlClient& mysql);
  void UpsertProduct(int id, int amount_fen, int diamond, int gift, const std::string& gift_items_json, bool enabled);
  std::vector<PayOrder> ListOrders(size_t limit = 100) const;

 private:
  std::string Escape(const std::string& s) const;
  std::optional<PayOrder> LoadOrder(const std::string& order_id);
  bool PersistPaid(PayOrder& o, const std::string& trade_no);
  std::string LoadGiftItemsJson(int product_id) const;

  WalletService& wallet_;
  MysqlClient& mysql_;
  BagService& bag_;
  AlipayConfig cfg_;
  mutable std::mutex mu_;
  std::vector<PayProduct> products_;
};

}  // namespace pandora
