#pragma once
#include <cstdint>
#include <optional>
#include <vector>
namespace pandora {
struct Frame {
  uint32_t msg_id{0};
  std::vector<uint8_t> body;
};
// length (u32 LE) = 4 + body.size(); then msg_id (u32 LE); then body
std::vector<uint8_t> EncodeFrame(uint32_t msg_id, const std::vector<uint8_t>& body);
std::optional<Frame> TryDecodeOneFrame(std::vector<uint8_t>& buffer, uint32_t max_frame_bytes);
}  // namespace pandora
