#include "store/mysql_client.hpp"

#include <cstring>
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
#error "MysqlClient targets Windows"
#endif

#include "common/log.hpp"
#include "common/sha1.hpp"

namespace pandora {
namespace {

void EnsureWsa() {
  static std::once_flag once;
  std::call_once(once, []() {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
  });
}

bool Sha1(const uint8_t* data, size_t len, uint8_t out[20]) {
  crypto::Sha1(data, len, out);
  return true;
}

void MysqlNativePassword(const std::string& password, const uint8_t* scramble, size_t scramble_len,
                         uint8_t out[20]) {
  crypto::MysqlNativePassword(password, scramble, scramble_len, out);
}

std::string DsnValue(const std::string& dsn, const std::string& key, const std::string& def) {
  const std::string pat = key + "=";
  auto p = dsn.find(pat);
  if (p == std::string::npos) return def;
  p += pat.size();
  auto e = dsn.find(';', p);
  return dsn.substr(p, e == std::string::npos ? std::string::npos : e - p);
}

}  // namespace

MysqlClient::MysqlClient(std::string dsn) : dsn_(std::move(dsn)) {
  host_ = DsnValue(dsn_, "host", host_);
  user_ = DsnValue(dsn_, "user", user_);
  password_ = DsnValue(dsn_, "password", password_);
  database_ = DsnValue(dsn_, "database", database_);
  try {
    port_ = std::stoi(DsnValue(dsn_, "port", std::to_string(port_)));
  } catch (...) {
  }
  available_ = Ping();
  if (available_) PLOG_INFO("mysql ok " << host_ << ":" << port_ << "/" << database_);
  else PLOG_WARN("mysql unavailable: " << last_error_);
}

void MysqlClient::Disconnect() {
  if (sock_ != static_cast<uintptr_t>(-1) && sock_ != kInvalid) {
    closesocket(static_cast<socket_t>(sock_));
  }
  sock_ = static_cast<uintptr_t>(-1);
}

bool MysqlClient::SendPacket(const std::vector<uint8_t>& payload, uint8_t seq) {
  socket_t s = static_cast<socket_t>(sock_);
  uint8_t hdr[4];
  const uint32_t len = static_cast<uint32_t>(payload.size());
  hdr[0] = static_cast<uint8_t>(len & 0xff);
  hdr[1] = static_cast<uint8_t>((len >> 8) & 0xff);
  hdr[2] = static_cast<uint8_t>((len >> 16) & 0xff);
  hdr[3] = seq;
  if (send(s, reinterpret_cast<const char*>(hdr), 4, 0) != 4) return false;
  if (!payload.empty()) {
    if (send(s, reinterpret_cast<const char*>(payload.data()), static_cast<int>(payload.size()), 0) !=
        static_cast<int>(payload.size()))
      return false;
  }
  return true;
}

bool MysqlClient::RecvPacket(std::vector<uint8_t>& payload, uint8_t& seq) {
  socket_t s = static_cast<socket_t>(sock_);
  uint8_t hdr[4];
  int got = 0;
  while (got < 4) {
    int n = recv(s, reinterpret_cast<char*>(hdr) + got, 4 - got, 0);
    if (n <= 0) return false;
    got += n;
  }
  const uint32_t len = hdr[0] | (hdr[1] << 8) | (hdr[2] << 16);
  seq = hdr[3];
  payload.resize(len);
  got = 0;
  while (static_cast<uint32_t>(got) < len) {
    int n = recv(s, reinterpret_cast<char*>(payload.data()) + got, static_cast<int>(len - got), 0);
    if (n <= 0) return false;
    got += n;
  }
  return true;
}

bool MysqlClient::ConnectAndAuth() {
  EnsureWsa();
  Disconnect();
  socket_t s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == kInvalid) {
    last_error_ = "socket failed";
    return false;
  }
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<u_short>(port_));
  if (inet_pton(AF_INET, host_.c_str(), &addr.sin_addr) != 1) {
    closesocket(s);
    last_error_ = "bad host";
    return false;
  }
  if (connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    closesocket(s);
    last_error_ = "connect failed";
    return false;
  }
  DWORD timeout = 3000;
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
  setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
  sock_ = static_cast<uintptr_t>(s);

  std::vector<uint8_t> pkt;
  uint8_t seq = 0;
  if (!RecvPacket(pkt, seq) || pkt.empty()) {
    last_error_ = "handshake recv failed";
    Disconnect();
    return false;
  }
  // Parse scramble from handshake (protocol 10)
  size_t i = 0;
  const uint8_t protocol = pkt[i++];
  if (protocol != 10) {
    last_error_ = "unsupported protocol";
    Disconnect();
    return false;
  }
  while (i < pkt.size() && pkt[i] != 0) ++i;  // version
  if (i < pkt.size()) ++i;
  i += 4;  // connection id
  uint8_t scramble[20];
  size_t scramble_len = 0;
  if (i + 8 <= pkt.size()) {
    memcpy(scramble, &pkt[i], 8);
    scramble_len = 8;
    i += 8;
  }
  if (i < pkt.size()) ++i;  // filler
  i += 2;                   // capability lower
  if (i < pkt.size()) ++i;  // charset
  i += 2;                   // status
  i += 2;                   // capability upper
  uint8_t auth_len = 0;
  if (i < pkt.size()) auth_len = pkt[i++];
  i += 10;  // reserved
  if (auth_len > 8 && i + (auth_len - 8) <= pkt.size()) {
    memcpy(scramble + 8, &pkt[i], auth_len - 9 > 12 ? 12 : auth_len - 9);
    // scramble part2 is null-terminated 12 bytes typically
    size_t part2 = 0;
    while (i + part2 < pkt.size() && pkt[i + part2] != 0 && part2 < 12) ++part2;
    memcpy(scramble + 8, &pkt[i], part2);
    scramble_len = 8 + part2;
  } else if (i + 12 <= pkt.size()) {
    memcpy(scramble + 8, &pkt[i], 12);
    scramble_len = 20;
  }

  uint8_t token[20];
  MysqlNativePassword(password_, scramble, scramble_len >= 20 ? 20 : scramble_len, token);

  // Client auth response (Capability CLIENT_PROTOCOL_41 | CLIENT_SECURE_CONNECTION | CLIENT_PLUGIN_AUTH)
  const uint32_t caps = 0x000AA20D;  // common flags incl PROTOCOL_41, SECURE_CONNECTION, PLUGIN_AUTH
  std::vector<uint8_t> auth;
  auth.push_back(caps & 0xff);
  auth.push_back((caps >> 8) & 0xff);
  auth.push_back((caps >> 16) & 0xff);
  auth.push_back((caps >> 24) & 0xff);
  auth.push_back(0);
  auth.push_back(0);
  auth.push_back(0);
  auth.push_back(1);    // max packet 16MB-ish placeholder
  auth.push_back(33);   // utf8 charset
  for (int z = 0; z < 23; ++z) auth.push_back(0);
  auth.insert(auth.end(), user_.begin(), user_.end());
  auth.push_back(0);
  auth.push_back(20);
  auth.insert(auth.end(), token, token + 20);
  auth.insert(auth.end(), database_.begin(), database_.end());
  auth.push_back(0);
  const char* plugin = "mysql_native_password";
  auth.insert(auth.end(), plugin, plugin + strlen(plugin));
  auth.push_back(0);

  if (!SendPacket(auth, 1)) {
    last_error_ = "auth send failed";
    Disconnect();
    return false;
  }
  if (!RecvPacket(pkt, seq)) {
    last_error_ = "auth recv failed";
    Disconnect();
    return false;
  }
  if (pkt.empty() || pkt[0] == 0xff) {
    last_error_ = "auth rejected";
    if (pkt.size() > 3) {
      last_error_ += ": ";
      last_error_.append(reinterpret_cast<const char*>(pkt.data() + 3), pkt.size() - 3);
    }
    Disconnect();
    return false;
  }
  // OK or EOF
  return true;
}

bool MysqlClient::RunQuery(const std::string& sql, std::vector<MysqlRow>* out_rows, int* affected) {
  if (!ConnectAndAuth()) return false;
  std::vector<uint8_t> q;
  q.push_back(0x03);  // COM_QUERY
  q.insert(q.end(), sql.begin(), sql.end());
  if (!SendPacket(q, 0)) {
    last_error_ = "query send failed";
    Disconnect();
    return false;
  }
  std::vector<uint8_t> pkt;
  uint8_t seq = 0;
  if (!RecvPacket(pkt, seq)) {
    last_error_ = "query recv failed";
    Disconnect();
    return false;
  }
  if (pkt.empty()) {
    Disconnect();
    return false;
  }
  if (pkt[0] == 0xff) {
    last_error_ = "sql error";
    if (pkt.size() > 3) last_error_.append(reinterpret_cast<const char*>(pkt.data() + 3), pkt.size() - 3);
    Disconnect();
    return false;
  }
  if (pkt[0] == 0x00) {
    // OK packet: affected rows is length-encoded at [1]
    if (affected) *affected = pkt.size() > 1 ? static_cast<int>(pkt[1]) : 0;
    Disconnect();
    return true;
  }
  // Result set: column count
  if (!out_rows) {
    // drain
    while (true) {
      if (!RecvPacket(pkt, seq)) break;
      if (pkt.empty()) break;
      if (pkt[0] == 0xfe && pkt.size() < 9) break;  // EOF
      if (pkt[0] == 0x00 && pkt.size() >= 7) break;
    }
    Disconnect();
    return true;
  }
  // skip column definitions until EOF
  while (true) {
    if (!RecvPacket(pkt, seq)) {
      Disconnect();
      return false;
    }
    if (pkt.empty()) break;
    if (pkt[0] == 0xfe && pkt.size() < 9) break;
  }
  out_rows->clear();
  while (true) {
    if (!RecvPacket(pkt, seq)) break;
    if (pkt.empty()) break;
    if (pkt[0] == 0xfe && pkt.size() < 9) break;
    if (pkt[0] == 0xff) break;
    // row: length-encoded strings
    MysqlRow row;
    size_t pos = 0;
    while (pos < pkt.size()) {
      if (pkt[pos] == 0xfb) {  // NULL
        row.cols.emplace_back();
        ++pos;
        continue;
      }
      uint64_t len = 0;
      if (pkt[pos] < 0xfb) {
        len = pkt[pos++];
      } else if (pkt[pos] == 0xfc && pos + 2 < pkt.size()) {
        len = pkt[pos + 1] | (pkt[pos + 2] << 8);
        pos += 3;
      } else {
        break;
      }
      if (pos + len > pkt.size()) break;
      row.cols.emplace_back(reinterpret_cast<const char*>(&pkt[pos]), static_cast<size_t>(len));
      pos += static_cast<size_t>(len);
    }
    out_rows->push_back(std::move(row));
  }
  Disconnect();
  return true;
}

bool MysqlClient::Ping() {
  std::lock_guard<std::mutex> lk(mu_);
  available_ = ConnectAndAuth();
  Disconnect();
  return available_;
}

int MysqlClient::Exec(const std::string& sql) {
  std::lock_guard<std::mutex> lk(mu_);
  int affected = 0;
  if (!RunQuery(sql, nullptr, &affected)) return -1;
  return affected;
}

std::optional<std::vector<MysqlRow>> MysqlClient::Query(const std::string& sql) {
  std::lock_guard<std::mutex> lk(mu_);
  std::vector<MysqlRow> rows;
  if (!RunQuery(sql, &rows, nullptr)) return std::nullopt;
  return rows;
}

}  // namespace pandora

