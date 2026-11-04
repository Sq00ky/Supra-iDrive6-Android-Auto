// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef CAR_PTT_HPP
#define CAR_PTT_HPP

// Steering-wheel voice (PTT) button: MOST FBlock 0x15 (MFL) inst 02, fkt 0x512 (PTTButton1), Status, 1 byte
// (1 pressed, 0 released, 3 cancel, 2 ignored). Copies only: the HMI still gets it (short press = BMW voice dialog, long
// press = phone voice session). The HMI times a long press itself (750 ms, HmiMain3D onPTTButtonStatus @0x252334); so
// does this class, from the time each message was received. Pure: host-tested in tests/test_ptt.cpp.

#include <stdint.h>

namespace nbt {
namespace car {

class PttTracker {
public:
    enum { kLongPressMs = 750 };
    enum Kind { kPressed, kIgnored, kCancelled, kReleasedShort, kReleasedLong };
    struct Result {
        Kind kind;
        uint32_t heldMs;    // for the two released kinds
    };

    PttTracker() : downMs_(0) {}

    Result feed(uint8_t state, uint32_t rxMs) {
        Result r;
        r.heldMs = 0;
        if (state == 1) {
            downMs_ = rxMs | 1u;    // | 1: 0 means not held
            r.kind = kPressed;
            return r;
        }
        if (state != 0 && state != 3) {
            r.kind = kIgnored;
            return r;
        }
        bool wasDown = downMs_ != 0;
        r.heldMs = wasDown ? rxMs - downMs_ : 0;
        downMs_ = 0;
        if (state == 3) r.kind = kCancelled;
        else r.kind = (wasDown && r.heldMs >= (uint32_t)kLongPressMs) ? kReleasedLong : kReleasedShort;
        return r;
    }

    void reset() { downMs_ = 0; }
    bool held() const { return downMs_ != 0; }

private:
    uint32_t downMs_;
};

} // namespace car
} // namespace nbt

#endif
