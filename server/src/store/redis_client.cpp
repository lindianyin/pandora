#include "store/redis_client.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#endif

#include <hiredis/hiredis.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <utility>

#include "common/log.hpp"

namespace pandora {

namespace {

thread_local std::string g_redis_last_error;

timeval MakeTv(int ms) {
  timeval tv{};
  tv.tv_sec = ms / 1000;
  tv.tv_usec = (ms % 1000) * 1000;
  return tv;
}

bool IsIoError(redisContext* ctx) {
  return !ctx || ctx->err != 0;
}

void FreeReply(redisReply* reply) {
  if (reply) freeReplyObject(reply);
}

}  // namespace

void RedisClient::Borrowed::Release() {
  if (owner) {
    owner->ReleaseIndex(index);
    owner = nullptr;
    ctx = nullptr;
  }
}

RedisClient::RedisClient(std::string uri, int pool_size) : uri_(std::move(uri)) {
  pool_size_ = pool_size < 1 ? 1 : pool_size;
  ParseUri();
  slots_.resize(static_cast<std::size_t>(pool_size_));

  int ok = 0;
  for (auto& slot : slots_) {
    slot.ctx = ConnectOne();
    if (slot.ctx) ++ok;
  }
  if (ok > 0) {
    PLOG_INFO("redis pool ok " << host_ << ":" << port_ << "/" << db_ << " size=" << pool_size_
                               << " live=" << ok);
  } else {
    PLOG_WARN("redis pool unavailable: " << g_redis_last_error);
  }
}

RedisClient::~RedisClient() {
  std::vector<redisContext*> to_close;
  {
    std::unique_lock<std::mutex> lk(mu_);
    stopping_ = true;
    cv_.notify_all();
    cv_.wait_for(lk, std::chrono::milliseconds(100), [this] { return borrowed_ == 0; });
    to_close.reserve(slots_.size());
    for (auto& slot : slots_) {
      if (slot.ctx) {
        to_close.push_back(slot.ctx);
        slot.ctx = nullptr;
      }
      slot.busy = false;
    }
    borrowed_ = 0;
  }
  for (auto* c : to_close) {
    redisFree(c);
  }
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

redisContext* RedisClient::ConnectOne() {
  const timeval ctv = MakeTv(2000);
  redisContext* ctx = redisConnectWithTimeout(host_.c_str(), port_, ctv);
  if (!ctx) {
    SetError("redisConnect failed: oom");
    return nullptr;
  }
  if (ctx->err) {
    SetError(ctx->errstr);
    redisFree(ctx);
    return nullptr;
  }
  const timeval stv = MakeTv(2000);
  if (redisSetTimeout(ctx, stv) != REDIS_OK) {
    SetError(ctx->errstr[0] ? ctx->errstr : "redisSetTimeout failed");
    redisFree(ctx);
    return nullptr;
  }
  if (db_ != 0) {
    redisReply* reply = static_cast<redisReply*>(redisCommand(ctx, "SELECT %d", db_));
    if (!reply || reply->type == REDIS_REPLY_ERROR || IsIoError(ctx)) {
      SetError(reply && reply->str ? reply->str : (ctx->errstr[0] ? ctx->errstr : "SELECT failed"));
      FreeReply(reply);
      redisFree(ctx);
      return nullptr;
    }
    FreeReply(reply);
  }
  g_redis_last_error.clear();
  return ctx;
}

void RedisClient::CloseOne(redisContext*& ctx) {
  if (!ctx) return;
  redisFree(ctx);
  ctx = nullptr;
}

bool RedisClient::ReconnectSlot(std::size_t index) {
  redisContext* old = nullptr;
  {
    std::lock_guard<std::mutex> lk(mu_);
    if (index >= slots_.size()) return false;
    old = slots_[index].ctx;
    slots_[index].ctx = nullptr;
  }
  CloseOne(old);
  redisContext* neu = ConnectOne();
  {
    std::lock_guard<std::mutex> lk(mu_);
    if (index >= slots_.size() || stopping_) {
      CloseOne(neu);
      return false;
    }
    slots_[index].ctx = neu;
  }
  return neu != nullptr;
}

std::optional<RedisClient::Borrowed> RedisClient::Acquire() {
  std::unique_lock<std::mutex> lk(mu_);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (!stopping_) {
    for (std::size_t i = 0; i < slots_.size(); ++i) {
      if (slots_[i].busy) continue;
      slots_[i].busy = true;
      ++borrowed_;
      redisContext* c = slots_[i].ctx;
      lk.unlock();

      if (!c) {
        if (!ReconnectSlot(i)) {
          ReleaseIndex(i);
          return std::nullopt;
        }
        std::lock_guard<std::mutex> lk2(mu_);
        c = slots_[i].ctx;
      }
      if (!c) {
        ReleaseIndex(i);
        return std::nullopt;
      }
      return Borrowed{this, i, c};
    }
    if (cv_.wait_until(lk, deadline) == std::cv_status::timeout) {
      SetError("redis pool acquire timeout");
      return std::nullopt;
    }
  }
  SetError("redis pool stopping");
  return std::nullopt;
}

void RedisClient::ReleaseIndex(std::size_t index) {
  std::lock_guard<std::mutex> lk(mu_);
  if (index >= slots_.size()) return;
  if (slots_[index].busy) {
    slots_[index].busy = false;
    --borrowed_;
    cv_.notify_one();
  }
}

bool RedisClient::WithConn(const std::function<bool(redisContext*)>& fn) {
  auto b = Acquire();
  if (!b) return false;

  if (fn(b->ctx)) {
    g_redis_last_error.clear();
    return true;
  }
  // IO / protocol failure: reconnect this slot and retry once.
  if (!IsIoError(b->ctx) && g_redis_last_error.empty()) {
    return false;
  }
  if (!ReconnectSlot(b->index)) return false;
  {
    std::lock_guard<std::mutex> lk(mu_);
    b->ctx = (b->index < slots_.size()) ? slots_[b->index].ctx : nullptr;
  }
  if (!b->ctx) return false;
  if (fn(b->ctx)) {
    g_redis_last_error.clear();
    return true;
  }
  return false;
}

bool RedisClient::Ping() {
  return WithConn([&](redisContext* ctx) {
    redisReply* reply = static_cast<redisReply*>(redisCommand(ctx, "PING"));
    if (!reply || IsIoError(ctx)) {
      SetError(ctx && ctx->errstr[0] ? ctx->errstr : "PING failed");
      FreeReply(reply);
      return false;
    }
    if (reply->type == REDIS_REPLY_ERROR) {
      SetError(reply->str ? reply->str : "PING error");
      FreeReply(reply);
      return false;
    }
    FreeReply(reply);
    return true;
  });
}

bool RedisClient::Set(const std::string& key, const std::string& value, int ttl_sec) {
  return WithConn([&](redisContext* ctx) {
    redisReply* reply = nullptr;
    if (ttl_sec > 0) {
      reply = static_cast<redisReply*>(
          redisCommand(ctx, "SET %b %b EX %d", key.data(), static_cast<size_t>(key.size()), value.data(),
                       static_cast<size_t>(value.size()), ttl_sec));
    } else {
      reply = static_cast<redisReply*>(redisCommand(ctx, "SET %b %b", key.data(), static_cast<size_t>(key.size()),
                                                    value.data(), static_cast<size_t>(value.size())));
    }
    if (!reply || IsIoError(ctx)) {
      SetError(ctx && ctx->errstr[0] ? ctx->errstr : "SET failed");
      FreeReply(reply);
      return false;
    }
    if (reply->type == REDIS_REPLY_ERROR) {
      SetError(reply->str ? reply->str : "SET error");
      FreeReply(reply);
      return false;
    }
    FreeReply(reply);
    return true;
  });
}

bool RedisClient::SetNx(const std::string& key, const std::string& value, int ttl_sec) {
  bool created = false;
  const bool ok = WithConn([&](redisContext* ctx) {
    redisReply* reply = nullptr;
    if (ttl_sec > 0) {
      reply = static_cast<redisReply*>(
          redisCommand(ctx, "SET %b %b NX EX %d", key.data(), static_cast<size_t>(key.size()), value.data(),
                       static_cast<size_t>(value.size()), ttl_sec));
    } else {
      reply = static_cast<redisReply*>(redisCommand(ctx, "SET %b %b NX", key.data(), static_cast<size_t>(key.size()),
                                                    value.data(), static_cast<size_t>(value.size())));
    }
    if (!reply || IsIoError(ctx)) {
      SetError(ctx && ctx->errstr[0] ? ctx->errstr : "SET NX failed");
      FreeReply(reply);
      return false;
    }
    if (reply->type == REDIS_REPLY_ERROR) {
      SetError(reply->str ? reply->str : "SET NX error");
      FreeReply(reply);
      return false;
    }
    created = (reply->type != REDIS_REPLY_NIL);
    FreeReply(reply);
    return true;
  });
  return ok && created;
}

std::optional<std::string> RedisClient::Get(const std::string& key) {
  std::optional<std::string> out;
  const bool ok = WithConn([&](redisContext* ctx) {
    redisReply* reply = static_cast<redisReply*>(
        redisCommand(ctx, "GET %b", key.data(), static_cast<size_t>(key.size())));
    if (!reply || IsIoError(ctx)) {
      SetError(ctx && ctx->errstr[0] ? ctx->errstr : "GET failed");
      FreeReply(reply);
      return false;
    }
    if (reply->type == REDIS_REPLY_ERROR) {
      SetError(reply->str ? reply->str : "GET error");
      FreeReply(reply);
      return false;
    }
    if (reply->type == REDIS_REPLY_NIL) {
      out = std::nullopt;
    } else if (reply->type == REDIS_REPLY_STRING && reply->str) {
      out = std::string(reply->str, reply->len);
    } else {
      SetError("GET unexpected reply");
      FreeReply(reply);
      return false;
    }
    FreeReply(reply);
    return true;
  });
  if (!ok) return std::nullopt;
  return out;
}

bool RedisClient::Del(const std::string& key) {
  return WithConn([&](redisContext* ctx) {
    redisReply* reply = static_cast<redisReply*>(
        redisCommand(ctx, "DEL %b", key.data(), static_cast<size_t>(key.size())));
    if (!reply || IsIoError(ctx)) {
      SetError(ctx && ctx->errstr[0] ? ctx->errstr : "DEL failed");
      FreeReply(reply);
      return false;
    }
    if (reply->type == REDIS_REPLY_ERROR) {
      SetError(reply->str ? reply->str : "DEL error");
      FreeReply(reply);
      return false;
    }
    FreeReply(reply);
    return true;
  });
}

std::optional<int64_t> RedisClient::Decr(const std::string& key) {
  std::optional<int64_t> out;
  const bool ok = WithConn([&](redisContext* ctx) {
    redisReply* reply = static_cast<redisReply*>(
        redisCommand(ctx, "DECR %b", key.data(), static_cast<size_t>(key.size())));
    if (!reply || IsIoError(ctx)) {
      SetError(ctx && ctx->errstr[0] ? ctx->errstr : "DECR failed");
      FreeReply(reply);
      return false;
    }
    if (reply->type == REDIS_REPLY_ERROR) {
      SetError(reply->str ? reply->str : "DECR error");
      FreeReply(reply);
      return false;
    }
    if (reply->type != REDIS_REPLY_INTEGER) {
      SetError("DECR unexpected reply");
      FreeReply(reply);
      return false;
    }
    out = reply->integer;
    FreeReply(reply);
    return true;
  });
  if (!ok) return std::nullopt;
  return out;
}

bool RedisClient::Expire(const std::string& key, int ttl_sec) {
  if (ttl_sec <= 0) return true;
  return WithConn([&](redisContext* ctx) {
    redisReply* reply = static_cast<redisReply*>(
        redisCommand(ctx, "EXPIRE %b %d", key.data(), static_cast<size_t>(key.size()), ttl_sec));
    if (!reply || IsIoError(ctx)) {
      SetError(ctx && ctx->errstr[0] ? ctx->errstr : "EXPIRE failed");
      FreeReply(reply);
      return false;
    }
    if (reply->type == REDIS_REPLY_ERROR) {
      SetError(reply->str ? reply->str : "EXPIRE error");
      FreeReply(reply);
      return false;
    }
    FreeReply(reply);
    return true;
  });
}

bool RedisClient::ZAdd(const std::string& key, const std::string& member, double score) {
  return WithConn([&](redisContext* ctx) {
    redisReply* reply = static_cast<redisReply*>(redisCommand(
        ctx, "ZADD %b %f %b", key.data(), static_cast<size_t>(key.size()), score, member.data(),
        static_cast<size_t>(member.size())));
    if (!reply || IsIoError(ctx)) {
      SetError(ctx && ctx->errstr[0] ? ctx->errstr : "ZADD failed");
      FreeReply(reply);
      return false;
    }
    if (reply->type == REDIS_REPLY_ERROR) {
      SetError(reply->str ? reply->str : "ZADD error");
      FreeReply(reply);
      return false;
    }
    FreeReply(reply);
    return true;
  });
}

bool RedisClient::ZRevRangeWithScores(const std::string& key, long long start, long long stop,
                                      std::vector<std::pair<std::string, double>>* out) {
  if (!out) return false;
  out->clear();
  return WithConn([&](redisContext* ctx) {
    redisReply* reply = static_cast<redisReply*>(redisCommand(
        ctx, "ZREVRANGE %b %lld %lld WITHSCORES", key.data(), static_cast<size_t>(key.size()), start, stop));
    if (!reply || IsIoError(ctx)) {
      SetError(ctx && ctx->errstr[0] ? ctx->errstr : "ZREVRANGE failed");
      FreeReply(reply);
      return false;
    }
    if (reply->type == REDIS_REPLY_ERROR) {
      SetError(reply->str ? reply->str : "ZREVRANGE error");
      FreeReply(reply);
      return false;
    }
    if (reply->type != REDIS_REPLY_ARRAY) {
      SetError("ZREVRANGE unexpected reply");
      FreeReply(reply);
      return false;
    }
    for (size_t i = 0; i + 1 < reply->elements; i += 2) {
      redisReply* m = reply->element[i];
      redisReply* s = reply->element[i + 1];
      if (!m || !s || m->type != REDIS_REPLY_STRING || !m->str) continue;
      double score = 0;
      if (s->type == REDIS_REPLY_STRING && s->str) {
        score = std::strtod(s->str, nullptr);
      }
      out->emplace_back(std::string(m->str, m->len), score);
    }
    FreeReply(reply);
    return true;
  });
}

std::optional<long long> RedisClient::ZRevRank(const std::string& key, const std::string& member) {
  std::optional<long long> out;
  const bool ok = WithConn([&](redisContext* ctx) {
    redisReply* reply = static_cast<redisReply*>(
        redisCommand(ctx, "ZREVRANK %b %b", key.data(), static_cast<size_t>(key.size()), member.data(),
                     static_cast<size_t>(member.size())));
    if (!reply || IsIoError(ctx)) {
      SetError(ctx && ctx->errstr[0] ? ctx->errstr : "ZREVRANK failed");
      FreeReply(reply);
      return false;
    }
    if (reply->type == REDIS_REPLY_ERROR) {
      SetError(reply->str ? reply->str : "ZREVRANK error");
      FreeReply(reply);
      return false;
    }
    if (reply->type == REDIS_REPLY_NIL) {
      out = std::nullopt;
    } else if (reply->type == REDIS_REPLY_INTEGER) {
      out = reply->integer;
    } else {
      SetError("ZREVRANK unexpected reply");
      FreeReply(reply);
      return false;
    }
    FreeReply(reply);
    return true;
  });
  if (!ok) return std::nullopt;
  return out;
}

std::optional<double> RedisClient::ZScore(const std::string& key, const std::string& member) {
  std::optional<double> out;
  const bool ok = WithConn([&](redisContext* ctx) {
    redisReply* reply = static_cast<redisReply*>(
        redisCommand(ctx, "ZSCORE %b %b", key.data(), static_cast<size_t>(key.size()), member.data(),
                     static_cast<size_t>(member.size())));
    if (!reply || IsIoError(ctx)) {
      SetError(ctx && ctx->errstr[0] ? ctx->errstr : "ZSCORE failed");
      FreeReply(reply);
      return false;
    }
    if (reply->type == REDIS_REPLY_ERROR) {
      SetError(reply->str ? reply->str : "ZSCORE error");
      FreeReply(reply);
      return false;
    }
    if (reply->type == REDIS_REPLY_NIL) {
      out = std::nullopt;
    } else if (reply->type == REDIS_REPLY_STRING && reply->str) {
      out = std::strtod(reply->str, nullptr);
    } else {
      SetError("ZSCORE unexpected reply");
      FreeReply(reply);
      return false;
    }
    FreeReply(reply);
    return true;
  });
  if (!ok) return std::nullopt;
  return out;
}

}  // namespace pandora
