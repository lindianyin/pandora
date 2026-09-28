#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>

#include "activity/activity_service.hpp"
#include "admin/admin_service.hpp"
#include "auth/auth_service.hpp"
#include "bag/bag_service.hpp"
#include "common/async_worker.hpp"
#include "common/config.hpp"
#include "common/log.hpp"
#include "net/game_runtime.hpp"
#include "net/http_api.hpp"
#include "net/ws_server.hpp"
#include "pay/pay_service.hpp"
#include "social/social_service.hpp"
#include "store/memory_store.hpp"
#include "store/mysql_client.hpp"
#include "store/redis_client.hpp"
#include "wallet/wallet_service.hpp"

namespace {
std::atomic_bool g_running{true};
std::mutex g_tick_mu;
std::condition_variable g_tick_cv;
}  // namespace

int main(int argc, char** argv) {
  using namespace pandora;

  std::string conf_path = "conf/server.json";
  if (argc >= 2) conf_path = argv[1];
  if (!std::filesystem::exists(conf_path)) {
    const auto alt = std::filesystem::path(argv[0]).parent_path() / "conf" / "server.json";
    if (std::filesystem::exists(alt)) conf_path = alt.string();
  }

  auto cfg = LoadConfig(conf_path);
  const auto conf_dir = std::filesystem::path(conf_path).parent_path();
  const auto log_file = (conf_dir / ".." / "logs" / "pandora.log").lexically_normal();
  InitLogging(log_file);
  PLOG_INFO("pandora-server Beast stack starting, config=" << conf_path);

  MysqlClient mysql(cfg.mysql.dsn, cfg.mysql.pool_size);
  RedisClient redis(cfg.redis.uri, cfg.redis.pool_size);

  AsyncWorker persist(cfg.worker.async_threads);
  persist.Start();

  MemoryStore store(mysql, redis, persist);
  AuthService auth(store);
  WalletService wallet(store, cfg.exchange.diamond_to_gold);
  GameRuntime runtime(store, wallet, cfg.game);
  BagService bag(mysql, redis, runtime.hub, persist, cfg.bag);
  AlipayConfig acfg;
  acfg.sandbox = cfg.alipay.sandbox;
  acfg.app_id = cfg.alipay.app_id;
  acfg.notify_url = cfg.alipay.notify_url;
  acfg.gateway = cfg.alipay.gateway;
  PayService pay(wallet, mysql, bag, acfg);
  AdminService admin(mysql, redis, store, wallet, pay, runtime.lobby, runtime.hub, persist);
  ActivityService activity(mysql, redis, wallet, bag, runtime.hub, persist);
  SocialService social(mysql, redis, wallet, bag, runtime.hub, persist, cfg.social);
  wallet.SetOnGoldChanged([&social](int64_t uid, int64_t gold) { social.OnGoldChanged(uid, gold); });
  runtime.rooms.SetAdmin(&admin);
  runtime.rooms.SetActivity(&activity);
  runtime.rooms.SetSocial(&social);
  admin.Bootstrap();
  bag.Bootstrap();
  activity.Bootstrap();
  social.Bootstrap();

  // Isolate HTTP (slow / admin / pay) from WSS (game hot path).
  const int ws_workers = (std::max)(1, cfg.net.iocp_workers);
  const int http_workers = (std::max)(2, cfg.worker.biz_threads);
  boost::asio::io_context http_ioc{http_workers};
  boost::asio::io_context ws_ioc{ws_workers};

  HttpApi http(cfg, store, auth, wallet, pay, admin, activity, social, bag, mysql, redis, runtime.hub);
  WsServer ws(cfg, auth, runtime, &admin);
  http.Start(http_ioc, conf_dir);
  ws.Start(ws_ioc, conf_dir);

  // Asio signal_set: one Ctrl+C stops both io_contexts immediately (no std::signal one-shot).
  boost::asio::signal_set signals(http_ioc, SIGINT, SIGTERM);
  signals.async_wait([&](const boost::system::error_code& ec, int) {
    if (ec) return;
    PLOG_INFO("signal received, stopping");
    g_running = false;
    g_tick_cv.notify_all();
    runtime.hub.CloseAll();
    http_ioc.stop();
    ws_ioc.stop();
  });

  std::thread tick_thread([&]() {
    while (g_running) {
      runtime.Tick();
      std::unique_lock<std::mutex> lk(g_tick_mu);
      g_tick_cv.wait_for(lk, std::chrono::milliseconds(100), [] { return !g_running.load(); });
    }
  });

  std::vector<std::thread> pool;
  pool.reserve(static_cast<size_t>(http_workers + ws_workers));
  for (int i = 0; i < http_workers; ++i) {
    pool.emplace_back([&http_ioc]() { http_ioc.run(); });
  }
  for (int i = 0; i < ws_workers; ++i) {
    pool.emplace_back([&ws_ioc]() { ws_ioc.run(); });
  }

  PLOG_INFO("ready: " << (cfg.net.tls_enabled ? "HTTPS" : "HTTP") << " :" << cfg.net.http_port << " "
                      << (cfg.net.tls_enabled ? "WSS" : "WS") << " :" << cfg.net.ws_port
                      << " http_workers=" << http_workers << " ws_workers=" << ws_workers
                      << " async_workers=" << cfg.worker.async_threads << " tls=" << cfg.net.tls_enabled
                      << " force_tls=" << cfg.net.force_tls << " mysql=" << mysql.Available()
                      << " mysql_pool=" << mysql.PoolSize() << " redis=" << redis.Available()
                      << " redis_pool=" << redis.PoolSize());

  for (auto& t : pool) t.join();
  g_running = false;
  g_tick_cv.notify_all();
  if (tick_thread.joinable()) tick_thread.join();
  persist.Stop();
  PLOG_INFO("shutting down");
  return 0;
}
