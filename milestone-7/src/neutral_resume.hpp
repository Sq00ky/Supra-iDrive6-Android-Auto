// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
//
// Manual gearbox: reverse -> neutral (handbrake on, no P) leaves iDrive's park-assist view up, because park assist
// only ends when the car drives off or the driver closes it. An automatic ends it by going to P. NBT_NEUTRAL_RESUME_S
// (seconds, default 5, 0 = off) brings Android Auto back once the car has sat in neutral that long straight after
// reverse. Pure state machine so it can be host-tested (tests/test_neutral_resume.cpp).
#ifndef NBT_NEUTRAL_RESUME_HPP
#define NBT_NEUTRAL_RESUME_HPP

#include <stdint.h>

namespace nbt {

class NeutralResume {
public:
    NeutralResume() : fromReverse_(false), timing_(false), fired_(false), since_(0) {}

    // Call every main-loop tick. Returns true once, on the tick the car has been in neutral for delayMs since it
    // came out of reverse. delayMs == 0 disables (always false). Any gear other than R or N cancels; going back
    // into reverse starts over.
    bool update(int gear, int reverseGear, int neutralGear, uint32_t nowMs, uint32_t delayMs) {
        if (delayMs == 0) { reset(); return false; }
        if (gear == reverseGear) {
            fromReverse_ = true;
            timing_ = false;
            fired_ = false;
            return false;
        }
        if (gear != neutralGear || !fromReverse_) { reset(); return false; }
        if (fired_) return false;
        if (!timing_) { timing_ = true; since_ = nowMs; return false; }
        if ((uint32_t)(nowMs - since_) >= delayMs) { fired_ = true; return true; }
        return false;
    }

    // In neutral straight after reverse, timer running (for the log).
    bool waiting() const { return timing_ && !fired_; }

private:
    void reset() { fromReverse_ = false; timing_ = false; fired_ = false; since_ = 0; }
    bool fromReverse_;
    bool timing_;
    bool fired_;
    uint32_t since_;
};

} // namespace nbt

#endif // NBT_NEUTRAL_RESUME_HPP
