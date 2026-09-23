#pragma once
#include "admin/admin_service.hpp"
#include "auth/auth_service.hpp"
#include "common/config.hpp"
#include "net/game_runtime.hpp"
namespace pandora {
class WsServer {
 public:
  WsServer(AppConfig cfg, AuthService& auth, GameRuntime& runtime, AdminService* admin = nullptr);
  void Run();
 private:
  AppConfig cfg_;
  AuthService& auth_;
  GameRuntime& runtime_;
  AdminService* admin_{nullptr};
};
}  // namespace pandora
