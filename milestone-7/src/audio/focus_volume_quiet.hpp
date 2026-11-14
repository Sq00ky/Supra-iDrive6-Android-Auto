// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef AUDIO_FOCUS_VOLUME_QUIET_HPP
#define AUDIO_FOCUS_VOLUME_QUIET_HPP

// Car volume changes while a nav prompt focus build or remove is in flight, or within kWindowMs after, come from the
// focus, not the driver. Thread-safe; timestamps are monotonic ms.

#include <pthread.h>
#include <stdint.h>

namespace nbt {

class FocusVolumeQuiet {
public:
    static const uint32_t kWindowMs = 1500;

    FocusVolumeQuiet() : busy_(false), marked_(false), last_ms_(0) { pthread_mutex_init(&lock_, NULL); }
    ~FocusVolumeQuiet() { pthread_mutex_destroy(&lock_); }

    // busy = a focus build or remove is queued or running; the window starts when that ends.
    void setBusy(bool busy, uint64_t now_ms) {
        pthread_mutex_lock(&lock_);
        if (busy_ && !busy) {
            marked_ = true;
            last_ms_ = now_ms;
        }
        busy_ = busy;
        pthread_mutex_unlock(&lock_);
    }

    bool quiet(uint64_t now_ms) const {
        pthread_mutex_lock(&lock_);
        const bool q = busy_ || (marked_ && now_ms <= last_ms_ + kWindowMs);
        pthread_mutex_unlock(&lock_);
        return q;
    }

private:
    FocusVolumeQuiet(const FocusVolumeQuiet&);
    FocusVolumeQuiet& operator=(const FocusVolumeQuiet&);

    bool busy_;
    bool marked_;
    uint64_t last_ms_;
    mutable pthread_mutex_t lock_;
};

// The daemon-wide instance; defined in speech_out.cpp.
FocusVolumeQuiet& focusVolumeQuiet();

} // namespace nbt

#endif // AUDIO_FOCUS_VOLUME_QUIET_HPP
