// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
//
// "Start with the car" bootloop guard: pure decision logic (host-tested in tests/test_boot_guard.cpp).
#ifndef NBT_AAD_BOOT_GUARD_HPP
#define NBT_AAD_BOOT_GUARD_HPP

namespace nbt {
namespace bootguard {

const long kClockValidFrom = 1577836800L;   // 2020-01-01: earlier = RTC not set, do not count

// Keeps the recorded autostart times that lie within `window` seconds before `now` (future stamps dropped, they come
// from a clock that was later corrected). Writes up to maxOut of them to out, oldest first; returns how many.
inline int recent(const long* in, int n, long now, long window, long* out, int maxOut) {
    int k = 0;
    for (int i = 0; i < n && k < maxOut; ++i)
        if (in[i] <= now && now - in[i] < window) out[k++] = in[i];
    return k;
}

// true = this boot may autostart.
inline bool allows(int recentCount, int maxBoots) { return recentCount < maxBoots; }

}  // namespace bootguard
}  // namespace nbt

#endif
