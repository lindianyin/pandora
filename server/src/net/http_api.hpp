#pragma once

#include "activity/activity_service.hpp"
#include "admin/admin_service.hpp"
#include "auth/auth_service.hpp"
#include "common/config.hpp"
#include "net/session_hub.hpp"
#include "pay/pay_service.hpp"
#include "social/social_service.hpp"
#include "store/memory_store.hpp"
#include "store/mysql_client.hpp"
#include "store/redis_client.hpp"

#include <filesystem>

#include <boost/asio/io_context.hpp>

namespace pandora {

class HttpApi {
 public:
  HttpApi(AppConfig cfg, MemoryStore& store, AuthService& auth, WalletService& wallet, PayService& pay,
          AdminService& admin, ActivityService& activity, SocialService& social, MysqlClient& mysql,
          RedisClient& redis, SessionHub& hub);
  void Start(boost::asio::io_context& ioc, const std::filesystem::path& conf_dir = {});

  const AppConfig& cfg() const { return cfg_; }
  MemoryStore& store() { return store_; }
  AuthService& auth() { return auth_; }
  WalletService& wallet() { return wallet_; }
  PayService& pay() { return pay_; }
  AdminService& admin() { return admin_; }
  ActivityService& activity() { return activity_; }
  SocialService& social() { return social_; }
  MysqlClient& mysql() { return mysql_; }
  RedisClient& redis() { return redis_; }
  SessionHub& hub() { return hub_; }

 private:
  AppConfig cfg_;
  MemoryStore& store_;
  AuthService& auth_;
  WalletService& wallet_;
  PayService& pay_;
  AdminService& admin_;
  ActivityService& activity_;
  SocialService& social_;
  MysqlClient& mysql_;
  RedisClient& redis_;
  SessionHub& hub_;
};

}  // namespace pandora
