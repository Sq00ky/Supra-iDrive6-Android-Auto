// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test for rhmiapp/boot_guard.hpp: the "Start with the car" bootloop guard.
#include "../rhmiapp/boot_guard.hpp"
#include <assert.h>
#include <stdio.h>

using namespace nbt::bootguard;

static const long kWindow = 15 * 60;
static const long kMax = 3;

// One boot of the iDrive app: read the file, decide, record. Returns whether it autostarted; the file is in/n.
static bool boot(long* file, int& n, long now) {
    long t[8];
    int k = recent(file, n, now, kWindow, t, 8);
    if (!allows(k, kMax)) return false;
    int from = k >= kMax ? k - kMax + 1 : 0;
    int m = 0;
    for (int i = from; i < k; ++i) file[m++] = t[i];
    file[m++] = now;
    n = m;
    return true;
}

int main() {
    const long y2026 = 1793836800L;   // 2026-11-04
    const long y1980 = 315532800L;    // what an unset RTC reads (Jan 01 1980)

    // Working clock: three resets inside 15 min, the fourth boot is skipped. Spread out, they age out.
    {
        long f[8]; int n = 0;
        assert(boot(f, n, y2026));
        assert(boot(f, n, y2026 + 60));
        assert(boot(f, n, y2026 + 120));
        assert(!boot(f, n, y2026 + 180));
        n = 0;
        assert(boot(f, n, y2026));
        assert(boot(f, n, y2026 + 20 * 60));
        assert(boot(f, n, y2026 + 40 * 60));
        assert(boot(f, n, y2026 + 60 * 60));
    }
    // Issue #84 (F31 bench, G30): the RTC reads 1980 at every boot. rhmiapp-31 never counted these boots; now each one
    // counts, and the fourth boot of the loop is skipped. Stamps 1980 + a few seconds, as the unset clock runs on.
    {
        long f[8]; int n = 0;
        assert(boot(f, n, y1980 + 40));
        assert(boot(f, n, y1980 + 41));
        assert(boot(f, n, y1980 + 39));                 // an unset clock may even read earlier than last boot
        assert(!boot(f, n, y1980 + 40));
        assert(!boot(f, n, y1980 + 4000));              // no window ages them out: only a reset of the file does
    }
    // Mixed: unset-clock stamps from earlier boots still count once the clock is set, and valid stamps count while it
    // is not (a clock that is unset only on some boots).
    {
        long f[8]; int n = 0;
        assert(boot(f, n, y1980 + 30));
        assert(boot(f, n, y1980 + 31));
        assert(boot(f, n, y2026 + 5000));               // clock set now: the third boot still runs...
        assert(!boot(f, n, y2026 + 5000 + 60));         // ...and the 1980 stamps have no age, so the fourth is skipped
        n = 0;
        assert(boot(f, n, y2026));
        assert(boot(f, n, y2026 + 30));
        assert(boot(f, n, y1980 + 30));
        assert(!boot(f, n, y1980 + 31));                // clock unset now: every stamp counts, however old
    }
    // Future stamps with a valid clock are dropped (a clock that was corrected backwards).
    {
        long in[3] = { y2026 + 9999, y2026 + 99999, y2026 - 10 }, out[8];
        assert(recent(in, 3, y2026, kWindow, out, 8) == 1 && out[0] == y2026 - 10);
    }
    // maxOut caps the copy.
    {
        long in[5] = { y1980, y1980, y1980, y1980, y1980 }, out[2];
        assert(recent(in, 5, y1980, kWindow, out, 2) == 2);
    }
    assert(!clockValid(y1980) && clockValid(y2026) && clockValid(kClockValidFrom));
    printf("test_boot_guard OK\n");
    return 0;
}
