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

#include <string>

namespace nbt {

static const int kSppReregFastPhaseSec = 120;   // retry at the short interval for this long after a link went away
static const int kSppReregSlowSec = 30;         // then this often
// A phone that asked for the link (ServiceRequest / ...Complete / ...Error) this recently is connected and already sees
// the service: re-registering would only take the service away under it while it retries (Z Fold8 bugreport
// 2026-11-14: Android Auto retries every ~5 s by itself). Hold re-registration for this long after its last request.
static const int kSppReregHoldSec = 30;         // default hold after the phone's last SPP request (NBT_SPP_REREG_HOLD_S)
static const int kSppHoldCapSec = 60;           // during a continuous hold, still one re-registration this often

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

// NBT_SPP_REREG_HOLD_S: seconds re-registration is held after the phone's last SPP request (default 30, 0 = no hold,
// max 600). Pure part takes the env value (NULL = unset).
inline int sppReregHoldSetting(const char* e) {
    if (!e || !*e) return kSppReregHoldSec;
    int v = atoi(e);
    if (v <= 0) return 0;
    return v > 600 ? 600 : v;
}
inline int sppReregHoldSec() { return sppReregHoldSetting(getenv("NBT_SPP_REREG_HOLD_S")); }

// Cap on a continuous hold: a phone that keeps asking and keeps being refused would otherwise hold re-registration
// for good. heldSince = when this hold began (or when the last capped round ran). True = run one round now.
inline bool sppHoldCapDue(uint32_t heldSince, uint32_t nowSec, int capSec = kSppHoldCapSec) {
    return capSec > 0 && (uint32_t)(nowSec - heldSince) >= (uint32_t)capSec;
}

// Which phone requests hold re-registration: only the serial-link (Android Auto) one. A2DP / HFP requests come
// when the phone connects audio and say nothing about the Android Auto link (J29 log 2026-11-16: an A2DP request
// held a round on its own); BMW logs show only 'SPP'.
inline bool sppRequestHolds(const std::string& service) { return service == "SPP"; }

// True while re-registration is held: a phone request was seen and the last one is less than holdSec old.
inline bool sppReregHeld(bool asked, uint32_t lastAskSec, uint32_t nowSec, int holdSec = kSppReregHoldSec) {
    return asked && holdSec > 0 && (uint32_t)(nowSec - lastAskSec) < (uint32_t)holdSec;
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
