#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>

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
  const auto conf_dir = std::filesystem::path(conf_path).parent_path();
  PLOG_INFO("pandora-server Beast stack starting, config=" << conf_path);

  MysqlClient mysql(cfg.mysql.dsn, cfg.mysql.pool_size);
  RedisClient redis(cfg.redis.uri, cfg.redis.pool_size);
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

  const int workers = (std::max)(1, cfg.net.iocp_workers);
  boost::asio::io_context ioc{workers};

  HttpApi http(cfg, store, auth, wallet, pay, admin, activity, mysql, redis, runtime.hub);
  WsServer ws(cfg, auth, runtime, &admin);
  http.Start(ioc, conf_dir);
  ws.Start(ioc, conf_dir);

  std::thread tick_thread([&]() {
    while (g_running) {
      runtime.Tick();
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    ioc.stop();
  });

  std::vector<std::thread> pool;
  pool.reserve(static_cast<size_t>(workers));
  for (int i = 0; i < workers; ++i) {
    pool.emplace_back([&ioc]() { ioc.run(); });
  }

  PLOG_INFO("ready: " << (cfg.net.tls_enabled ? "HTTPS" : "HTTP") << " :" << cfg.net.http_port << " "
                      << (cfg.net.tls_enabled ? "WSS" : "WS") << " :" << cfg.net.ws_port
                      << " asio_workers=" << workers << " tls=" << cfg.net.tls_enabled
                      << " force_tls=" << cfg.net.force_tls << " mysql=" << mysql.Available()
                      << " mysql_pool=" << mysql.PoolSize() << " redis=" << redis.Available()
                      << " redis_pool=" << redis.PoolSize());

  for (auto& t : pool) t.join();
  g_running = false;
  if (tick_thread.joinable()) tick_thread.join();
  PLOG_INFO("shutting down");
  return 0;
}
