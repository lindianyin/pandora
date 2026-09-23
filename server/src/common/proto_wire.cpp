#include "common/proto_wire.hpp"

#include <cstring>

namespace pandora {
namespace proto_wire {

namespace {

void AppendVarint(std::vector<uint8_t>& out, uint64_t v) {
  while (v >= 0x80) {
    out.push_back(static_cast<uint8_t>(v | 0x80));
    v >>= 7;
  }
  out.push_back(static_cast<uint8_t>(v));
}

bool ReadVarint(const uint8_t*& p, const uint8_t* end, uint64_t& v) {
  v = 0;
  int shift = 0;
  while (p < end && shift <= 63) {
    const uint8_t b = *p++;
    v |= static_cast<uint64_t>(b & 0x7F) << shift;
    if ((b & 0x80) == 0) return true;
    shift += 7;
  }
  return false;
}

void AppendRaw(std::vector<uint8_t>& dst, const std::vector<uint8_t>& src) {
  dst.insert(dst.end(), src.begin(), src.end());
}

}  // namespace

std::vector<uint8_t> EncodeStringField(uint32_t field_number, const std::string& value) {
  std::vector<uint8_t> out;
  AppendVarint(out, (static_cast<uint64_t>(field_number) << 3) | 2);
  AppendVarint(out, value.size());
  out.insert(out.end(), value.begin(), value.end());
  return out;
}

std::vector<uint8_t> EncodeVarintField(uint32_t field_number, uint64_t value) {
  std::vector<uint8_t> out;
  AppendVarint(out, (static_cast<uint64_t>(field_number) << 3) | 0);
  AppendVarint(out, value);
  return out;
}

std::vector<uint8_t> EncodeInt32Field(uint32_t field_number, int32_t value) {
  return EncodeVarintField(field_number, static_cast<uint64_t>(static_cast<int64_t>(value)));
}

std::vector<uint8_t> EncodeInt64Field(uint32_t field_number, int64_t value) {
  return EncodeVarintField(field_number, static_cast<uint64_t>(value));
}

std::vector<uint8_t> EncodeBoolField(uint32_t field_number, bool value) {
  return EncodeVarintField(field_number, value ? 1 : 0);
}

std::vector<uint8_t> EncodeBytesField(uint32_t field_number, const std::vector<uint8_t>& value) {
  std::vector<uint8_t> out;
  AppendVarint(out, (static_cast<uint64_t>(field_number) << 3) | 2);
  AppendVarint(out, value.size());
  out.insert(out.end(), value.begin(), value.end());
  return out;
}

std::vector<uint8_t> EncodeRepeatedInt32(uint32_t field_number, const std::vector<int32_t>& values) {
  std::vector<uint8_t> out;
  for (int32_t v : values) AppendRaw(out, EncodeInt32Field(field_number, v));
  return out;
}

bool DecodeStringField(const uint8_t* data, size_t len, uint32_t field_number, std::string& out) {
  const uint8_t* p = data;
  const uint8_t* end = data + len;
  while (p < end) {
    uint64_t tag = 0;
    if (!ReadVarint(p, end, tag)) return false;
    const uint32_t fn = static_cast<uint32_t>(tag >> 3);
    const uint32_t wt = static_cast<uint32_t>(tag & 7);
    if (wt == 0) {
      uint64_t v = 0;
      if (!ReadVarint(p, end, v)) return false;
      (void)v;
    } else if (wt == 2) {
      uint64_t slen = 0;
      if (!ReadVarint(p, end, slen) || p + slen > end) return false;
      if (fn == field_number) {
        out.assign(reinterpret_cast<const char*>(p), static_cast<size_t>(slen));
        return true;
      }
      p += slen;
    } else if (wt == 5) {
      if (p + 4 > end) return false;
      p += 4;
    } else if (wt == 1) {
      if (p + 8 > end) return false;
      p += 8;
    } else {
      return false;
    }
  }
  return false;
}

bool DecodeInt64Field(const uint8_t* data, size_t len, uint32_t field_number, int64_t& out) {
  const uint8_t* p = data;
  const uint8_t* end = data + len;
  while (p < end) {
    uint64_t tag = 0;
    if (!ReadVarint(p, end, tag)) return false;
    const uint32_t fn = static_cast<uint32_t>(tag >> 3);
    const uint32_t wt = static_cast<uint32_t>(tag & 7);
    if (wt == 0) {
      uint64_t v = 0;
      if (!ReadVarint(p, end, v)) return false;
      if (fn == field_number) {
        out = static_cast<int64_t>(v);
        return true;
      }
    } else if (wt == 2) {
      uint64_t slen = 0;
      if (!ReadVarint(p, end, slen) || p + slen > end) return false;
      p += slen;
    } else if (wt == 5) {
      if (p + 4 > end) return false;
      p += 4;
    } else if (wt == 1) {
      if (p + 8 > end) return false;
      p += 8;
    } else {
      return false;
    }
  }
  return false;
}

bool DecodeInt32Field(const uint8_t* data, size_t len, uint32_t field_number, int32_t& out) {
  int64_t v = 0;
  if (!DecodeInt64Field(data, len, field_number, v)) return false;
  out = static_cast<int32_t>(v);
  return true;
}

bool DecodeBoolField(const uint8_t* data, size_t len, uint32_t field_number, bool& out) {
  int64_t v = 0;
  if (!DecodeInt64Field(data, len, field_number, v)) return false;
  out = v != 0;
  return true;
}

std::vector<int32_t> DecodeAllInt32Field(const uint8_t* data, size_t len, uint32_t field_number) {
  std::vector<int32_t> out;
  const uint8_t* p = data;
  const uint8_t* end = data + len;
  while (p < end) {
    uint64_t tag = 0;
    if (!ReadVarint(p, end, tag)) break;
    const uint32_t fn = static_cast<uint32_t>(tag >> 3);
    const uint32_t wt = static_cast<uint32_t>(tag & 7);
    if (wt == 0) {
      uint64_t v = 0;
      if (!ReadVarint(p, end, v)) break;
      if (fn == field_number) out.push_back(static_cast<int32_t>(v));
    } else if (wt == 2) {
      uint64_t slen = 0;
      if (!ReadVarint(p, end, slen) || p + slen > end) break;
      if (fn == field_number) {
        // packed repeated
        const uint8_t* pe = p + slen;
        while (p < pe) {
          uint64_t v = 0;
          if (!ReadVarint(p, pe, v)) break;
          out.push_back(static_cast<int32_t>(v));
        }
      } else {
        p += slen;
      }
    } else if (wt == 5) {
      if (p + 4 > end) break;
      p += 4;
    } else if (wt == 1) {
      if (p + 8 > end) break;
      p += 8;
    } else {
      break;
    }
  }
  return out;
}

std::vector<uint8_t> EncodeC2S_Auth(const std::string& token) { return EncodeStringField(1, token); }

bool DecodeC2S_Auth(const uint8_t* data, size_t len, std::string& token) {
  return DecodeStringField(data, len, 1, token);
}

std::vector<uint8_t> EncodeS2C_AuthResult(int32_t code, const std::string& message, int64_t uid) {
  auto a = EncodeInt32Field(1, code);
  AppendRaw(a, EncodeStringField(2, message));
  AppendRaw(a, EncodeInt64Field(3, uid));
  return a;
}

std::vector<uint8_t> EncodeC2S_Heartbeat(int64_t client_time_ms) {
  return EncodeInt64Field(1, client_time_ms);
}

bool DecodeC2S_Heartbeat(const uint8_t* data, size_t len, int64_t& client_time_ms) {
  if (len == 0) {
    client_time_ms = 0;
    return true;
  }
  return DecodeInt64Field(data, len, 1, client_time_ms);
}

std::vector<uint8_t> EncodeS2C_HeartbeatAck(int64_t server_time_ms) {
  return EncodeInt64Field(1, server_time_ms);
}

std::vector<uint8_t> EncodeS2C_Kick(int32_t reason, const std::string& message) {
  auto a = EncodeInt32Field(1, reason);
  AppendRaw(a, EncodeStringField(2, message));
  return a;
}

std::vector<uint8_t> EncodeS2C_Error(int32_t code, const std::string& message, uint32_t ref_msg_id) {
  auto a = EncodeInt32Field(1, code);
  AppendRaw(a, EncodeStringField(2, message));
  AppendRaw(a, EncodeVarintField(3, ref_msg_id));
  return a;
}

std::vector<uint8_t> EncodeS2C_LobbyInfo(const std::vector<LobbyTemplate>& templates, int64_t gold,
                                         int64_t diamond) {
  std::vector<uint8_t> out;
  for (const auto& t : templates) {
    std::vector<uint8_t> msg;
    AppendRaw(msg, EncodeInt32Field(1, t.id));
    AppendRaw(msg, EncodeStringField(2, t.name));
    AppendRaw(msg, EncodeInt32Field(3, t.base_score));
    AppendRaw(msg, EncodeInt64Field(4, t.min_gold));
    AppendRaw(msg, EncodeInt64Field(5, t.max_gold));
    AppendRaw(msg, EncodeBoolField(6, t.enabled));
    AppendRaw(out, EncodeBytesField(1, msg));
  }
  AppendRaw(out, EncodeInt64Field(2, gold));
  AppendRaw(out, EncodeInt64Field(3, diamond));
  return out;
}

bool DecodeC2S_QuickMatch(const uint8_t* data, size_t len, int32_t& template_id) {
  template_id = 1;
  if (len == 0) return true;
  return DecodeInt32Field(data, len, 1, template_id);
}

std::vector<uint8_t> EncodeS2C_MatchStatus(int32_t status, int64_t room_id, const std::string& message) {
  auto a = EncodeInt32Field(1, status);
  AppendRaw(a, EncodeInt64Field(2, room_id));
  AppendRaw(a, EncodeStringField(3, message));
  return a;
}

std::vector<uint8_t> EncodeS2C_RoomState(int64_t room_id, int32_t template_id, const std::vector<RoomSeat>& seats,
                                         const std::string& phase) {
  std::vector<uint8_t> out = EncodeInt64Field(1, room_id);
  AppendRaw(out, EncodeInt32Field(2, template_id));
  for (const auto& s : seats) {
    std::vector<uint8_t> msg;
    AppendRaw(msg, EncodeInt32Field(1, s.seat_id));
    AppendRaw(msg, EncodeInt64Field(2, s.uid));
    AppendRaw(msg, EncodeStringField(3, s.nickname));
    AppendRaw(msg, EncodeBoolField(4, s.ready));
    AppendRaw(msg, EncodeBoolField(5, s.online));
    AppendRaw(msg, EncodeBoolField(6, s.trusteeship));
    AppendRaw(out, EncodeBytesField(3, msg));
  }
  AppendRaw(out, EncodeStringField(4, phase));
  return out;
}

bool DecodeC2S_Ready(const uint8_t* data, size_t len, bool& ready) {
  ready = true;
  if (len == 0) return true;
  return DecodeBoolField(data, len, 1, ready);
}

bool DecodeC2S_DdzBid(const uint8_t* data, size_t len, int32_t& score) {
  score = 0;
  if (len == 0) return true;
  return DecodeInt32Field(data, len, 1, score);
}

bool DecodeC2S_DdzPlay(const uint8_t* data, size_t len, bool& pass, std::vector<int32_t>& cards) {
  pass = false;
  cards.clear();
  const uint8_t* p = data;
  const uint8_t* end = data + len;
  while (p < end) {
    uint64_t tag = 0;
    if (!ReadVarint(p, end, tag)) return false;
    const uint32_t fn = static_cast<uint32_t>(tag >> 3);
    const uint32_t wt = static_cast<uint32_t>(tag & 7);
    if (wt == 0) {
      uint64_t v = 0;
      if (!ReadVarint(p, end, v)) return false;
      if (fn == 1) pass = v != 0;
      if (fn == 2) cards.push_back(static_cast<int32_t>(v));
    } else if (wt == 2) {
      uint64_t slen = 0;
      if (!ReadVarint(p, end, slen) || p + slen > end) return false;
      if (fn == 2) {
        const uint8_t* pe = p + slen;
        while (p < pe) {
          uint64_t v = 0;
          if (!ReadVarint(p, pe, v)) return false;
          cards.push_back(static_cast<int32_t>(v));
        }
      } else {
        p += slen;
      }
    } else {
      return false;
    }
  }
  return true;
}

std::vector<uint8_t> EncodeS2C_DdzGameStart(int32_t seat_id, const std::vector<int32_t>& hand, int32_t landlord_seat,
                                            const std::vector<int32_t>& bottom) {
  auto a = EncodeInt32Field(1, seat_id);
  AppendRaw(a, EncodeRepeatedInt32(2, hand));
  AppendRaw(a, EncodeInt32Field(3, landlord_seat));
  AppendRaw(a, EncodeRepeatedInt32(4, bottom));
  return a;
}

std::vector<uint8_t> EncodeS2C_DdzTurn(int32_t seat_id, const std::string& phase, int32_t timeout_s) {
  auto a = EncodeInt32Field(1, seat_id);
  AppendRaw(a, EncodeStringField(2, phase));
  AppendRaw(a, EncodeInt32Field(3, timeout_s));
  return a;
}

std::vector<uint8_t> EncodeS2C_DdzBidBroadcast(int32_t seat_id, int32_t score) {
  auto a = EncodeInt32Field(1, seat_id);
  AppendRaw(a, EncodeInt32Field(2, score));
  return a;
}

std::vector<uint8_t> EncodeS2C_DdzPlayBroadcast(int32_t seat_id, bool pass, const std::vector<int32_t>& cards,
                                                int32_t cards_left) {
  auto a = EncodeInt32Field(1, seat_id);
  AppendRaw(a, EncodeBoolField(2, pass));
  AppendRaw(a, EncodeRepeatedInt32(3, cards));
  AppendRaw(a, EncodeInt32Field(4, cards_left));
  return a;
}

std::vector<uint8_t> EncodeS2C_DdzSettle(int64_t round_id, int32_t base_score, int32_t multiplier,
                                         const std::vector<SettleEntry>& entries) {
  auto a = EncodeInt64Field(1, round_id);
  AppendRaw(a, EncodeInt32Field(2, base_score));
  AppendRaw(a, EncodeInt32Field(3, multiplier));
  for (const auto& e : entries) {
    std::vector<uint8_t> msg;
    AppendRaw(msg, EncodeInt64Field(1, e.uid));
    AppendRaw(msg, EncodeInt32Field(2, e.seat_id));
    AppendRaw(msg, EncodeInt64Field(3, e.delta_gold));
    AppendRaw(a, EncodeBytesField(4, msg));
  }
  return a;
}

std::vector<uint8_t> EncodeS2C_DdzReconnect(int32_t seat_id, const std::string& phase,
                                            const std::vector<int32_t>& hand, int32_t landlord_seat,
                                            int32_t current_seat, int32_t timeout_s) {
  auto a = EncodeInt32Field(1, seat_id);
  AppendRaw(a, EncodeStringField(2, phase));
  AppendRaw(a, EncodeRepeatedInt32(3, hand));
  AppendRaw(a, EncodeInt32Field(4, landlord_seat));
  AppendRaw(a, EncodeInt32Field(5, current_seat));
  AppendRaw(a, EncodeInt32Field(6, timeout_s));
  return a;
}

std::vector<uint8_t> EncodeS2C_ActivityUpdate(int32_t activity_id, const std::string& type,
                                              const std::string& progress_json, bool claimable) {
  auto a = EncodeInt32Field(1, activity_id);
  AppendRaw(a, EncodeStringField(2, type));
  AppendRaw(a, EncodeStringField(3, progress_json));
  AppendRaw(a, EncodeBoolField(4, claimable));
  return a;
}

}  // namespace proto_wire
}  // namespace pandora

