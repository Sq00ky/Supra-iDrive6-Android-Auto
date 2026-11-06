// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef AA_CONTROL_MSGS_HPP
#define AA_CONTROL_MSGS_HPP

// Plain (pre-encryption) AA control-channel messages the head unit sends. Header-only, so tests/dump_messages.cpp can
// check them against proto/aa_session.proto. Each message = 2-byte big-endian id + protobuf body.

#include "aa_proto.hpp"

#include <stdint.h>
#include <vector>

namespace nbt {

enum { AA_MSG_BYE_BYE_REQUEST = 0x000F };
enum { AA_BYE_REASON_QUIT = 1 };    // what the phone sends when it ends a session itself

inline std::vector<uint8_t> aaByeByeRequest(int reason) {
    enum { F_REASON = 1 };
    proto::ProtoWriter w(AA_MSG_BYE_BYE_REQUEST);
    w.integer(F_REASON, reason);
    return w.bytes();
}

} // namespace nbt

#endif // AA_CONTROL_MSGS_HPP
