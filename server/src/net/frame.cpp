#include "net/frame.hpp"

#include <cstring>

namespace pandora {

namespace {

uint32_t ReadU32LE(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

void WriteU32LE(uint8_t* p, uint32_t v) {
  p[0] = static_cast<uint8_t>(v & 0xFF);
  p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
  p[2] = static_cast<uint8_t>((v >> 16) & 0xFF);
  p[3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}

}  // namespace

std::vector<uint8_t> EncodeFrame(uint32_t msg_id, const std::vector<uint8_t>& body) {
  const uint32_t length = 4u + static_cast<uint32_t>(body.size());
  std::vector<uint8_t> out(8 + body.size());
  WriteU32LE(out.data(), length);
  WriteU32LE(out.data() + 4, msg_id);
  if (!body.empty()) {
    std::memcpy(out.data() + 8, body.data(), body.size());
  }
  return out;
}

std::optional<Frame> TryDecodeOneFrame(std::vector<uint8_t>& buffer, uint32_t max_frame_bytes) {
  if (buffer.size() < 8) return std::nullopt;
  const uint32_t length = ReadU32LE(buffer.data());
  if (length < 4 || length > max_frame_bytes) {
    buffer.clear();
    return std::nullopt;
  }
  const size_t total = 4u + static_cast<size_t>(length);
  if (buffer.size() < total) return std::nullopt;

  Frame f;
  f.msg_id = ReadU32LE(buffer.data() + 4);
  const size_t body_len = length - 4u;
  f.body.assign(buffer.begin() + 8, buffer.begin() + 8 + static_cast<std::ptrdiff_t>(body_len));
  buffer.erase(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(total));
  return f;
}

}  // namespace pandora
