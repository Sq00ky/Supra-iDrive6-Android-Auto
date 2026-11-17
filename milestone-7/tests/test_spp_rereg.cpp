// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test for src/spp_rereg.hpp: the re-registration schedule and the hold while the phone is asking.
#include "spp_rereg.hpp"
#include <assert.h>
#include <stdio.h>
using namespace nbt;

int main() {
    // Schedule: base interval for the first 2 minutes, then every 30 s; 0 = off; a long base stays as is.
    assert(sppReregIntervalSec(0, 5) == 5);
    assert(sppReregIntervalSec(23, 5) == 5);
    assert(sppReregIntervalSec(24, 5) == kSppReregSlowSec);
    assert(sppReregIntervalSec(3, 0) == 0);
    assert(sppReregIntervalSec(0, 60) == 60 && sppReregIntervalSec(9, 60) == 60);
    {
        SppReregTimer t;
        t.configure(5);
        t.reset(1000);
        assert(!t.due(1004));
        assert(t.due(1005) && t.attempts() == 1);
        assert(!t.due(1009) && t.due(1010));
    }
    // Hold: only after a request, for kSppReregHoldSec after the last one, wrap-safe.
    assert(!sppReregHeld(false, 0, 100));
    assert(sppReregHeld(true, 100, 100));
    assert(sppReregHeld(true, 100, 100 + kSppReregHoldSec - 1));
    assert(!sppReregHeld(true, 100, 100 + kSppReregHoldSec));
    assert(!sppReregHeld(true, 100, 5000));
    assert(sppReregHeld(true, 0xFFFFFFF0u, 5));          // monotonic seconds wrapped
    assert(!sppReregHeld(true, 100, 105, 0));            // hold 0 = never held
    // Only the serial-link request holds (A2DP / HFP come with every audio connect; J29 2026-11-16).
    assert(sppRequestHolds("SPP"));
    assert(!sppRequestHolds("A2DP_SOURCE") && !sppRequestHolds("HFPGW") && !sppRequestHolds(""));
    // NBT_SPP_REREG_HOLD_S: unset = 30, 0 / negative = no hold, clamped at 600.
    assert(sppReregHoldSetting(NULL) == kSppReregHoldSec && sppReregHoldSetting("") == kSppReregHoldSec);
    assert(sppReregHoldSetting("0") == 0 && sppReregHoldSetting("-4") == 0);
    assert(sppReregHoldSetting("45") == 45 && sppReregHoldSetting("99999") == 600);
    // Cap: one round per kSppHoldCapSec of continuous hold, wrap-safe; 0 = never.
    assert(!sppHoldCapDue(1000, 1000) && !sppHoldCapDue(1000, 1000 + kSppHoldCapSec - 1));
    assert(sppHoldCapDue(1000, 1000 + kSppHoldCapSec));
    assert(sppHoldCapDue(0xFFFFFFF0u, 0xFFFFFFF0u + kSppHoldCapSec));   // wraps past 0
    assert(!sppHoldCapDue(1000, 5000, 0));
    // A phone refused every 5 s for 10 min: with the cap, one round per minute (the simulation the referee ran gave 0).
    {
        uint32_t heldSince = 0;
        int rounds = 0;
        for (uint32_t now = 100; now < 100 + 600; ++now) {
            if (!heldSince) heldSince = now;
            if (sppHoldCapDue(heldSince, now)) { ++rounds; heldSince = now; }
        }
        assert(rounds == 600 / kSppHoldCapSec - 1 || rounds == 600 / kSppHoldCapSec);
    }
    printf("test_spp_rereg OK\n");
    return 0;
}
