#pragma once

#include <cstdint>

#include "common/errors.hpp"
#include "common/proto_wire.hpp"
#include "net/session_hub.hpp"

namespace pandora {

inline void SendError(SessionHub& hub, int64_t uid, Err e, uint32_t ref_msg_id = 0) {
  hub.Send(uid, MsgId::kS2C_Error,
           proto_wire::EncodeS2C_Error(static_cast<int32_t>(e), ErrMessage(e), ref_msg_id));
}

}  // namespace pandora
