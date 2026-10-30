// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef NBT_SPP_REREG_HPP
#define NBT_SPP_REREG_HPP

// When to register the Android Auto Bluetooth service again while no phone opens the Bluetooth setup link.
//
// MINI F60 report (v1.8.35, 2026-10-29): after a cold start, and after the head unit went to sleep and the phone
// closed the link, the phone stayed connected over Bluetooth (A2DP music kept playing) but never opened the Android
// Auto link, so /dev/btspp0 never appeared. Restart from the iDrive menu fixed it within a second; the only thing
// Restart changes on the Bluetooth side is SppUuidDeregister + SppUuidRegister while the phone is connected. The
// daemon now does that by itself: every NBT_SPP_REREG_S (default 5) seconds for the first 2 minutes without a link,
// then every 30 s, so a car with no Android phone around (iPhone drive) is not churned. 0 turns it off.

#include <stdint.h>
#include <stdlib.h>

namespace nbt {

static const int kSppReregFastPhaseSec = 120;   // retry at the short interval for this long after a link went away
static const int kSppReregSlowSec = 30;         // then this often

// Seconds to wait before re-registration number `attempt` (0-based) of one idle period.
inline int sppReregIntervalSec(int attempt, int base) {
    if (base <= 0) return 0;
    if (base >= kSppReregSlowSec) return base;
    int fastTries = kSppReregFastPhaseSec / base;
    return attempt < fastTries ? base : kSppReregSlowSec;
}

// NBT_SPP_REREG_S: short interval in seconds (default 5, 0 = off, max 3600).
inline int sppReregBaseSec() {
    const char* e = getenv("NBT_SPP_REREG_S");
    if (!e || !*e) return 5;
    int v = atoi(e);
    if (v <= 0) return 0;
    return v > 3600 ? 3600 : v;
}

// Idle-period timer: reset() when waiting for a link starts (start-up, end of a link/session, access point back),
// due() returns true once per interval and moves on to the next, longer one.
class SppReregTimer {
public:
    SppReregTimer() : base_(0), attempt_(0), since_(0) {}
    void configure(int baseSec) { base_ = baseSec > 0 ? baseSec : 0; }
    bool enabled() const { return base_ > 0; }
    void reset(uint32_t nowSec) { attempt_ = 0; since_ = nowSec; }
    bool due(uint32_t nowSec) {
        if (!base_) return false;
        if ((uint32_t)(nowSec - since_) < (uint32_t)sppReregIntervalSec(attempt_, base_)) return false;
        since_ = nowSec;
        ++attempt_;
        return true;
    }
    int attempts() const { return attempt_; }
    int nextIntervalSec() const { return sppReregIntervalSec(attempt_, base_); }

private:
    int base_;
    int attempt_;
    uint32_t since_;
};

} // namespace nbt

#endif // NBT_SPP_REREG_HPP
