#include <atomic>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <string>
#include <thread>

#include "activity/activity_service.hpp"
#include "admin/admin_service.hpp"
#include "auth/auth_service.hpp"
#include "common/config.hpp"
#include "common/log.hpp"
#include "net/game_runtime.hpp"
#include "net/http_api.hpp"
#include "net/ws_server.hpp"
#include "pay/pay_service.hpp"
#include "store/memory_store.hpp"
#include "store/mysql_client.hpp"
#include "store/redis_client.hpp"
#include "wallet/wallet_service.hpp"

namespace {
std::atomic_bool g_running{true};
void OnSignal(int) { g_running = false; }
}  // namespace

int main(int argc, char** argv) {
  using namespace pandora;
  std::signal(SIGINT, OnSignal);
#ifdef _WIN32
  std::signal(SIGTERM, OnSignal);
#endif

  std::string conf_path = "conf/server.json";
  if (argc >= 2) conf_path = argv[1];
  if (!std::filesystem::exists(conf_path)) {
    const auto alt = std::filesystem::path(argv[0]).parent_path() / "conf" / "server.json";
    if (std::filesystem::exists(alt)) conf_path = alt.string();
  }

  auto cfg = LoadConfig(conf_path);
  PLOG_INFO("pandora-server M5 starting, config=" << conf_path);

  MysqlClient mysql(cfg.mysql.dsn);
  RedisClient redis(cfg.redis.uri);
  MemoryStore store(mysql, redis);
  AuthService auth(store);
  WalletService wallet(store, cfg.exchange.diamond_to_gold);
  AlipayConfig acfg;
  acfg.sandbox = cfg.alipay.sandbox;
  acfg.app_id = cfg.alipay.app_id;
  acfg.notify_url = cfg.alipay.notify_url;
  acfg.gateway = cfg.alipay.gateway;
  PayService pay(wallet, mysql, acfg);

  GameRuntime runtime(store, wallet, cfg.game);
  AdminService admin(mysql, redis, store, wallet, pay, runtime.lobby, runtime.hub);
  ActivityService activity(mysql, redis, wallet, runtime.hub);
  runtime.rooms.SetAdmin(&admin);
  runtime.rooms.SetActivity(&activity);
  admin.Bootstrap();
  activity.Bootstrap();

  HttpApi http(cfg, store, auth, wallet, pay, admin, activity, mysql, redis);
  WsServer ws(cfg, auth, runtime, &admin);

  std::thread http_thread([&]() { http.Run(); });
  std::thread ws_thread([&]() { ws.Run(); });
  std::thread tick_thread([&]() {
    while (g_running) {
      runtime.Tick();
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  });

  PLOG_INFO("M5 ready: HTTP :" << cfg.net.http_port << " WS :" << cfg.net.ws_port
                               << " mysql=" << mysql.Available() << " redis=" << redis.Available());
  while (g_running) std::this_thread::sleep_for(std::chrono::milliseconds(200));
  PLOG_INFO("shutting down");
  std::exit(0);
}

