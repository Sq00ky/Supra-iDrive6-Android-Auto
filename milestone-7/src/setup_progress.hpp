// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef NBT_SETUP_PROGRESS_HPP
#define NBT_SETUP_PROGRESS_HPP
#include "reconnect_limits.hpp"
#include <stdint.h>

namespace nbt {

// Daemon telemetry shared by wireless setup, TCP accept and recovery control.
// The caller MUST hold the same external mutex for every access, including the
// canRecover() decision and committing shutdown / accepting a TCP connection.
// This class deliberately does not own process shutdown or session lifecycle.
class SetupProgress {
public:
    SetupProgress() : failures_(0), cycle_(0), tcp_(false), starting_(false),
                      waitingSpp_(false), waitingSince_(0), confirmedCycle_(0) {}

    unsigned failures() const { return failures_; }
    unsigned cycle() const { return cycle_; }
    unsigned confirmedCycle() const { return confirmedCycle_; }

    // Called on actual phone video, once per TCP cycle. Ignore frames from the
    // previous session while start() is joining it and the next start is pending.
    // Keep the last success after disconnect so a short session is not lost
    // between HMI polls. A TCP accept alone must not replenish recovery.
    bool confirmVideo() {
        if (!tcp_ || starting_ || confirmedCycle_ == cycle_) return false;
        confirmedCycle_ = cycle_;
        return true;
    }

    void waitForSpp(bool waiting, uint64_t now) {
        if (tcp_ || starting_) waiting = false;
        if (waiting && (!waitingSpp_ || now < waitingSince_)) waitingSince_ = now;
        waitingSpp_ = waiting;
    }

    unsigned sppWaitSeconds(uint64_t now) const {
        if (!waitingSpp_ || tcp_ || starting_ || now < waitingSince_) return 0;
        uint64_t age = now - waitingSince_;
        return age < 1000000u ? (unsigned)age : 1000000u;
    }

    // A completed setup belongs to the cycle captured before its handshake. A
    // later accepted TCP connection invalidates that attempt, even if the new
    // session has already ended before the wireless worker reports its result.
    bool failed(unsigned attemptCycle) {
        if (attemptCycle != cycle_ || tcp_ || starting_ || failures_ >= 1000000u) return false;
        ++failures_;
        return true;
    }

    void beginTcp() {
        ++cycle_;
        failures_ = 0;
        starting_ = tcp_ = true;
        waitingSpp_ = false;
    }

    void finishTcp(bool active) {
        starting_ = false;
        tcp_ = active;
    }

    void endTcp() {
        tcp_ = false;
        // AASession::start() joins the old session. Its end callback must not
        // remove the guard protecting the new TCP connection being initialized.
    }

    bool canRecover(unsigned requestCycle, bool sessionActive, uint64_t now = 0) const {
        return requestCycle == cycle_ && (failures_ >= kReconnectFailedSetups ||
               sppWaitSeconds(now) >= kReconnectMissingSppSeconds) &&
               !tcp_ && !starting_ && !sessionActive;
    }

private:
    unsigned failures_, cycle_;
    bool tcp_, starting_;
    bool waitingSpp_;
    uint64_t waitingSince_;
    unsigned confirmedCycle_;
};

} // namespace nbt

#endif // NBT_SETUP_PROGRESS_HPP
