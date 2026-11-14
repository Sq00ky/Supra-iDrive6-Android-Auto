// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test: when the nav prompt focus is built, held and released (audio/nav_prompt_policy.hpp), and the volume
// pill quiet window around it (audio/focus_volume_quiet.hpp).
#include "audio/focus_volume_quiet.hpp"
#include "audio/nav_prompt_policy.hpp"
#include <stdio.h>

using namespace nbt;

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

int main() {
    // 1. Defaults.
    {
        NavPromptPolicy::Config c;
        CHECK(c.grant_timeout_ms == 2000);
        CHECK(c.hold_ms == 500);
        CHECK(c.backoff_ms == 30000);
        CHECK(c.release_timeout_ms == 5000);
        NavPromptPolicy p;
        CHECK(p.state() == NavPromptPolicy::IDLE);
        CHECK(!p.mayPlay());
        CHECK(p.available(0));
    }

    // 2. Normal cycle.
    {
        NavPromptPolicy p;
        CHECK(p.tryBuild(1000));
        CHECK(p.state() == NavPromptPolicy::BUILDING);
        CHECK(!p.mayPlay());
        CHECK(p.granted(1300) == NavPromptPolicy::BUILDING);
        CHECK(p.state() == NavPromptPolicy::ROUTED);
        CHECK(p.mayPlay());
        CHECK(p.decide(1400, true, 1400) == NavPromptPolicy::NONE);
        CHECK(p.decide(5000, true, 1000) == NavPromptPolicy::NONE);
        CHECK(p.decide(1900, false, 1400) == NavPromptPolicy::NONE);
        CHECK(p.decide(1901, false, 1400) == NavPromptPolicy::RELEASE);
        CHECK(p.startRelease(1901));
        CHECK(p.state() == NavPromptPolicy::RELEASING);
        p.released();
        CHECK(p.state() == NavPromptPolicy::IDLE);
    }

    // 3. Grant timeout, then backoff.
    {
        NavPromptPolicy p;
        CHECK(p.tryBuild(0));
        CHECK(p.decide(2000, true, 2000) == NavPromptPolicy::NONE);
        CHECK(p.decide(2001, true, 2001) == NavPromptPolicy::GRANT_TIMEOUT);
        CHECK(p.startRelease(2001));
        p.released();
        p.backoff(2001);
        CHECK(!p.available(31000));
        CHECK(p.available(32001));
        CHECK(!p.tryBuild(32000));
        CHECK(p.tryBuild(32001));
    }

    // 4. A new prompt within the hold time extends it without a rebuild.
    {
        NavPromptPolicy p;
        CHECK(p.tryBuild(900));
        p.granted(1000);
        CHECK(p.decide(1400, false, 1200) == NavPromptPolicy::NONE);
        CHECK(!p.tryBuild(1450));
        CHECK(p.mayPlay());
        CHECK(p.decide(1960, false, 1500) == NavPromptPolicy::NONE);
        CHECK(p.decide(2001, false, 1500) == NavPromptPolicy::RELEASE);
    }

    // 5. Session end releases from BUILDING and ROUTED only.
    {
        NavPromptPolicy p;
        CHECK(!p.startRelease(10));
        CHECK(p.tryBuild(100));
        CHECK(p.startRelease(200));
        CHECK(!p.startRelease(201));
        p.released();
        CHECK(p.tryBuild(300));
        p.granted(400);
        CHECK(p.startRelease(500));
        CHECK(!p.startRelease(501));
    }

    // 6. Release timeout.
    {
        NavPromptPolicy p;
        CHECK(p.tryBuild(0));
        CHECK(p.startRelease(0));
        CHECK(p.decide(5000, false, 0) == NavPromptPolicy::NONE);
        CHECK(p.decide(5001, false, 0) == NavPromptPolicy::RELEASE_TIMEOUT);
    }

    // 7. hold_ms = 0.
    {
        NavPromptPolicy::Config c;
        c.hold_ms = 0;
        NavPromptPolicy p;
        p.configure(c);
        CHECK(p.tryBuild(900));
        p.granted(1000);
        CHECK(p.decide(1000, false, 1000) == NavPromptPolicy::NONE);
        CHECK(p.decide(1001, false, 1000) == NavPromptPolicy::RELEASE);
    }

    // 8. Build failure and backoff clearing.
    {
        NavPromptPolicy p;
        CHECK(p.tryBuild(1000));
        p.buildFailed();
        CHECK(p.state() == NavPromptPolicy::IDLE);
        p.backoff(1000);
        CHECK(!p.tryBuild(2000));
        p.clearBackoff();
        CHECK(p.tryBuild(2000));
    }

    // 9. Volume pill: quiet while a focus change is in flight, however long, and 1500 ms after it, inclusive.
    {
        FocusVolumeQuiet q;
        CHECK(!q.quiet(0));
        q.setBusy(false, 500);   // nothing was in flight: no window
        CHECK(!q.quiet(500));
        q.setBusy(true, 1000);
        CHECK(q.quiet(1000));
        CHECK(q.quiet(9000));
        q.setBusy(true, 9500);   // still in flight: the window has not started
        q.setBusy(false, 10000);
        CHECK(q.quiet(11500));
        CHECK(!q.quiet(11501));
        q.setBusy(false, 12000);
        CHECK(!q.quiet(12000));
        q.setBusy(true, 13000);
        q.setBusy(false, 13100);
        CHECK(q.quiet(14600));
        CHECK(!q.quiet(14601));
    }

    if (fails) { printf("test_nav_prompt_policy: %d failure(s)\n", fails); return 1; }
    printf("test_nav_prompt_policy OK\n");
    return 0;
}
