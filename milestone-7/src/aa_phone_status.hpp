// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef AA_PHONE_STATUS_HPP
#define AA_PHONE_STATUS_HPP

// PhoneStatus (0x8001) on the phone status channel: repeated call = 1 {state = 1, ...}. States: 1 in call, 2 on hold,
// 3 inactive, 4 ringing, 5 conference, 6 muted. Car-seen 2026-11-19: ringing 4, then 1 once answered.

#include "aa_proto.hpp"

#include <stddef.h>
#include <stdint.h>

namespace nbt {

// body: the message without its id. True while any call is in a state other than inactive.
inline bool aaPhoneInCall(const uint8_t* body, size_t len) {
    if (!body) return false;
    proto::PbReader r(body, len);
    proto::PbField f;
    while (r.next(f)) {
        if (f.num != 1 || f.wire != proto::WIRE_LENGTH) continue;
        int64_t state = 0;
        if (proto::pbInt(proto::PbSpan(f.data, f.len), 1, state) && state != 0 && state != 3) return true;
    }
    return false;
}

} // namespace nbt

#endif // AA_PHONE_STATUS_HPP
