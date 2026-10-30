// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef SPP_LINK_POLICY_HPP
#define SPP_LINK_POLICY_HPP

// Which /dev/btsppN links the daemon keeps poking.
//
// On BMW/MINI cars with BMW Apps (6NR), MyBMW / BMW Connected opens its own Bluetooth serial link to the car and
// AAIdrive rides on it (AAIdrive has no Bluetooth link of its own: its apps reach the car through MyBMW's BCL
// tunnel). The car serves that link in NBTMediaMainApp's A4AHandler, which opens the hard-coded mount point
// /dev/btspp0 and waits for the BCL startup magic 12 34 56 78. The daemon scans the same nodes for the Android Auto
// link. Every Android Auto WifiStartRequest written into the BMW app's link corrupts its BCL stream (the app then
// drops and reconnects; AAIdrive loses the car), and every read can take bytes the A4AHandler was waiting for.
//
// BMW log 2026-10-30: 53 links on btspp0, not one Android Auto reply, the BCL magic twice. So besides the BCL
// signature check, a node whose links end without a single Android Auto frame is given "strikes"; after `limit`
// strikes in a row it is left alone for `holdS` seconds (or until the node disappears), and the A4AHandler keeps it.
// A real Android Auto phone answers our WifiStartRequest within ~100 ms (00 02 00 07 ...), so it never collects a
// strike. Plain C++98, host-tested (tests/test_spp_link_policy.cpp).

#include <stdint.h>

namespace nbt {

class SppSilencePolicy {
public:
    enum { kNodes = 8 };

    SppSilencePolicy() : limit_(0), holdS_(60) { reset(); }

    void configure(int limit, uint32_t holdS) {
        limit_ = limit < 0 ? 0 : limit;
        holdS_ = holdS == 0 ? 1 : holdS;
        reset();
    }
    int limit() const { return limit_; }
    uint32_t holdSeconds() const { return holdS_; }

    // A link on node i ended. spoke = at least one Android Auto wireless frame came back. Returns true when the
    // node has just been put on hold.
    bool onLinkEnd(int i, bool spoke, uint32_t now) {
        if (!valid(i)) return false;
        if (spoke) { strikes_[i] = 0; return false; }
        if (limit_ == 0) return false;
        if (++strikes_[i] < limit_) return false;
        strikes_[i] = 0;
        holdUntil_[i] = now + holdS_;
        held_[i] = true;
        return true;
    }

    bool onHold(int i, uint32_t now) const {
        return valid(i) && held_[i] && (int32_t)(holdUntil_[i] - now) > 0;
    }

    // Hold expired (true once, so the caller can log it).
    bool releaseIfExpired(int i, uint32_t now) {
        if (!valid(i) || !held_[i] || (int32_t)(holdUntil_[i] - now) > 0) return false;
        held_[i] = false;
        return true;
    }

    // The node vanished (the app closed its link): forget everything about it.
    void nodeGone(int i) {
        if (!valid(i)) return;
        strikes_[i] = 0;
        held_[i] = false;
        holdUntil_[i] = 0;
    }

    int strikes(int i) const { return valid(i) ? strikes_[i] : 0; }

private:
    static bool valid(int i) { return i >= 0 && i < kNodes; }
    void reset() {
        for (int i = 0; i < kNodes; ++i) { strikes_[i] = 0; holdUntil_[i] = 0; held_[i] = false; }
    }
    int limit_;
    uint32_t holdS_;
    int strikes_[kNodes];
    uint32_t holdUntil_[kNodes];
    bool held_[kNodes];
};

}  // namespace nbt
#endif
