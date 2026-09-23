#pragma once
#include <optional>
#include <string>
#include "store/memory_store.hpp"
namespace pandora {
struct LoginResult {
  int64_t uid{0};
  std::string token;
  std::string nickname;
  int64_t gold{0};
  int64_t diamond{0};
};
class AuthService {
 public:
  explicit AuthService(MemoryStore& store) : store_(store) {}
  LoginResult LoginGuest(const std::string& device_id);
  std::optional<SessionRecord> ValidateToken(const std::string& token);
 private:
  MemoryStore& store_;
};
}  // namespace pandora
