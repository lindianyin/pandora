#include "store/redis_client.hpp"

#include <sw/redis++/redis++.h>

#include <chrono>
#include <utility>

#include "common/log.hpp"

namespace pandora {

namespace {

thread_local std::string g_redis_last_error;

}  // namespace

RedisClient::RedisClient(std::string uri, int pool_size) : uri_(std::move(uri)) {
  pool_size_ = pool_size < 1 ? 1 : pool_size;
  ParseUri();
  available_.store(EnsureConnected(), std::memory_order_relaxed);
  if (available_.load(std::memory_order_relaxed)) {
    PLOG_INFO("redis pool ok " << host_ << ":" << port_ << "/" << db_ << " size=" << pool_size_);
  } else {
    PLOG_WARN("redis pool unavailable: " << g_redis_last_error);
  }
}

RedisClient::~RedisClient() {
  std::lock_guard<std::mutex> lk(mu_);
  redis_.reset();
  available_.store(false, std::memory_order_relaxed);
}

void RedisClient::ParseUri() {
  // redis://host:port/db
  auto s = uri_;
  if (s.rfind("redis://", 0) == 0) s = s.substr(8);
  auto slash = s.find('/');
  std::string hostport = slash == std::string::npos ? s : s.substr(0, slash);
  if (slash != std::string::npos) {
    try {
      db_ = std::stoi(s.substr(slash + 1));
    } catch (...) {
    }
  }
  auto colon = hostport.find(':');
  if (colon == std::string::npos) {
    host_ = hostport.empty() ? host_ : hostport;
  } else {
    host_ = hostport.substr(0, colon);
    try {
      port_ = std::stoi(hostport.substr(colon + 1));
    } catch (...) {
    }
  }
}

void RedisClient::SetError(std::string err) const { g_redis_last_error = std::move(err); }

std::string RedisClient::LastError() const { return g_redis_last_error; }

std::shared_ptr<sw::redis::Redis> RedisClient::GetRedis() {
  std::lock_guard<std::mutex> lk(mu_);
  return redis_;
}

bool RedisClient::EnsureConnected() {
  try {
    sw::redis::ConnectionOptions opts;
    opts.host = host_;
    opts.port = static_cast<int>(port_);
    opts.db = db_;
    opts.connect_timeout = std::chrono::milliseconds(2000);
    opts.socket_timeout = std::chrono::milliseconds(2000);

    sw::redis::ConnectionPoolOptions pool;
    pool.size = static_cast<std::size_t>(pool_size_);
    pool.wait_timeout = std::chrono::milliseconds(3000);
    pool.connection_lifetime = std::chrono::minutes(30);

    auto neu = std::make_shared<sw::redis::Redis>(opts, pool);
    neu->ping();

    {
      std::lock_guard<std::mutex> lk(mu_);
      redis_ = std::move(neu);
    }
    g_redis_last_error.clear();
    available_.store(true, std::memory_order_relaxed);
    return true;
  } catch (const sw::redis::Error& e) {
    SetError(e.what());
    std::lock_guard<std::mutex> lk(mu_);
    redis_.reset();
    available_.store(false, std::memory_order_relaxed);
    return false;
  } catch (const std::exception& e) {
    SetError(e.what());
    std::lock_guard<std::mutex> lk(mu_);
    redis_.reset();
    available_.store(false, std::memory_order_relaxed);
    return false;
  }
}

bool RedisClient::RunWithRetry(const std::function<void(sw::redis::Redis&)>& fn) {
  auto r = GetRedis();
  if (!r) {
    if (!EnsureConnected()) return false;
    r = GetRedis();
    if (!r) return false;
  }
  try {
    fn(*r);
    g_redis_last_error.clear();
    available_.store(true, std::memory_order_relaxed);
    return true;
  } catch (const std::exception& e) {
    SetError(e.what());
  }
  // auto-reconnect pool + one retry
  if (!EnsureConnected()) return false;
  r = GetRedis();
  if (!r) return false;
  try {
    fn(*r);
    g_redis_last_error.clear();
    available_.store(true, std::memory_order_relaxed);
    return true;
  } catch (const std::exception& e) {
    SetError(e.what());
    available_.store(false, std::memory_order_relaxed);
    return false;
  }
}

bool RedisClient::Ping() {
  return RunWithRetry([](sw::redis::Redis& r) { r.ping(); });
}

bool RedisClient::Set(const std::string& key, const std::string& value, int ttl_sec) {
  return RunWithRetry([&](sw::redis::Redis& r) {
    if (ttl_sec > 0) {
      r.set(key, value, std::chrono::seconds(ttl_sec));
    } else {
      r.set(key, value);
    }
  });
}

std::optional<std::string> RedisClient::Get(const std::string& key) {
  std::optional<std::string> out;
  const bool ok = RunWithRetry([&](sw::redis::Redis& r) {
    auto v = r.get(key);
    if (v) out = *v;
    else out = std::nullopt;
  });
  if (!ok) return std::nullopt;
  return out;
}

bool RedisClient::Del(const std::string& key) {
  return RunWithRetry([&](sw::redis::Redis& r) { r.del(key); });
}

std::optional<int64_t> RedisClient::Decr(const std::string& key) {
  std::optional<int64_t> out;
  const bool ok = RunWithRetry([&](sw::redis::Redis& r) { out = r.decr(key); });
  if (!ok) return std::nullopt;
  return out;
}

}  // namespace pandora
