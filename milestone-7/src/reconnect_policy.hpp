// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef NBT_RECONNECT_POLICY_HPP
#define NBT_RECONNECT_POLICY_HPP

#include <stdint.h>
#include "reconnect_limits.hpp"

namespace nbt {

// HMI-owned policy: evaluate from the app's existing status poll, using seconds
// from CLOCK_MONOTONIC. setupFailures counts genuine completed wireless setup
// attempts without TCP; setupCycle changes when a TCP session starts. Both are
// daemon-local counters, so connection stability is scoped to the daemon PID.
// A true result authorizes ONE controlled daemon restart. It does not perform it.
// Restart budget survives daemon replacement, disabled polling and manual Stop.
class ReconnectPolicy {
public:
    ReconnectPolicy() : spent_(false), haveClock_(false), lastNow_(0), blockedUntil_(0) {
        clearObservation();
    }

    // Call only for an explicit user Start/Restart, never for an automatic launch.
    void manualStart() {
        spent_ = false;
        haveClock_ = false;
        blockedUntil_ = 0;
        clearObservation();
    }

    // Distinct CDS power-state observations rearm the episode, without assuming
    // enum values mean ignition ON/OFF. Manual Stop remains an independent guard.
    void powerChanged(uint64_t now) {
        spent_ = false;
        clearObservation();
        haveClock_ = true;
        lastNow_ = now;
        blockedUntil_ = now + kReconnectPowerSettleSeconds;
    }

    bool evaluate(uint64_t now, bool enabled, bool manuallyStopped, bool stopping,
                  int pid, bool connected, unsigned setupFailures, unsigned setupCycle,
                  unsigned sppWaitSeconds = 0,
                  unsigned sppWaitThreshold = kReconnectMissingSppSeconds) {
        if (haveClock_ && now < lastNow_) {
            clearObservation();
            if (blockedUntil_) blockedUntil_ = now + kReconnectPowerSettleSeconds;
        }
        haveClock_ = true;
        lastNow_ = now;

        if (!enabled || manuallyStopped || stopping || pid <= 0) {
            clearObservation();
            return false;
        }

        // A new process, new TCP cycle or reset failure counter begins a fresh
        // observation, but never replenishes the restart budget.
        if (pid != pid_ || setupCycle != cycle_ || setupFailures < failures_) {
            clearObservation();
        }
        pid_ = pid;
        cycle_ = setupCycle;
        failures_ = setupFailures;

        if (connected) {
            if (!trackingConnected_) {
                trackingConnected_ = true;
                connectedSince_ = now;
            } else if (now - connectedSince_ >= kStableConnectedSeconds) {
                spent_ = false;
            }
            return false;
        }

        trackingConnected_ = false;
        // Setup already gave the phone its normal TCP grace window. Do not add
        // another fixed wait after the second completed failure.
        if (spent_ || now < blockedUntil_ ||
            (setupFailures < kReconnectFailedSetups &&
             sppWaitSeconds < reconnectBoundedSeconds(sppWaitThreshold))) return false;

        spent_ = true;
        return true;
    }

private:
    enum { kStableConnectedSeconds = 30 };

    void clearObservation() {
        pid_ = 0;
        cycle_ = failures_ = 0;
        trackingConnected_ = false;
        connectedSince_ = 0;
    }

    bool spent_, haveClock_;
    uint64_t lastNow_;
    uint64_t blockedUntil_;
    int pid_;
    unsigned cycle_, failures_;
    bool trackingConnected_;
    uint64_t connectedSince_;
};

// Quiet relaunch is separate from manual Start. Cancellation never launches a
// daemon; an external/manual launch during the wait must also cancel it.
class ReconnectLaunchDelay {
public:
    ReconnectLaunchDelay() : pending_(false), due_(0), observed_(0), seconds_(kReconnectQuietSeconds) {}
    void schedule(uint64_t now, unsigned seconds = kReconnectQuietSeconds) {
        seconds_ = reconnectBoundedSeconds(seconds);
        pending_ = true;
        observed_ = now;
        due_ = now + seconds_;
    }
    void cancel() { pending_ = false; }
    bool pending() const { return pending_; }
    bool ready(uint64_t now, bool enabled, bool manuallyStopped, bool daemonPresent) {
        if (!pending_) return false;
        if (!enabled || manuallyStopped || daemonPresent) { cancel(); return false; }
        if (now < observed_) { schedule(now, seconds_); return false; }
        observed_ = now;
        if (now < due_) return false;
        cancel();
        return true;
    }
private:
    bool pending_;
    uint64_t due_, observed_;
    unsigned seconds_;
};

} // namespace nbt

#endif // NBT_RECONNECT_POLICY_HPP
