#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "common/async_worker.hpp"
#include "common/config.hpp"
#include "net/session_hub.hpp"
#include "store/mysql_client.hpp"
#include "store/redis_client.hpp"

namespace pandora {

struct ItemDef {
  int id{0};
  std::string name;
  std::string icon;
  std::string kind;  // qty=数量 / qty_ttl=数量+时效 / ttl=时效(过期累加，持有期持续有效)
  bool stackable{true};
  int default_expire_sec{0};
  std::string tag;
  bool enabled{true};
};

struct BagItemView {
  int item_id{0};
  std::string name;
  std::string kind;
  std::string icon;
  std::string tag;
  int64_t quantity{0};
  std::string expire_at;  // readable
};

struct ExpirePolicy {
  enum class Mode { kNone, kDurationSec, kAbsolute };
  Mode mode{Mode::kNone};
  int duration_sec{0};
  std::string absolute;  // YYYY-MM-DD HH:mm:ss[.fff]
};

struct BagOpResult {
  bool ok{false};
  std::string error;
  int err_code{0};  // Err::* as int when set
  int64_t quantity_after{0};
  std::string expire_at;
};

class BagService {
 public:
  BagService(MysqlClient& mysql, RedisClient& redis, SessionHub& hub, AsyncWorker& persist, BagConfig cfg);

  void Bootstrap();
  void ReloadDefs();

  std::vector<ItemDef> ListDefs(bool include_disabled = false) const;
  std::optional<ItemDef> GetDef(int item_id) const;
  bool UpsertDef(const ItemDef& def, std::string* err);
  bool SetDefEnabled(int id, bool enabled, std::string* err);

  std::string ListBagJson(int64_t uid, bool include_expired) const;
  std::string ListLedgersJson(int64_t uid, int item_id, int page, int page_size) const;
  std::string ListDefsJson(bool include_disabled) const;

  BagOpResult Grant(int64_t uid, int item_id, int64_t quantity, const ExpirePolicy& policy,
                    const std::string& idempotent_key, const std::string& biz_type, const std::string& ref_id);
  BagOpResult Consume(int64_t uid, int item_id, int64_t quantity, const std::string& prefer_expire_at,
                      const std::string& idempotent_key, const std::string& biz_type, const std::string& ref_id);
  int64_t GetQuantity(int64_t uid, int item_id) const;

  /** Grant items from reward JSON array; parent_idem used for sub-keys. */
  bool GrantItemsFromJson(int64_t uid, const std::string& items_json_array, const std::string& parent_idem,
                          const std::string& biz_type, const std::string& ref_id, std::string* err);

 private:
  static constexpr const char* kNeverExpire = "9999-12-31 23:59:59.999";
  std::string ResolveExpireAt(const ItemDef& def, const ExpirePolicy& policy) const;
  int ResolveDurationSec(const ItemDef& def, const ExpirePolicy& policy) const;
  /** ttl：同 item_id 仅一行；未过期则 expire_at 累加时长，像 buff 持续有效 */
  BagOpResult GrantTtl(int64_t uid, int item_id, int64_t quantity, const ExpirePolicy& policy,
                       const std::string& idempotent_key, const std::string& biz_type, const std::string& ref_id,
                       const ItemDef& def);
  void PushUpdate(int64_t uid, int item_id, int64_t qty, const std::string& expire_at, int reason);
  bool LedgerExists(const std::string& idem) const;

  MysqlClient& mysql_;
  RedisClient& redis_;
  SessionHub& hub_;
  AsyncWorker& persist_;
  BagConfig cfg_;

  mutable std::mutex mu_;
  std::vector<ItemDef> defs_;
};

}  // namespace pandora
