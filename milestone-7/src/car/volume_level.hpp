// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef CAR_VOLUME_LEVEL_HPP
#define CAR_VOLUME_LEVEL_HPP

// The car's master volume: MOST FBlock 0x20 (AudioMaster) inst 00, fkt 0x0C08, Status, 5 bytes 01 00 LEVEL 00 MAX
// (MAX = 0x64 = 100 on a J29). Car-verified 2026-11-03 with tools/volcap.cpp: one message ~6 ms after each press of the
// steering-wheel volume key (FBlock 0x15 inst 02 fkt 0x0401, which is not needed: the level is what the amplifier
// actually applied, so an overlay following it cannot drift from what the driver hears). Up presses walked
// 8,9,11,12,13,15; down presses 13,12,11,9,8,7. The level repeats within a few ms, so the same (level, max) inside
// kDupMs is dropped. Copies only: the HMI still gets the message and draws its own bar.
// Pure: host-tested in tests/test_volume_level.cpp, from the time each message was received (most::View::rxMs).

#include <stddef.h>
#include <stdint.h>

namespace nbt {
namespace car {

class VolumeLevel {
public:
    enum { kFBlock = 0x20, kFkt = 0xC08, kDupMs = 40 };

    VolumeLevel() : level_(-1), max_(-1), ms_(0) {}

    // A Status payload of kFBlock/kFkt. True: a new level, in level and max (0 <= level <= max, max > 0).
    bool feed(const uint8_t* d, size_t n, uint32_t rxMs, int& level, int& max) {
        if (!d || n < 5) return false;
        // Byte 0 is the AudioMaster channel. 0x01 is main entertainment volume.
        // Ignore other channels (0x02 nav prompt, 0x03 phone, 0x06 gong/PDC) so guidance doesn't trigger OSD.
        if (d[0] != 0x01) return false;
        int lv = d[2], mx = d[4];
        if (mx <= 0 || lv > mx) return false;
        if (lv == level_ && mx == max_ && rxMs - ms_ < (uint32_t)kDupMs) return false;   // the same step, repeated
        level_ = lv; max_ = mx; ms_ = rxMs;
        level = lv; max = mx;
        return true;
    }

    // The messages stopped: the next level is new whatever it is.
    void reset() { level_ = -1; max_ = -1; }

    // 0..100 for a level of max; max <= 0 counts as 100.
    static int percent(int level, int max) {
        if (max <= 0) max = 100;
        int p = level * 100 / max;
        return p < 0 ? 0 : (p > 100 ? 100 : p);
    }

private:
    int level_;
    int max_;
    uint32_t ms_;
};

} // namespace car
} // namespace nbt

#endif
