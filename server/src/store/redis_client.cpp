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
  if (EnsureConnected()) {
    PLOG_INFO("redis pool ok " << host_ << ":" << port_ << "/" << db_ << " size=" << pool_size_);
  } else {
    PLOG_WARN("redis pool unavailable: " << g_redis_last_error);
  }
}

RedisClient::~RedisClient() {
  std::lock_guard<std::mutex> connect_lk(connect_mu_);
  std::lock_guard<std::mutex> lk(mu_);
  redis_.reset();
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
  // Single-flight: avoid concurrent full-pool construction.
  std::lock_guard<std::mutex> connect_lk(connect_mu_);
  {
    std::lock_guard<std::mutex> lk(mu_);
    if (redis_) return true;
  }

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
    return true;
  } catch (const sw::redis::Error& e) {
    SetError(e.what());
    std::lock_guard<std::mutex> lk(mu_);
    redis_.reset();
    return false;
  } catch (const std::exception& e) {
    SetError(e.what());
    std::lock_guard<std::mutex> lk(mu_);
    redis_.reset();
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

  // redis-plus-plus reconnects broken connections on the next pool fetch;
  // retry the command once on the same pool (do not rebuild the whole pool).
  for (int attempt = 0; attempt < 2; ++attempt) {
    try {
      fn(*r);
      g_redis_last_error.clear();
      return true;
    } catch (const std::exception& e) {
      SetError(e.what());
      if (attempt == 0) continue;
      return false;
    }
  }
  return false;
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

bool RedisClient::SetNx(const std::string& key, const std::string& value, int ttl_sec) {
  bool created = false;
  const bool ok = RunWithRetry([&](sw::redis::Redis& r) {
    if (ttl_sec > 0) {
      created = r.set(key, value, std::chrono::milliseconds(ttl_sec * 1000LL), sw::redis::UpdateType::NOT_EXIST);
    } else {
      created = r.set(key, value, false, sw::redis::UpdateType::NOT_EXIST);
    }
  });
  return ok && created;
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

bool RedisClient::Expire(const std::string& key, int ttl_sec) {
  if (ttl_sec <= 0) return true;
  return RunWithRetry([&](sw::redis::Redis& r) { r.expire(key, std::chrono::seconds(ttl_sec)); });
}

bool RedisClient::ZAdd(const std::string& key, const std::string& member, double score) {
  return RunWithRetry([&](sw::redis::Redis& r) { r.zadd(key, member, score); });
}

bool RedisClient::ZRevRangeWithScores(const std::string& key, long long start, long long stop,
                                      std::vector<std::pair<std::string, double>>* out) {
  if (!out) return false;
  out->clear();
  return RunWithRetry([&](sw::redis::Redis& r) {
    // Output pair<> triggers WITHSCORES in redis-plus-plus.
    r.zrevrange(key, start, stop, std::back_inserter(*out));
  });
}

std::optional<long long> RedisClient::ZRevRank(const std::string& key, const std::string& member) {
  std::optional<long long> out;
  const bool ok = RunWithRetry([&](sw::redis::Redis& r) {
    auto v = r.zrevrank(key, member);
    if (v) out = *v;
    else out = std::nullopt;
  });
  if (!ok) return std::nullopt;
  return out;
}

std::optional<double> RedisClient::ZScore(const std::string& key, const std::string& member) {
  std::optional<double> out;
  const bool ok = RunWithRetry([&](sw::redis::Redis& r) {
    auto v = r.zscore(key, member);
    if (v) out = *v;
    else out = std::nullopt;
  });
  if (!ok) return std::nullopt;
  return out;
}

}  // namespace pandora
