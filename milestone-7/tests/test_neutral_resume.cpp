// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test: reverse -> neutral resume timer (src/neutral_resume.hpp). Car codes: 3 P, 1 N, 2 R, 5 D.
#include "neutral_resume.hpp"
#include <assert.h>
#include <stdio.h>
using namespace nbt;

static const int R = 2, N = 1, P = 3, D = 5;
static const uint32_t kDelay = 5000;

int main() {
    // R -> N, held 5 s: fires once, at 5 s.
    {
        NeutralResume nr;
        assert(!nr.update(R, R, N, 0, kDelay));
        assert(!nr.update(N, R, N, 1000, kDelay));
        assert(nr.waiting());
        assert(!nr.update(N, R, N, 5999, kDelay));
        assert(nr.update(N, R, N, 6000, kDelay));
        assert(!nr.waiting());
        assert(!nr.update(N, R, N, 7000, kDelay));   // only once
        assert(!nr.update(N, R, N, 60000, kDelay));
    }
    // N without reverse first (car started in neutral): never.
    {
        NeutralResume nr;
        for (uint32_t t = 0; t < 20000; t += 250) assert(!nr.update(N, R, N, t, kDelay));
    }
    // R -> N -> D within 5 s (automatic passing through N): cancelled.
    {
        NeutralResume nr;
        nr.update(R, R, N, 0, kDelay);
        nr.update(N, R, N, 500, kDelay);
        assert(!nr.update(D, R, N, 1500, kDelay));
        assert(!nr.update(N, R, N, 2000, kDelay));   // N again, but not straight after reverse
        assert(!nr.update(N, R, N, 9000, kDelay));
    }
    // R -> P: never (automatic; park assist ends by itself).
    {
        NeutralResume nr;
        nr.update(R, R, N, 0, kDelay);
        for (uint32_t t = 250; t < 20000; t += 250) assert(!nr.update(P, R, N, t, kDelay));
    }
    // R -> N -> R -> N: timer restarts from the second N.
    {
        NeutralResume nr;
        nr.update(R, R, N, 0, kDelay);
        nr.update(N, R, N, 1000, kDelay);
        nr.update(R, R, N, 4000, kDelay);
        assert(!nr.update(N, R, N, 4500, kDelay));
        assert(!nr.update(N, R, N, 9000, kDelay));
        assert(nr.update(N, R, N, 9500, kDelay));
    }
    // After firing, a new reverse arms it again.
    {
        NeutralResume nr;
        nr.update(R, R, N, 0, kDelay);
        nr.update(N, R, N, 100, kDelay);
        assert(nr.update(N, R, N, 5100, kDelay));
        nr.update(R, R, N, 8000, kDelay);
        nr.update(N, R, N, 9000, kDelay);
        assert(nr.update(N, R, N, 14000, kDelay));
    }
    // Off (0): never.
    {
        NeutralResume nr;
        nr.update(R, R, N, 0, 0);
        for (uint32_t t = 0; t < 20000; t += 250) assert(!nr.update(N, R, N, t, 0));
    }
    // Uptime wrap.
    {
        NeutralResume nr;
        nr.update(R, R, N, 0xFFFFF000u, kDelay);
        nr.update(N, R, N, 0xFFFFF800u, kDelay);
        assert(!nr.update(N, R, N, 0x00000100u, kDelay));
        assert(nr.update(N, R, N, 0x00001000u, kDelay));
    }
    printf("test_neutral_resume OK\n");
    return 0;
}
