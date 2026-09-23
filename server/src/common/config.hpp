#pragma once

#include <cstdint>
#include <string>

namespace pandora {

struct NetConfig {
  std::string http_host{"0.0.0.0"};
  int http_port{8080};
  std::string ws_host{"0.0.0.0"};
  int ws_port{8081};
  bool tls_enabled{false};
  bool force_tls{false};
  std::string tls_cert;
  std::string tls_key;
  uint32_t max_frame_bytes{1048576};
  int heartbeat_interval_s{15};
  int heartbeat_timeout_s{45};
  int iocp_workers{8};
  int max_connections{30000};
};

struct GameConfig {
  int base_score{100};
  int rake_bp{500};
  int bid_timeout_s{15};
  int play_timeout_s{20};
  int match_timeout_s{30};
  int64_t min_gold{0};
  int64_t max_gold{100000000};
};

struct ExchangeConfig {
  int64_t diamond_to_gold{1000};
};

struct AlipayAppConfig {
  bool sandbox{true};
  std::string app_id{"REPLACE"};
  std::string merchant_private_key{"REPLACE"};
  std::string alipay_public_key{"REPLACE"};
  std::string gateway{"https://openapi.alipay.com/gateway.do"};
  std::string notify_url{"http://127.0.0.1:8080/api/v1/pay/alipay/notify"};
};

struct MysqlConfig {
  // host=127.0.0.1;port=3306;user=pandora;password=pandora;database=pandora
  std::string dsn{"host=127.0.0.1;port=3306;user=pandora;password=pandora;database=pandora"};
  int pool_size{10};
};

struct RedisConfig {
  std::string uri{"redis://127.0.0.1:6379/0"};
  int pool_size{10};
};

struct AppConfig {
  NetConfig net;
  GameConfig game;
  ExchangeConfig exchange;
  AlipayAppConfig alipay;
  MysqlConfig mysql;
  RedisConfig redis;
  std::string store_backend{"memory"};  // memory | mysql (mysql driver optional; falls back)
};

AppConfig LoadConfig(const std::string& path);

}  // namespace pandora
