#include "auth/auth_service.hpp"

namespace pandora {

LoginResult AuthService::LoginGuest(const std::string& device_id) {
  auto player = store_.CreateGuest(device_id);
  auto session = store_.CreateSession(player.uid);
  LoginResult r;
  r.uid = player.uid;
  r.token = session.token;
  r.nickname = player.nickname;
  r.gold = player.gold;
  r.diamond = player.diamond;
  return r;
}

std::optional<SessionRecord> AuthService::ValidateToken(const std::string& token) {
  return store_.GetSession(token);
}

}  // namespace pandora
