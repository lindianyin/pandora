#include "pay/pay_service.hpp"

#include <chrono>
#include <sstream>

#include "common/log.hpp"

namespace pandora {

PayService::PayService(WalletService& wallet, MysqlClient& mysql, AlipayConfig cfg)
    : wallet_(wallet), mysql_(mysql), cfg_(std::move(cfg)) {
  products_ = {
      {1, 600, 60, 0, true},
      {2, 3000, 300, 30, true},
      {3, 9800, 980, 100, true},
  };
}

std::string PayService::Escape(const std::string& s) const {
  std::string o;
  for (char c : s) {
    if (c == '\'' || c == '\\') o.push_back('\\');
    o.push_back(c);
  }
  return o;
}

std::vector<PayProduct> PayService::ListProducts(bool include_disabled) const {
  std::lock_guard<std::mutex> lk(mu_);
  std::vector<PayProduct> out;
  for (const auto& p : products_)
    if (include_disabled || p.enabled) out.push_back(p);
  return out;
}

void PayService::ReloadFromDb(MysqlClient& mysql) {
  auto rows = mysql.Query("SELECT id,amount_fen,diamond,gift_diamond,enabled FROM pay_product ORDER BY sort,id");
  if (!rows || rows->empty()) return;
  std::lock_guard<std::mutex> lk(mu_);
  products_.clear();
  for (const auto& row : *rows) {
    if (row.cols.size() < 5) continue;
    PayProduct p;
    p.id = std::stoi(row.cols[0]);
    p.amount_fen = std::stoi(row.cols[1]);
    p.diamond = std::stoi(row.cols[2]);
    p.gift_diamond = std::stoi(row.cols[3]);
    p.enabled = row.cols[4] == "1";
    products_.push_back(p);
  }
}

void PayService::UpsertProduct(int id, int amount_fen, int diamond, int gift, bool enabled) {
  std::lock_guard<std::mutex> lk(mu_);
  for (auto& p : products_) {
    if (p.id == id) {
      if (amount_fen > 0) p.amount_fen = amount_fen;
      if (diamond > 0) p.diamond = diamond;
      p.gift_diamond = gift;
      p.enabled = enabled;
      return;
    }
  }
  if (id <= 0) id = products_.empty() ? 1 : products_.back().id + 1;
  products_.push_back({id, amount_fen, diamond, gift, enabled});
}

std::optional<PayOrder> PayService::LoadOrder(const std::string& order_id) {
  if (!mysql_.Available()) return std::nullopt;
  auto rows = mysql_.QueryBind(
      "SELECT order_id,uid,product_id,amount_fen,status,IFNULL(alipay_trade_no,''),"
      "(SELECT diamond+gift_diamond FROM pay_product WHERE id=pay_order.product_id LIMIT 1) "
      "FROM pay_order WHERE order_id=? LIMIT 1",
      {Str(order_id)});
  if (!rows || rows->empty() || rows->front().cols.size() < 6) return std::nullopt;
  const auto& c = rows->front().cols;
  PayOrder o;
  o.order_id = c[0];
  try {
    o.uid = std::stoll(c[1]);
    o.product_id = std::stoi(c[2]);
    o.amount_fen = std::stoi(c[3]);
    o.status = std::stoi(c[4]);
  } catch (...) {
    return std::nullopt;
  }
  o.alipay_trade_no = c[5];
  if (c.size() >= 7 && !c[6].empty()) {
    try {
      o.diamond = std::stoi(c[6]);
    } catch (...) {
      o.diamond = 0;
    }
  }
  // fallback diamond from amount if join null
  if (o.diamond <= 0) {
    std::lock_guard<std::mutex> lk(mu_);
    for (const auto& p : products_) {
      if (p.id == o.product_id) {
        o.diamond = p.diamond + p.gift_diamond;
        break;
      }
    }
  }
  return o;
}

std::vector<PayOrder> PayService::ListOrders(size_t limit) const {
  std::vector<PayOrder> out;
  if (!mysql_.Available()) return out;
  auto rows = mysql_.QueryBind(
      "SELECT o.order_id,o.uid,o.product_id,o.amount_fen,o.status,IFNULL(o.alipay_trade_no,''),"
      "IFNULL(p.diamond,0)+IFNULL(p.gift_diamond,0) FROM pay_order o "
      "LEFT JOIN pay_product p ON p.id=o.product_id ORDER BY o.created_at DESC LIMIT ?",
      {I64(static_cast<int64_t>(limit))});
  if (!rows) return out;
  for (const auto& row : *rows) {
    if (row.cols.size() < 7) continue;
    PayOrder o;
    o.order_id = row.cols[0];
    try {
      o.uid = std::stoll(row.cols[1]);
      o.product_id = std::stoi(row.cols[2]);
      o.amount_fen = std::stoi(row.cols[3]);
      o.status = std::stoi(row.cols[4]);
      o.diamond = std::stoi(row.cols[6]);
    } catch (...) {
      continue;
    }
    o.alipay_trade_no = row.cols[5];
    out.push_back(o);
  }
  return out;
}

std::optional<PayOrder> PayService::CreateOrder(int64_t uid, int product_id) {
  PayProduct prod{};
  bool found = false;
  {
    std::lock_guard<std::mutex> lk(mu_);
    for (const auto& p : products_) {
      if (p.id == product_id && p.enabled) {
        prod = p;
        found = true;
        break;
      }
    }
  }
  if (!found) return std::nullopt;
  PayOrder o;
  const auto ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
          .count();
  o.order_id = "P" + std::to_string(ms) + "_" + std::to_string(uid);
  o.uid = uid;
  o.product_id = prod.id;
  o.amount_fen = prod.amount_fen;
  o.diamond = prod.diamond + prod.gift_diamond;
  o.status = 0;
  if (mysql_.Available()) {
    const int r = mysql_.ExecBind(
        "INSERT INTO pay_order(order_id,uid,product_id,amount_fen,status,alipay_trade_no,idempotent_paid) "
        "VALUES(?,?,?,?,0,'',0)",
        {Str(o.order_id), I64(uid), I64(prod.id), I64(prod.amount_fen)});
    if (r < 0) {
      PLOG_WARN("CreateOrder mysql fail: " << mysql_.LastError());
      return std::nullopt;
    }
  } else {
    PLOG_WARN("CreateOrder without mysql");
    return std::nullopt;
  }
  return o;
}

std::string PayService::BuildOrderStr(const PayOrder& order) const {
  std::ostringstream oss;
  oss << "app_id=" << cfg_.app_id << "&out_trade_no=" << order.order_id << "&total_amount="
      << (order.amount_fen / 100.0) << "&subject=PandoraDiamond&product_code=QUICK_MSECURITY_PAY"
      << "&notify_url=" << cfg_.notify_url << "&sandbox=" << (cfg_.sandbox ? "1" : "0")
      << "&sign=SANDBOX_PLACEHOLDER";
  return oss.str();
}

bool PayService::PersistPaid(PayOrder& o, const std::string& trade_no) {
  if (o.status == 1) return true;
  o.status = 1;
  o.alipay_trade_no = trade_no.empty() ? ("SANDBOX_" + o.order_id) : trade_no;
  const std::string idem = "pay:" + o.order_id;
  auto r = wallet_.Adjust(o.uid, Currency::kDiamond, o.diamond, "pay_recharge", idem, o.order_id);
  if (!r.ok) return false;
  if (mysql_.Available()) {
    mysql_.ExecBind(
        "UPDATE pay_order SET status=1,alipay_trade_no=?,idempotent_paid=1,paid_at=NOW(3) WHERE order_id=?",
        {Str(o.alipay_trade_no), Str(o.order_id)});
  }
  return true;
}

bool PayService::HandleNotify(const std::string& order_id, const std::string& trade_no, int amount_fen) {
  auto loaded = LoadOrder(order_id);
  if (!loaded) return false;
  PayOrder o = *loaded;
  if (o.status == 1) return true;
  if (amount_fen > 0 && amount_fen != o.amount_fen) return false;
  return PersistPaid(o, trade_no);
}

bool PayService::SandboxComplete(int64_t uid, const std::string& order_id) {
  if (!cfg_.sandbox) return false;
  auto loaded = LoadOrder(order_id);
  if (!loaded) return false;
  if (loaded->uid != uid) return false;
  PayOrder o = *loaded;
  if (o.status == 1) return true;
  return PersistPaid(o, "SANDBOX_" + order_id);
}

}  // namespace pandora
