#include "common/config.hpp"

#include <fstream>
#include <regex>
#include <sstream>

#include "common/log.hpp"

namespace pandora {

namespace {

std::string ReadFile(const std::string& path) {
  std::ifstream in(path);
  if (!in) return {};
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

int ExtractInt(const std::string& s, const std::string& key, int def) {
  std::regex re("\"" + key + "\"\\s*:\\s*(-?\\d+)");
  std::smatch m;
  if (std::regex_search(s, m, re)) return std::stoi(m[1].str());
  return def;
}

std::string ExtractString(const std::string& s, const std::string& key, const std::string& def) {
  std::regex re("\"" + key + "\"\\s*:\\s*\"([^\"]*)\"");
  std::smatch m;
  if (std::regex_search(s, m, re)) return m[1].str();
  return def;
}

bool ExtractBool(const std::string& s, const std::string& key, bool def) {
  std::regex re("\"" + key + "\"\\s*:\\s*(true|false)");
  std::smatch m;
  if (std::regex_search(s, m, re)) return m[1].str() == "true";
  return def;
}

}  // namespace

AppConfig LoadConfig(const std::string& path) {
  const std::string text = ReadFile(path);
  if (text.empty()) {
    PLOG_WARN("config not found: " << path << ", using defaults");
    return {};
  }

  AppConfig cfg;
  cfg.net.http_host = ExtractString(text, "http_host", cfg.net.http_host);
  cfg.net.http_port = ExtractInt(text, "http_port", cfg.net.http_port);
  cfg.net.ws_host = ExtractString(text, "ws_host", cfg.net.ws_host);
  cfg.net.ws_port = ExtractInt(text, "ws_port", cfg.net.ws_port);
  cfg.net.max_frame_bytes =
      static_cast<uint32_t>(ExtractInt(text, "max_frame_bytes", static_cast<int>(cfg.net.max_frame_bytes)));
  cfg.net.heartbeat_interval_s = ExtractInt(text, "heartbeat_interval_s", cfg.net.heartbeat_interval_s);
  cfg.net.heartbeat_timeout_s = ExtractInt(text, "heartbeat_timeout_s", cfg.net.heartbeat_timeout_s);
  cfg.net.tls_enabled = ExtractBool(text, "enabled", false);
  cfg.net.force_tls = ExtractBool(text, "force_tls", false);
  cfg.net.tls_cert = ExtractString(text, "cert", "");
  cfg.net.tls_key = ExtractString(text, "key", "");
  cfg.net.iocp_workers = ExtractInt(text, "iocp_workers", cfg.net.iocp_workers);
  cfg.net.max_connections = ExtractInt(text, "max_connections", cfg.net.max_connections);

  cfg.store_backend = ExtractString(text, "backend", cfg.store_backend);
  cfg.game.base_score = ExtractInt(text, "base_score", cfg.game.base_score);
  cfg.game.rake_bp = ExtractInt(text, "rake_bp", cfg.game.rake_bp);
  cfg.game.bid_timeout_s = ExtractInt(text, "bid_timeout_s", cfg.game.bid_timeout_s);
  cfg.game.play_timeout_s = ExtractInt(text, "play_timeout_s", cfg.game.play_timeout_s);
  cfg.game.match_timeout_s = ExtractInt(text, "match_timeout_s", cfg.game.match_timeout_s);
  cfg.exchange.diamond_to_gold = ExtractInt(text, "diamond_to_gold", static_cast<int>(cfg.exchange.diamond_to_gold));

  cfg.alipay.sandbox = ExtractBool(text, "sandbox", cfg.alipay.sandbox);
  cfg.alipay.app_id = ExtractString(text, "app_id", cfg.alipay.app_id);
  cfg.alipay.notify_url = ExtractString(text, "notify_url", cfg.alipay.notify_url);
  cfg.alipay.gateway = ExtractString(text, "gateway", cfg.alipay.gateway);

  cfg.mysql.dsn = ExtractString(text, "dsn", cfg.mysql.dsn);
  cfg.mysql.pool_size = ExtractInt(text, "pool_size", cfg.mysql.pool_size);
  cfg.redis.uri = ExtractString(text, "uri", cfg.redis.uri);
  // second pool_size in file may overwrite; prefer redis-specific default if missing after mysql
  if (cfg.redis.uri.find("redis://") == 0) {
    // keep pool_size from last match; re-read with defaults if needed
  }
  cfg.redis.pool_size = ExtractInt(text, "pool_size", cfg.redis.pool_size);

  if (cfg.store_backend == "mysql") {
    PLOG_WARN("store.backend=mysql selected; ledger still uses memory until MySQL driver is linked. "
              "DSN kept for Docker MySQL: "
              << cfg.mysql.dsn);
    cfg.store_backend = "memory";
  }

  PLOG_INFO("deps config mysql.dsn=" << cfg.mysql.dsn << " redis.uri=" << cfg.redis.uri);
  return cfg;
}

}  // namespace pandora
