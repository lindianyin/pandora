#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include <openssl/sha.h>

namespace pandora {
namespace crypto {

inline std::string Sha1Hex(const std::string& s) {
  unsigned char dig[SHA_DIGEST_LENGTH];
  SHA1(reinterpret_cast<const unsigned char*>(s.data()), s.size(), dig);
  static const char* hex = "0123456789abcdef";
  std::string out(40, '0');
  for (int i = 0; i < SHA_DIGEST_LENGTH; ++i) {
    out[i * 2] = hex[(dig[i] >> 4) & 0xf];
    out[i * 2 + 1] = hex[dig[i] & 0xf];
  }
  return out;
}

}  // namespace crypto
}  // namespace pandora
