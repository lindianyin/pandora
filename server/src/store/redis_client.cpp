#include "store/redis_client.hpp"

#include <mutex>
#include <sstream>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using socket_t = SOCKET;
static constexpr socket_t kInvalid = INVALID_SOCKET;
#else
#error "RedisClient targets Windows"
#endif

#include "common/log.hpp"

namespace pandora {
namespace {

void EnsureWsa() {
  static std::once_flag once;
  std::call_once(once, []() {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
  });
}

socket_t TcpConnect(const std::string& host, int port) {
  EnsureWsa();
  socket_t s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == kInvalid) return kInvalid;
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<u_short>(port));
  if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
    closesocket(s);
    return kInvalid;
  }
  if (connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    closesocket(s);
    return kInvalid;
  }
  DWORD timeout = 2000;
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
  setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
  return s;
}

}  // namespace

RedisClient::RedisClient(std::string uri) : uri_(std::move(uri)) {
  // redis://host:port/db
  auto p = uri_.find("://");
  std::string rest = p == std::string::npos ? uri_ : uri_.substr(p + 3);
  auto slash = rest.find('/');
  std::string hp = slash == std::string::npos ? rest : rest.substr(0, slash);
  if (slash != std::string::npos) {
    try {
      db_ = std::stoi(rest.substr(slash + 1));
    } catch (...) {
    }
  }
  auto colon = hp.rfind(':');
  if (colon != std::string::npos) {
    host_ = hp.substr(0, colon);
    try {
      port_ = std::stoi(hp.substr(colon + 1));
    } catch (...) {
    }
  } else if (!hp.empty()) {
    host_ = hp;
  }
  available_ = Ping();
  if (available_) PLOG_INFO("redis ok " << host_ << ":" << port_);
  else PLOG_WARN("redis unavailable: " << last_error_);
}

bool RedisClient::EnsureConnected() {
  if (sock_ != static_cast<uintptr_t>(-1) && sock_ != kInvalid) return true;
  socket_t s = TcpConnect(host_, port_);
  if (s == kInvalid) {
    last_error_ = "connect failed";
    available_ = false;
    return false;
  }
  sock_ = static_cast<uintptr_t>(s);
  if (db_ != 0) {
    auto r = Command({"SELECT", std::to_string(db_)});
    if (!r) {
      Disconnect();
      return false;
    }
  }
  return true;
}

void RedisClient::Disconnect() {
  if (sock_ != static_cast<uintptr_t>(-1) && sock_ != kInvalid) {
    closesocket(static_cast<socket_t>(sock_));
  }
  sock_ = static_cast<uintptr_t>(-1);
}

std::optional<std::string> RedisClient::Command(const std::vector<std::string>& args) {
  if (!EnsureConnected()) return std::nullopt;
  std::ostringstream oss;
  oss << "*" << args.size() << "\r\n";
  for (const auto& a : args) {
    oss << "$" << a.size() << "\r\n" << a << "\r\n";
  }
  const std::string req = oss.str();
  socket_t s = static_cast<socket_t>(sock_);
  if (send(s, req.c_str(), static_cast<int>(req.size()), 0) <= 0) {
    last_error_ = "send failed";
    Disconnect();
    available_ = false;
    return std::nullopt;
  }
  char buf[4096];
  const int n = recv(s, buf, sizeof(buf) - 1, 0);
  if (n <= 0) {
    last_error_ = "recv failed";
    Disconnect();
    available_ = false;
    return std::nullopt;
  }
  buf[n] = 0;
  std::string resp(buf, n);
  if (resp[0] == '-') {
    last_error_ = resp;
    return std::nullopt;
  }
  if (resp[0] == '+') {
    auto end = resp.find("\r\n");
    return resp.substr(1, end == std::string::npos ? std::string::npos : end - 1);
  }
  if (resp[0] == '$') {
    if (resp.size() >= 2 && resp[1] == '-' ) return std::string();  // null -> empty
    auto nl = resp.find("\r\n");
    if (nl == std::string::npos) return std::nullopt;
    int len = 0;
    try {
      len = std::stoi(resp.substr(1, nl - 1));
    } catch (...) {
      return std::nullopt;
    }
    if (len < 0) return std::string();
    return resp.substr(nl + 2, static_cast<size_t>(len));
  }
  if (resp[0] == ':') {
    auto end = resp.find("\r\n");
    return resp.substr(1, end == std::string::npos ? std::string::npos : end - 1);
  }
  return resp;
}

bool RedisClient::Ping() {
  std::lock_guard<std::mutex> lk(mu_);
  Disconnect();
  auto r = Command({"PING"});
  available_ = r && (*r == "PONG" || r->find("PONG") != std::string::npos);
  return available_;
}

bool RedisClient::Set(const std::string& key, const std::string& value, int ttl_sec) {
  std::lock_guard<std::mutex> lk(mu_);
  if (ttl_sec > 0) {
    auto r = Command({"SET", key, value, "EX", std::to_string(ttl_sec)});
    return r.has_value();
  }
  auto r = Command({"SET", key, value});
  return r.has_value();
}

std::optional<std::string> RedisClient::Get(const std::string& key) {
  std::lock_guard<std::mutex> lk(mu_);
  auto r = Command({"GET", key});
  if (!r) return std::nullopt;
  if (r->empty() && last_error_.find("$-1") != std::string::npos) return std::nullopt;
  return r;
}

bool RedisClient::Del(const std::string& key) {
  std::lock_guard<std::mutex> lk(mu_);
  auto r = Command({"DEL", key});
  return r.has_value();
}

std::optional<int64_t> RedisClient::Decr(const std::string& key) {
  std::lock_guard<std::mutex> lk(mu_);
  auto r = Command({"DECR", key});
  if (!r || r->empty()) return std::nullopt;
  try {
    return std::stoll(*r);
  } catch (...) {
    return std::nullopt;
  }
}

}  // namespace pandora

