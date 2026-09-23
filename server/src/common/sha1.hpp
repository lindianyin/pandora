#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace pandora {
namespace crypto {

inline uint32_t Rol(uint32_t v, uint32_t n) { return (v << n) | (v >> (32 - n)); }

inline void Sha1(const uint8_t* data, size_t len, uint8_t out[20]) {
  uint32_t h0 = 0x67452301, h1 = 0xEFCDAB89, h2 = 0x98BADCFE, h3 = 0x10325476, h4 = 0xC3D2E1F0;
  std::vector<uint8_t> msg(data, data + len);
  msg.push_back(0x80);
  while ((msg.size() % 64) != 56) msg.push_back(0);
  uint64_t bitlen = static_cast<uint64_t>(len) * 8;
  for (int i = 7; i >= 0; --i) msg.push_back(static_cast<uint8_t>((bitlen >> (i * 8)) & 0xff));

  for (size_t chunk = 0; chunk < msg.size(); chunk += 64) {
    uint32_t w[80];
    for (int i = 0; i < 16; ++i) {
      w[i] = (msg[chunk + i * 4] << 24) | (msg[chunk + i * 4 + 1] << 16) | (msg[chunk + i * 4 + 2] << 8) |
             msg[chunk + i * 4 + 3];
    }
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
      uint32_t temp = Rol(a, 5) + f + e + k + w[i];
      e = d;
      d = c;
      c = Rol(b, 30);
      b = a;
      a = temp;
    }
    h0 += a;
    h1 += b;
    h2 += c;
    h3 += d;
    h4 += e;
  }
  auto put = [&](uint32_t v, int off) {
    out[off] = (v >> 24) & 0xff;
    out[off + 1] = (v >> 16) & 0xff;
    out[off + 2] = (v >> 8) & 0xff;
    out[off + 3] = v & 0xff;
  };
  put(h0, 0);
  put(h1, 4);
  put(h2, 8);
  put(h3, 12);
  put(h4, 16);
}

inline std::string Sha1Hex(const std::string& s) {
  uint8_t dig[20];
  Sha1(reinterpret_cast<const uint8_t*>(s.data()), s.size(), dig);
  static const char* hex = "0123456789abcdef";
  std::string out(40, '0');
  for (int i = 0; i < 20; ++i) {
    out[i * 2] = hex[(dig[i] >> 4) & 0xf];
    out[i * 2 + 1] = hex[dig[i] & 0xf];
  }
  return out;
}

inline void MysqlNativePassword(const std::string& password, const uint8_t* scramble, size_t scramble_len,
                                uint8_t out[20]) {
  uint8_t stage1[20], stage2[20], stage3[20];
  Sha1(reinterpret_cast<const uint8_t*>(password.data()), password.size(), stage1);
  Sha1(stage1, 20, stage2);
  std::vector<uint8_t> mix(scramble, scramble + scramble_len);
  mix.insert(mix.end(), stage2, stage2 + 20);
  Sha1(mix.data(), mix.size(), stage3);
  for (int i = 0; i < 20; ++i) out[i] = stage1[i] ^ stage3[i];
}

}  // namespace crypto
}  // namespace pandora
