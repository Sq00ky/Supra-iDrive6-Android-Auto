// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
//
// A video frame that never reached the decoder (queue full, or mdev did not take it) leaves the picture damaged until
// the phone's next keyframe, and the phone sends one only when the video focus comes back. So the daemon asks the way
// it does after a decoder rebuild: video focus off, on again kGapMs later. At most one ask in kMinIntervalMs, so a
// decoder that keeps falling behind does not make the picture stop and start. Pure state machine, host-tested
// (tests/test_keyframe_ask.cpp); the main loop calls step() once per tick and sends what it returns.
#ifndef NBT_KEYFRAME_ASK_HPP
#define NBT_KEYFRAME_ASK_HPP

#include <stdint.h>

namespace nbt {

class KeyframeAsk {
public:
    enum Action { kNone = 0, kFocusOff = 1, kFocusOn = 2 };
    enum { kGapMs = 500, kMinIntervalMs = 10000 };

    KeyframeAsk() : pending_(false), asked_(false), offMs_(0) {}

    // wanted:     a frame was lost and no keyframe has come since.
    // canAsk:     Android Auto has the screen in a settled session, and nothing else is cycling the video focus.
    // canRestore: Android Auto should have the screen. While iDrive has it the second half (focus on) waits, because
    //             not every way back sends a focus of its own; a second one is harmless.
    // active:     a session is running. Its end drops a pending second half: the next session starts focused.
    Action step(uint32_t nowMs, bool wanted, bool canAsk, bool canRestore, bool active) {
        if (!active) { pending_ = false; return kNone; }
        if (pending_) {
            if (!canRestore || (uint32_t)(nowMs - offMs_) < (uint32_t)kGapMs) return kNone;
            pending_ = false;
            return kFocusOn;
        }
        if (!wanted || !canAsk) return kNone;
        if (asked_ && (uint32_t)(nowMs - offMs_) < (uint32_t)kMinIntervalMs) return kNone;
        asked_ = true;
        pending_ = true;
        offMs_ = nowMs;
        return kFocusOff;
    }

    bool pending() const { return pending_; }   // focus is off, the second half is still to come

private:
    bool pending_;
    bool asked_;
    uint32_t offMs_;
};

}  // namespace nbt

#endif
