#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace pandora {
namespace proto_wire {

// Thin helpers over official protobuf messages (proto/*.proto).

struct LobbyTemplate {
  int32_t id{0};
  std::string name;
  int32_t base_score{0};
  int64_t min_gold{0};
  int64_t max_gold{0};
  bool enabled{true};
};

struct RoomSeat {
  int32_t seat_id{0};
  int64_t uid{0};
  std::string nickname;
  bool ready{false};
  bool online{true};
  bool trusteeship{false};
};

struct SettleEntry {
  int64_t uid{0};
  int32_t seat_id{0};
  int64_t delta_gold{0};
};

std::vector<uint8_t> EncodeC2S_Auth(const std::string& token);
bool DecodeC2S_Auth(const uint8_t* data, size_t len, std::string& token);

std::vector<uint8_t> EncodeS2C_AuthResult(int32_t code, const std::string& message, int64_t uid);
std::vector<uint8_t> EncodeC2S_Heartbeat(int64_t client_time_ms);
bool DecodeC2S_Heartbeat(const uint8_t* data, size_t len, int64_t& client_time_ms);
std::vector<uint8_t> EncodeS2C_HeartbeatAck(int64_t server_time_ms);
std::vector<uint8_t> EncodeS2C_Kick(int32_t reason, const std::string& message);
std::vector<uint8_t> EncodeS2C_Error(int32_t code, const std::string& message, uint32_t ref_msg_id);

std::vector<uint8_t> EncodeS2C_LobbyInfo(const std::vector<LobbyTemplate>& templates, int64_t gold,
                                         int64_t diamond);
bool DecodeC2S_QuickMatch(const uint8_t* data, size_t len, int32_t& template_id);
std::vector<uint8_t> EncodeS2C_MatchStatus(int32_t status, int64_t room_id, const std::string& message);

std::vector<uint8_t> EncodeS2C_RoomState(int64_t room_id, int32_t template_id, const std::vector<RoomSeat>& seats,
                                         const std::string& phase);
bool DecodeC2S_Ready(const uint8_t* data, size_t len, bool& ready);

bool DecodeC2S_DdzBid(const uint8_t* data, size_t len, int32_t& score);
bool DecodeC2S_DdzPlay(const uint8_t* data, size_t len, bool& pass, std::vector<int32_t>& cards);

std::vector<uint8_t> EncodeS2C_DdzGameStart(int32_t seat_id, const std::vector<int32_t>& hand, int32_t landlord_seat,
                                            const std::vector<int32_t>& bottom);
std::vector<uint8_t> EncodeS2C_DdzTurn(int32_t seat_id, const std::string& phase, int32_t timeout_s);
std::vector<uint8_t> EncodeS2C_DdzBidBroadcast(int32_t seat_id, int32_t score);
std::vector<uint8_t> EncodeS2C_DdzPlayBroadcast(int32_t seat_id, bool pass, const std::vector<int32_t>& cards,
                                                int32_t cards_left);
std::vector<uint8_t> EncodeS2C_DdzSettle(int64_t round_id, int32_t base_score, int32_t multiplier,
                                         const std::vector<SettleEntry>& entries);
std::vector<uint8_t> EncodeS2C_DdzReconnect(int32_t seat_id, const std::string& phase,
                                            const std::vector<int32_t>& hand, int32_t landlord_seat,
                                            int32_t current_seat, int32_t timeout_s);

std::vector<uint8_t> EncodeS2C_ActivityUpdate(int32_t activity_id, const std::string& type,
                                              const std::string& progress_json, bool claimable);

}  // namespace proto_wire
}  // namespace pandora
