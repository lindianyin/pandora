#pragma once

#include "admin/admin_service.hpp"
#include "auth/auth_service.hpp"
#include "common/config.hpp"
#include "net/game_runtime.hpp"

#include <filesystem>

#include <boost/asio/io_context.hpp>

namespace pandora {

class WsServer {
 public:
  WsServer(AppConfig cfg, AuthService& auth, GameRuntime& runtime, AdminService* admin = nullptr);
  void Start(boost::asio::io_context& ioc, const std::filesystem::path& conf_dir = {});

 private:
  AppConfig cfg_;
  AuthService& auth_;
  GameRuntime& runtime_;
  AdminService* admin_{nullptr};
};

}  // namespace pandora
