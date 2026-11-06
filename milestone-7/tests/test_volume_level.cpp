// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test: car::VolumeLevel, the AudioMaster level decoder.
// g++ -std=gnu++98 -Wall -Wextra -Iinclude -Isrc -o /tmp/test_volume_level tests/test_volume_level.cpp && /tmp/test_volume_level
#include "car/volume_level.hpp"
#include <assert.h>
#include <stdio.h>

using nbt::car::VolumeLevel;

int main() {
    {   // Car capture 2026-11-03 (volcap.log on the car): AudioMaster FBlock 0x20 fkt 0x0C08, 5 bytes
        // 01 00 LEVEL 00 MAX. Up presses walked 8,9,11,12,13,15; down presses 13,12,11,9,8,7; MAX is 0x64.
        VolumeLevel v;
        const int up[]   = { 8, 9, 11, 12, 13, 15 };
        const int down[] = { 13, 12, 11, 9, 8, 7 };
        uint32_t t = 1000;
        int lv = -1, mx = -1;
        for (int i = 0; i < 6; ++i, t += 300) {
            uint8_t m[5] = { 0x01, 0x00, (uint8_t)up[i], 0x00, 0x64 };
            assert(v.feed(m, 5, t, lv, mx) && lv == up[i] && mx == 100);
        }
        uint8_t dup[5] = { 0x01, 0x00, 0x0F, 0x00, 0x64 };            // the same level again at once: dropped
        assert(!v.feed(dup, 5, t - 300 + 6, lv, mx));
        assert(v.feed(dup, 5, t - 300 + 40, lv, mx) && lv == 15);   // ...but not once kDupMs has passed
        for (int i = 0; i < 6; ++i, t += 300) {
            uint8_t m[5] = { 0x01, 0x00, (uint8_t)down[i], 0x00, 0x64 };
            assert(v.feed(m, 5, t, lv, mx) && lv == down[i] && mx == 100);
        }
        printf("test_volume_level car capture OK\n");
    }
    {   // Not a level message.
        VolumeLevel v;
        int lv = -1, mx = -1;
        uint8_t bad[5] = { 0x01, 0x00, 0x05, 0x00, 0x00 };            // MAX 0
        assert(!v.feed(bad, 5, 0, lv, mx));
        assert(!v.feed(bad, 4, 0, lv, mx));                           // short payload
        assert(!v.feed(NULL, 5, 0, lv, mx));
        uint8_t over[5] = { 0x01, 0x00, 0x65, 0x00, 0x64 };           // level above MAX
        assert(!v.feed(over, 5, 0, lv, mx));
        assert(lv == -1 && mx == -1);                                 // untouched
    }
    {   // reset: the next level is new even if it repeats the last one.
        VolumeLevel v;
        int lv, mx;
        uint8_t m[5] = { 0x01, 0x00, 0x0A, 0x00, 0x64 };
        assert(v.feed(m, 5, 100, lv, mx));
        assert(!v.feed(m, 5, 110, lv, mx));
        v.reset();
        assert(v.feed(m, 5, 120, lv, mx) && lv == 10);
    }
    {   // percent, as the overlay shows it.
        assert(VolumeLevel::percent(15, 100) == 15);
        assert(VolumeLevel::percent(20, 40) == 50);
        assert(VolumeLevel::percent(7, 0) == 7);                      // max 0 counts as 100
        assert(VolumeLevel::percent(120, 100) == 100);
        assert(VolumeLevel::percent(-3, 100) == 0);
    }
    printf("test_volume_level OK\n");
    return 0;
}
