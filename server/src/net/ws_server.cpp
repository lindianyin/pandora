#include "net/ws_server.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using socket_t = SOCKET;
#else
#error "ws server currently targets Windows"
#endif

#include "common/errors.hpp"
#include "common/log.hpp"
#include "common/proto_wire.hpp"
#include "net/frame.hpp"
#include "net/session_hub.hpp"

namespace pandora {

namespace {

struct Sha1 {
  uint32_t h0 = 0x67452301, h1 = 0xEFCDAB89, h2 = 0x98BADCFE, h3 = 0x10325476, h4 = 0xC3D2E1F0;
  uint64_t total = 0;
  std::vector<uint8_t> buf;
  static uint32_t Rol(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }
  void ProcessBlock(const uint8_t* p) {
    uint32_t w[80];
    for (int i = 0; i < 16; ++i)
      w[i] = (p[4 * i] << 24) | (p[4 * i + 1] << 16) | (p[4 * i + 2] << 8) | p[4 * i + 3];
    for (int i = 16; i < 80; ++i) w[i] = Rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
    for (int i = 0; i < 80; ++i) {
      uint32_t f, k;
      if (i < 20) {
        f = (b & c) | ((~b) & d);
        k = 0x5A827999;
      } else if (i < 40) {
        f = b ^ c ^ d;
        k = 0x6ED9EBA1;
      } else if (i < 60) {
        f = (b & c) | (b & d) | (c & d);
        k = 0x8F1BBCDC;
      } else {
        f = b ^ c ^ d;
        k = 0xCA62C1D6;
      }
      uint32_t t = Rol(a, 5) + f + e + k + w[i];
      e = d;
      d = c;
      c = Rol(b, 30);
      b = a;
      a = t;
    }
    h0 += a;
    h1 += b;
    h2 += c;
    h3 += d;
    h4 += e;
  }
  void Update(const uint8_t* data, size_t len) {
    total += len;
    buf.insert(buf.end(), data, data + len);
    while (buf.size() >= 64) {
      ProcessBlock(buf.data());
      buf.erase(buf.begin(), buf.begin() + 64);
    }
  }
  std::array<uint8_t, 20> Final() {
    uint64_t bitlen = total * 8;
    buf.push_back(0x80);
    while ((buf.size() % 64) != 56) buf.push_back(0);
    for (int i = 7; i >= 0; --i) buf.push_back(static_cast<uint8_t>((bitlen >> (i * 8)) & 0xFF));
    for (size_t i = 0; i < buf.size(); i += 64) ProcessBlock(buf.data() + i);
    std::array<uint8_t, 20> out{};
    uint32_t hs[5] = {h0, h1, h2, h3, h4};
    for (int i = 0; i < 5; ++i) {
      out[i * 4] = static_cast<uint8_t>((hs[i] >> 24) & 0xFF);
      out[i * 4 + 1] = static_cast<uint8_t>((hs[i] >> 16) & 0xFF);
      out[i * 4 + 2] = static_cast<uint8_t>((hs[i] >> 8) & 0xFF);
      out[i * 4 + 3] = static_cast<uint8_t>(hs[i] & 0xFF);
    }
    return out;
  }
};

std::string Base64Encode(const uint8_t* data, size_t len) {
  static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  for (size_t i = 0; i < len; i += 3) {
    int v = data[i] << 16;
    if (i + 1 < len) v |= data[i + 1] << 8;
    if (i + 2 < len) v |= data[i + 2];
    out.push_back(tbl[(v >> 18) & 63]);
    out.push_back(tbl[(v >> 12) & 63]);
    out.push_back(i + 1 < len ? tbl[(v >> 6) & 63] : '=');
    out.push_back(i + 2 < len ? tbl[v & 63] : '=');
  }
  return out;
}

std::string WsAcceptKey(const std::string& client_key) {
  const std::string magic = client_key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
  Sha1 sha;
  sha.Update(reinterpret_cast<const uint8_t*>(magic.data()), magic.size());
  auto dig = sha.Final();
  return Base64Encode(dig.data(), dig.size());
}

std::string HeaderValue(const std::string& req, const char* name) {
  auto pos = req.find(name);
  if (pos == std::string::npos) return {};
  pos = req.find(':', pos);
  if (pos == std::string::npos) return {};
  ++pos;
  while (pos < req.size() && (req[pos] == ' ' || req[pos] == '\t')) ++pos;
  auto end = req.find("\r\n", pos);
  return req.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
}

int64_t NowMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

bool SendAll(socket_t s, const uint8_t* data, size_t len) {
  size_t off = 0;
  while (off < len) {
    const int n = send(s, reinterpret_cast<const char*>(data + off), static_cast<int>(len - off), 0);
    if (n <= 0) return false;
    off += static_cast<size_t>(n);
  }
  return true;
}

bool SendWsBinary(socket_t s, const std::vector<uint8_t>& payload) {
  std::vector<uint8_t> frame;
  frame.push_back(0x82);
  if (payload.size() < 126) {
    frame.push_back(static_cast<uint8_t>(payload.size()));
  } else if (payload.size() <= 0xFFFF) {
    frame.push_back(126);
    frame.push_back(static_cast<uint8_t>((payload.size() >> 8) & 0xFF));
    frame.push_back(static_cast<uint8_t>(payload.size() & 0xFF));
  } else {
    frame.push_back(127);
    uint64_t n = payload.size();
    for (int i = 7; i >= 0; --i) frame.push_back(static_cast<uint8_t>((n >> (i * 8)) & 0xFF));
  }
  frame.insert(frame.end(), payload.begin(), payload.end());
  return SendAll(s, frame.data(), frame.size());
}

class WsConn : public ISessionConn {
 public:
  explicit WsConn(socket_t s) : sock_(s) {}
  void Send(uint32_t msg_id, const std::vector<uint8_t>& body) override {
    std::lock_guard<std::mutex> lk(mu_);
    if (sock_ == INVALID_SOCKET) return;
    SendWsBinary(sock_, EncodeFrame(msg_id, body));
  }
  void Close() {
    std::lock_guard<std::mutex> lk(mu_);
    if (sock_ != INVALID_SOCKET) {
      closesocket(sock_);
      sock_ = INVALID_SOCKET;
    }
  }
  socket_t Sock() {
    std::lock_guard<std::mutex> lk(mu_);
    return sock_;
  }

 private:
  std::mutex mu_;
  socket_t sock_;
};

void SessionLoop(socket_t client, AuthService& auth, GameRuntime& runtime, AdminService* admin, uint32_t max_frame,
                 int hb_timeout_s) {
  std::string req;
  char tmp[4096];
  while (req.find("\r\n\r\n") == std::string::npos) {
    const int n = recv(client, tmp, sizeof(tmp), 0);
    if (n <= 0) {
      closesocket(client);
      return;
    }
    req.append(tmp, tmp + n);
    if (req.size() > 65536) {
      closesocket(client);
      return;
    }
  }
  const auto key = HeaderValue(req, "Sec-WebSocket-Key");
  if (key.empty()) {
    closesocket(client);
    return;
  }
  const auto accept = WsAcceptKey(key);
  std::ostringstream oss;
  oss << "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
      << "Sec-WebSocket-Accept: " << accept << "\r\n\r\n";
  const auto resp = oss.str();
  if (!SendAll(client, reinterpret_cast<const uint8_t*>(resp.data()), resp.size())) {
    closesocket(client);
    return;
  }

  auto conn = std::make_shared<WsConn>(client);
  bool authed = false;
  int64_t uid = 0;
  auto last_hb = std::chrono::steady_clock::now();
  std::vector<uint8_t> ws_buf;
  std::vector<uint8_t> app_buf;

  DWORD timeout_ms = 1000;
  setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout_ms), sizeof(timeout_ms));

  while (true) {
    if (std::chrono::steady_clock::now() - last_hb > std::chrono::seconds(hb_timeout_s)) {
      conn->Send(MsgId::kS2C_Kick, proto_wire::EncodeS2C_Kick(1, "heartbeat timeout"));
      break;
    }
    char rbuf[4096];
    const int n = recv(client, rbuf, sizeof(rbuf), 0);
    if (n == 0) break;
    if (n < 0) {
      const int err = WSAGetLastError();
      if (err == WSAETIMEDOUT || err == WSAEWOULDBLOCK) continue;
      break;
    }
    ws_buf.insert(ws_buf.end(), rbuf, rbuf + n);

    while (true) {
      if (ws_buf.size() < 2) break;
      const uint8_t b0 = ws_buf[0];
      const uint8_t b1 = ws_buf[1];
      const uint8_t opcode = b0 & 0x0F;
      const bool masked = (b1 & 0x80) != 0;
      uint64_t payload_len = b1 & 0x7F;
      size_t header_len = 2;
      if (payload_len == 126) {
        if (ws_buf.size() < 4) break;
        payload_len = (static_cast<uint64_t>(ws_buf[2]) << 8) | ws_buf[3];
        header_len = 4;
      } else if (payload_len == 127) {
        if (ws_buf.size() < 10) break;
        payload_len = 0;
        for (int i = 0; i < 8; ++i) payload_len = (payload_len << 8) | ws_buf[2 + i];
        header_len = 10;
      }
      const size_t mask_len = masked ? 4u : 0u;
      const size_t total = header_len + mask_len + static_cast<size_t>(payload_len);
      if (ws_buf.size() < total) break;

      std::vector<uint8_t> payload(static_cast<size_t>(payload_len));
      const uint8_t* mask = masked ? ws_buf.data() + header_len : nullptr;
      const uint8_t* data = ws_buf.data() + header_len + mask_len;
      for (size_t i = 0; i < payload.size(); ++i)
        payload[i] = masked ? static_cast<uint8_t>(data[i] ^ mask[i % 4]) : data[i];
      ws_buf.erase(ws_buf.begin(), ws_buf.begin() + static_cast<std::ptrdiff_t>(total));

      if (opcode == 0x8) goto done;
      if (opcode == 0x9) {
        std::vector<uint8_t> pong;
        pong.push_back(0x8A);
        pong.push_back(static_cast<uint8_t>(payload.size()));
        pong.insert(pong.end(), payload.begin(), payload.end());
        SendAll(client, pong.data(), pong.size());
        continue;
      }
      if (opcode != 0x2 && opcode != 0x1) continue;

      app_buf.insert(app_buf.end(), payload.begin(), payload.end());
      while (auto frame = TryDecodeOneFrame(app_buf, max_frame)) {
        last_hb = std::chrono::steady_clock::now();
        if (!authed) {
          if (frame->msg_id != MsgId::kC2S_Auth) {
            conn->Send(MsgId::kS2C_Error,
                       proto_wire::EncodeS2C_Error(static_cast<int>(Err::kUnauthorized), "auth required",
                                                   frame->msg_id));
            goto done;
          }
          std::string token;
          if (!proto_wire::DecodeC2S_Auth(frame->body.data(), frame->body.size(), token)) {
            conn->Send(MsgId::kS2C_AuthResult,
                       proto_wire::EncodeS2C_AuthResult(static_cast<int>(Err::kBadParam), "bad auth", 0));
            goto done;
          }
          auto sess = auth.ValidateToken(token);
          if (!sess) {
            conn->Send(MsgId::kS2C_AuthResult,
                       proto_wire::EncodeS2C_AuthResult(static_cast<int>(Err::kUnauthorized), "invalid token", 0));
            goto done;
          }
          if (admin) {
            if (admin->IsMaintain()) {
              conn->Send(MsgId::kS2C_AuthResult,
                         proto_wire::EncodeS2C_AuthResult(static_cast<int>(Err::kMaintain), "maintain", 0));
              goto done;
            }
            if (admin->IsBanned(sess->uid)) {
              conn->Send(MsgId::kS2C_AuthResult,
                         proto_wire::EncodeS2C_AuthResult(static_cast<int>(Err::kBanned), "banned", 0));
              goto done;
            }
          }
          authed = true;
          uid = sess->uid;
          runtime.hub.Bind(uid, conn);
          conn->Send(MsgId::kS2C_AuthResult, proto_wire::EncodeS2C_AuthResult(0, "ok", uid));
          runtime.OnReconnect(uid);
          PLOG_INFO("ws auth ok uid=" << uid);
          continue;
        }
        if (frame->msg_id == MsgId::kC2S_Heartbeat) {
          int64_t ts = 0;
          proto_wire::DecodeC2S_Heartbeat(frame->body.data(), frame->body.size(), ts);
          (void)ts;
          conn->Send(MsgId::kS2C_HeartbeatAck, proto_wire::EncodeS2C_HeartbeatAck(NowMs()));
          continue;
        }
        runtime.Dispatch(uid, frame->msg_id, frame->body.data(), frame->body.size());
      }
    }
  }
done:
  if (authed) {
    runtime.hub.Unbind(uid, conn.get());
    runtime.OnDisconnect(uid);
  }
  conn->Close();
}

}  // namespace

WsServer::WsServer(AppConfig cfg, AuthService& auth, GameRuntime& runtime, AdminService* admin)
    : cfg_(std::move(cfg)), auth_(auth), runtime_(runtime), admin_(admin) {}

void WsServer::Run() {
  WSADATA wsa;
  WSAStartup(MAKEWORD(2, 2), &wsa);
  // SessionLoop is long-lived; WS IOCP pool sized for concurrent sessions (no per-accept detach).
  const int ws_workers = (std::max)(cfg_.net.iocp_workers, 256);
  if (!pool_.Start(ws_workers, [this](ULONG_PTR key, DWORD, OVERLAPPED*, bool) {
        const socket_t client = static_cast<socket_t>(key);
        if (cfg_.net.force_tls) {
          closesocket(client);
          return;
        }
        if (cfg_.net.max_connections > 0 &&
            runtime_.hub.OnlineCount() >= static_cast<size_t>(cfg_.net.max_connections)) {
          closesocket(client);
          return;
        }
        SessionLoop(client, auth_, runtime_, admin_, cfg_.net.max_frame_bytes, cfg_.net.heartbeat_timeout_s);
      })) {
    PLOG_ERROR("WS IOCP start failed");
    return;
  }
  socket_t listen_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<u_short>(cfg_.net.ws_port));
  inet_pton(AF_INET, cfg_.net.ws_host.c_str(), &addr.sin_addr);
  int yes = 1;
  setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));
  if (bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    PLOG_ERROR("WS bind failed port=" << cfg_.net.ws_port);
    return;
  }
  listen(listen_fd, SOMAXCONN);
  PLOG_INFO("WS IOCP listening on " << cfg_.net.ws_host << ":" << cfg_.net.ws_port
                                    << " workers=" << pool_.WorkerCount());
  while (true) {
    socket_t client = accept(listen_fd, nullptr, nullptr);
    if (client == INVALID_SOCKET) continue;
    if (!pool_.Post(static_cast<ULONG_PTR>(client))) {
      closesocket(client);
    }
  }
}

}  // namespace pandora

