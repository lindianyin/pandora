#include "common/config.hpp"

#include <fstream>

#include <nlohmann/json.hpp>

#include "common/log.hpp"

namespace pandora {

AppConfig LoadConfig(const std::string& path) {
  std::ifstream in(path);
  if (!in) {
    PLOG_WARN("config not found: " << path << ", using defaults");
    return {};
  }

  nlohmann::json root;
  try {
    in >> root;
  } catch (const std::exception& e) {
    PLOG_ERROR("config parse failed: " << e.what());
    return {};
  }

  AppConfig cfg;
  const auto net = root.value("net", nlohmann::json::object());
  cfg.net.http_host = net.value("http_host", cfg.net.http_host);
  cfg.net.http_port = net.value("http_port", cfg.net.http_port);
  cfg.net.ws_host = net.value("ws_host", cfg.net.ws_host);
  cfg.net.ws_port = net.value("ws_port", cfg.net.ws_port);
  cfg.net.max_frame_bytes = net.value("max_frame_bytes", cfg.net.max_frame_bytes);
  cfg.net.heartbeat_interval_s = net.value("heartbeat_interval_s", cfg.net.heartbeat_interval_s);
  cfg.net.heartbeat_timeout_s = net.value("heartbeat_timeout_s", cfg.net.heartbeat_timeout_s);
  cfg.net.force_tls = net.value("force_tls", cfg.net.force_tls);
  cfg.net.iocp_workers = net.value("iocp_workers", cfg.net.iocp_workers);
  cfg.net.max_connections = net.value("max_connections", cfg.net.max_connections);
  if (net.contains("tls") && net["tls"].is_object()) {
    const auto& tls = net["tls"];
    cfg.net.tls_enabled = tls.value("enabled", cfg.net.tls_enabled);
    cfg.net.tls_cert = tls.value("cert", cfg.net.tls_cert);
    cfg.net.tls_key = tls.value("key", cfg.net.tls_key);
  }

  const auto store = root.value("store", nlohmann::json::object());
  cfg.store_backend = store.value("backend", cfg.store_backend);

  const auto game = root.value("game", nlohmann::json::object());
  cfg.game.base_score = game.value("base_score", cfg.game.base_score);
  cfg.game.rake_bp = game.value("rake_bp", cfg.game.rake_bp);
  cfg.game.bid_timeout_s = game.value("bid_timeout_s", cfg.game.bid_timeout_s);
  cfg.game.play_timeout_s = game.value("play_timeout_s", cfg.game.play_timeout_s);
  cfg.game.match_timeout_s = game.value("match_timeout_s", cfg.game.match_timeout_s);

  const auto exchange = root.value("exchange", nlohmann::json::object());
  cfg.exchange.diamond_to_gold = exchange.value("diamond_to_gold", cfg.exchange.diamond_to_gold);

  const auto alipay = root.value("alipay", nlohmann::json::object());
  cfg.alipay.sandbox = alipay.value("sandbox", cfg.alipay.sandbox);
  cfg.alipay.app_id = alipay.value("app_id", cfg.alipay.app_id);
  cfg.alipay.notify_url = alipay.value("notify_url", cfg.alipay.notify_url);
  cfg.alipay.gateway = alipay.value("gateway", cfg.alipay.gateway);

  const auto mysql = root.value("mysql", nlohmann::json::object());
  cfg.mysql.dsn = mysql.value("dsn", cfg.mysql.dsn);
  cfg.mysql.pool_size = mysql.value("pool_size", cfg.mysql.pool_size);

  const auto redis = root.value("redis", nlohmann::json::object());
  cfg.redis.uri = redis.value("uri", cfg.redis.uri);
  cfg.redis.pool_size = redis.value("pool_size", cfg.redis.pool_size);

  const auto worker = root.value("worker", nlohmann::json::object());
  cfg.worker.biz_threads = worker.value("biz_threads", cfg.worker.biz_threads);
  cfg.worker.async_threads = worker.value("async_threads", cfg.worker.async_threads);

  if (cfg.store_backend == "mysql") {
    PLOG_WARN("store.backend=mysql selected; ledger uses MemoryStore over MySQL/Redis drivers");
    cfg.store_backend = "memory";
  }

  PLOG_INFO("deps config mysql.dsn=" << cfg.mysql.dsn << " redis.uri=" << cfg.redis.uri);
  return cfg;
}

}  // namespace pandora
