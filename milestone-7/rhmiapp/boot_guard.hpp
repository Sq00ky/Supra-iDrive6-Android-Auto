// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
//
// "Start with the car" bootloop guard: pure decision logic (host-tested in tests/test_boot_guard.cpp).
//
// The guard file holds one stamp per autostart. It only ever holds boots that went wrong: the daemon deletes it after
// 10 min of uptime or on a clean exit, menu Start deletes it, and the iDrive app deletes it once it has stayed up
// kSettledS after an autostart. So the file is a boot counter first; the time window is only there to age out stamps
// from a working clock.
//
// Many head units keep the RTC unset at boot (slog and file times at Jan 01 1980, issue #84; the cluster still shows the
// right time). A stamp from an unset clock, or any stamp while the clock is unset now, has no age and always counts.
// Ignoring those boots, as rhmiapp-20..31 did, meant the guard never tripped on exactly the cars that loop.
#ifndef NBT_AAD_BOOT_GUARD_HPP
#define NBT_AAD_BOOT_GUARD_HPP

namespace nbt {
namespace bootguard {

const long kClockValidFrom = 1577836800L;   // 2020-01-01: earlier = RTC not set
const long kSettledS = 5 * 60;              // up this long after an autostart: that boot was not part of a loop

inline bool clockValid(long t) { return t >= kClockValidFrom; }

// Keeps the recorded autostarts that still count against this boot. Writes up to maxOut of them to out, oldest first;
// returns how many. With a valid clock now: a valid stamp counts within `window` seconds before `now` (future stamps
// are dropped, they come from a clock that was later corrected); an unset-clock stamp always counts. With the clock
// unset now: every stamp counts.
inline int recent(const long* in, int n, long now, long window, long* out, int maxOut) {
    int k = 0;
    for (int i = 0; i < n && k < maxOut; ++i) {
        bool counts = !clockValid(now) || !clockValid(in[i]) || (in[i] <= now && now - in[i] < window);
        if (counts) out[k++] = in[i];
    }
    return k;
}

// true = this boot may autostart.
inline bool allows(int recentCount, int maxBoots) { return recentCount < maxBoots; }

}  // namespace bootguard
}  // namespace nbt

#endif
