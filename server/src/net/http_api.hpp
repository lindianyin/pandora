#pragma once

#include "activity/activity_service.hpp"
#include "admin/admin_service.hpp"
#include "auth/auth_service.hpp"
#include "common/config.hpp"
#include "net/iocp_pool.hpp"
#include "net/session_hub.hpp"
#include "pay/pay_service.hpp"
#include "store/memory_store.hpp"
#include "store/mysql_client.hpp"
#include "store/redis_client.hpp"

namespace pandora {

class HttpApi {
 public:
  HttpApi(AppConfig cfg, MemoryStore& store, AuthService& auth, WalletService& wallet, PayService& pay,
          AdminService& admin, ActivityService& activity, MysqlClient& mysql, RedisClient& redis, SessionHub& hub);
  void Run();

 private:
  AppConfig cfg_;
  MemoryStore& store_;
  AuthService& auth_;
  WalletService& wallet_;
  PayService& pay_;
  AdminService& admin_;
  ActivityService& activity_;
  MysqlClient& mysql_;
  RedisClient& redis_;
  SessionHub& hub_;
  IocpPool pool_;
};

}  // namespace pandora
