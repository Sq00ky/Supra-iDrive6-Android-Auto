// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test: when browser mode builds and releases the browser audio source (audio/route_policy.hpp).
#include "audio/route_policy.hpp"
#include <stdio.h>
#include <string.h>

using namespace nbt;

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

// A policy taken to ROUTED at `t` (built at t - 100).
static RoutePolicy routedAt(uint64_t t, const RoutePolicy::Config& cfg = RoutePolicy::Config()) {
    RoutePolicy p;
    p.configure(cfg);
    p.tryBuild(t - 100);
    p.granted(t);
    return p;
}

int main() {
    // 1. Defaults match the browser mode's constants and settings.
    {
        RoutePolicy::Config c;
        CHECK(c.grant_timeout_ms == 75000);
        CHECK(c.release_timeout_ms == 20000);
        CHECK(c.backoff_ms == 60000);
        CHECK(c.idle_s == 10);
        CHECK(c.prompt_idle_ms == 1000);
        RoutePolicy p;
        CHECK(p.state() == RoutePolicy::IDLE);
        CHECK(!p.routed());
    }

    // 2. Build, grant, release, released: the normal cycle.
    {
        RoutePolicy p;
        CHECK(p.tryBuild(1000));
        CHECK(p.state() == RoutePolicy::BUILDING && p.since() == 1000);
        CHECK(!p.tryBuild(1001));                                 // already building
        CHECK(p.granted(1500) == RoutePolicy::BUILDING);
        CHECK(p.routed() && p.since() == 1500);
        CHECK(!p.tryBuild(1600));                                 // already routed
        CHECK(p.granted(1700) == RoutePolicy::ROUTED);            // a refocus build granted again
        CHECK(p.routed() && p.since() == 1500);                   // since unchanged
        CHECK(p.startRelease(2000));
        CHECK(p.state() == RoutePolicy::RELEASING && p.since() == 2000);
        CHECK(!p.startRelease(2001));                             // a remove is already on its way
        p.released();
        CHECK(p.state() == RoutePolicy::IDLE);
        CHECK(!p.startRelease(2100));                             // idle: nothing to release
        CHECK(p.tryBuild(2200));
    }

    // 3. No build while releasing: the build must never overtake the queued remove.
    {
        RoutePolicy p = routedAt(1000);
        CHECK(p.startRelease(2000));
        CHECK(!p.tryBuild(2001));
        CHECK(!p.tryBuild(30000));
        CHECK(p.granted(2500) == RoutePolicy::RELEASING);         // a late grant of the old build: ignored
        CHECK(p.state() == RoutePolicy::RELEASING);
        p.released();
        CHECK(p.tryBuild(30001));
    }

    // 4. A release while still building (session ended before the grant).
    {
        RoutePolicy p;
        CHECK(p.tryBuild(1000));
        CHECK(p.startRelease(1200));
        CHECK(p.state() == RoutePolicy::RELEASING);
        CHECK(p.granted(1300) == RoutePolicy::RELEASING);
        p.released();
        CHECK(p.granted(1400) == RoutePolicy::IDLE);              // granted after it was released: still idle
        CHECK(p.state() == RoutePolicy::IDLE);
    }

    // 5. A queue failure puts it back to idle.
    {
        RoutePolicy p;
        CHECK(p.tryBuild(1000));
        p.buildFailed();
        CHECK(p.state() == RoutePolicy::IDLE);
        CHECK(p.tryBuild(1001));
    }

    // 6. Grant timeout after 75 s (strictly more), then a 60 s backoff before the next build.
    {
        RoutePolicy p;
        CHECK(p.tryBuild(1000));
        CHECK(p.decide(1000 + 75000, true, 0, 0) == RoutePolicy::NONE);
        CHECK(p.decide(1000 + 75001, true, 0, 0) == RoutePolicy::GRANT_TIMEOUT);
        CHECK(p.decide(1000 + 75001, false, 0, 0) == RoutePolicy::GRANT_TIMEOUT);   // active or not
        // What the mixer then does:
        CHECK(p.startRelease(76001));
        p.backoff(76002);
        CHECK(p.backoffUntil() == 76002 + 60000);
        p.released();
        CHECK(!p.tryBuild(76002 + 59999));
        CHECK(p.state() == RoutePolicy::IDLE);
        CHECK(p.tryBuild(76002 + 60000));
        // Session end clears the backoff.
        RoutePolicy q;
        q.backoff(1000);
        CHECK(!q.tryBuild(2000));
        q.clearBackoff();
        CHECK(q.tryBuild(2000));
    }

    // 7. Release timeout: a remove that never reports back is given up after 20 s.
    {
        RoutePolicy p = routedAt(1000);
        CHECK(p.startRelease(5000));
        CHECK(p.decide(5000 + 20000, false, 0, 0) == RoutePolicy::NONE);
        CHECK(p.decide(5000 + 20001, false, 0, 0) == RoutePolicy::RELEASE_TIMEOUT);
        p.released();
        CHECK(p.state() == RoutePolicy::IDLE);
        CHECK(p.decide(5000 + 20002, false, 0, 0) == RoutePolicy::NONE);
    }

    // 8. After AA media the source is kept NBT_BROWSER_IDLE_S (10 s), so resuming doesn't rebuild it.
    {
        RoutePolicy p = routedAt(1000);
        const uint64_t sound = 50000;
        // Media and sound ended together: kept while the media is recent (under 10 s ago) ...
        CHECK(p.decide(sound + 1001, false, sound, sound) == RoutePolicy::NONE);
        CHECK(p.decide(sound + 9999, false, sound, sound) == RoutePolicy::NONE);
        // ... and from 10 s on the media no longer counts as recent, so the prompt rule (quiet > 1 s) releases it.
        CHECK(p.decide(sound + 10000, false, sound, sound) == RoutePolicy::RELEASE_PROMPT);
        // A media stream still open but silent (last_media later than last_sound): released once quiet > 10 s.
        CHECK(p.decide(sound + 10000, false, sound, sound + 5000) == RoutePolicy::NONE);
        CHECK(p.decide(sound + 10001, false, sound, sound + 5000) == RoutePolicy::RELEASE_IDLE);
        // Something still playing or queued: never released.
        CHECK(p.decide(sound + 60000, true, sound, sound + 59000) == RoutePolicy::NONE);
        CHECK(p.decide(sound + 60000, true, sound, sound) == RoutePolicy::NONE);
    }

    // 9. Prompt or sound alone (no AA media this session): released NBT_BROWSER_PROMPT_IDLE_MS (1 s) after it ends.
    {
        RoutePolicy p = routedAt(1000);
        const uint64_t sound = 20000;
        CHECK(p.decide(sound + 1000, false, sound, 0) == RoutePolicy::NONE);
        CHECK(p.decide(sound + 1001, false, sound, 0) == RoutePolicy::RELEASE_PROMPT);
        CHECK(p.decide(sound + 1001, true, sound, 0) == RoutePolicy::NONE);
    }

    // 10. NBT_BROWSER_PROMPT_IDLE_MS=0: prompts too wait NBT_BROWSER_IDLE_S.
    {
        RoutePolicy::Config c;
        c.prompt_idle_ms = 0;
        RoutePolicy p = routedAt(1000, c);
        const uint64_t sound = 20000;
        CHECK(p.decide(sound + 5000, false, sound, 0) == RoutePolicy::NONE);
        CHECK(p.decide(sound + 10001, false, sound, 0) == RoutePolicy::RELEASE_IDLE);
    }

    // 11. NBT_BROWSER_IDLE_S=0: after media the source is kept until the session ends; prompts alone still release.
    {
        RoutePolicy::Config c;
        c.idle_s = 0;
        RoutePolicy p = routedAt(1000, c);
        CHECK(p.decide(10000000, false, 2000, 2000) == RoutePolicy::NONE);   // media this session: recent forever
        CHECK(p.decide(10000000, false, 2000, 0) == RoutePolicy::RELEASE_PROMPT);
        c.prompt_idle_ms = 0;
        RoutePolicy q = routedAt(1000, c);
        CHECK(q.decide(10000000, false, 2000, 0) == RoutePolicy::NONE);      // both 0: never released when idle
    }

    // 12. Not routed: no idle release whatever the timing.
    {
        RoutePolicy p;
        CHECK(p.decide(1000000, false, 0, 0) == RoutePolicy::NONE);
        CHECK(p.tryBuild(1000));
        CHECK(p.decide(2000, false, 0, 0) == RoutePolicy::NONE);
    }

    // 13. State names as the stats line prints them.
    CHECK(strcmp(RoutePolicy::stateName(RoutePolicy::IDLE), "idle") == 0);
    CHECK(strcmp(RoutePolicy::stateName(RoutePolicy::BUILDING), "requested") == 0);
    CHECK(strcmp(RoutePolicy::stateName(RoutePolicy::ROUTED), "granted") == 0);
    CHECK(strcmp(RoutePolicy::stateName(RoutePolicy::RELEASING), "releasing") == 0);

    // 14. NBT_BROWSER_HOLD=1: after media, a pause keeps the source however long, whatever NBT_BROWSER_IDLE_S says;
    //     prompts alone still go after NBT_BROWSER_PROMPT_IDLE_MS, and with that at 0 after NBT_BROWSER_IDLE_S.
    {
        RoutePolicy::Config c;
        CHECK(!c.hold);                                                      // off by default
        c.hold = true;
        RoutePolicy p = routedAt(1000, c);
        CHECK(p.decide(2000 + 10001, false, 2000, 2000) == RoutePolicy::NONE);
        CHECK(p.decide(2000 + 3600000, false, 2000, 2000) == RoutePolicy::NONE);   // an hour paused
        CHECK(p.decide(20000 + 1001, false, 20000, 0) == RoutePolicy::RELEASE_PROMPT);
        c.prompt_idle_ms = 0;
        RoutePolicy q = routedAt(1000, c);
        CHECK(q.decide(20000 + 5000, false, 20000, 0) == RoutePolicy::NONE);
        CHECK(q.decide(20000 + 10001, false, 20000, 0) == RoutePolicy::RELEASE_IDLE);
        CHECK(q.decide(20000 + 3600000, false, 2000, 2000) == RoutePolicy::NONE);
    }

    // 15. holdsMedia: media played and the source built or being built; not when idle or releasing.
    {
        RoutePolicy p;
        CHECK(!p.holdsMedia(5000));
        CHECK(p.tryBuild(1000));
        CHECK(p.holdsMedia(5000));
        CHECK(!p.holdsMedia(0));                                             // only a prompt built it
        p.granted(1100);
        CHECK(p.holdsMedia(5000));
        CHECK(p.startRelease(6000));
        CHECK(!p.holdsMedia(5000));
    }

    // 16. Yield: AA media alone no longer needs the source; prompts and sounds still do; endYield gives it back.
    {
        RoutePolicy p;
        CHECK(!p.yielded());
        CHECK(p.needsSource(true, false) && p.needsSource(false, true) && !p.needsSource(false, false));
        p.yieldToCar();
        CHECK(p.yielded());
        CHECK(!p.needsSource(true, false));                                  // late media frames: no rebuild
        CHECK(p.needsSource(false, true) && p.needsSource(true, true));      // a prompt on the radio: built for it
        CHECK(!p.needsSource(false, false));
        CHECK(p.endYield());
        CHECK(!p.yielded() && p.needsSource(true, false));
        CHECK(!p.endYield());                                                // not yielded: nothing to end
    }

    // 17. The whole MODE cycle with hold: media holds through a pause, MODE yields (released at once), a prompt on the
    //     radio builds and releases the source, play in AA (focus request) takes it back and holds it again.
    {
        RoutePolicy::Config c;
        c.hold = true;
        RoutePolicy p = routedAt(1000, c);
        CHECK(p.decide(500000, false, 2000, 2000) == RoutePolicy::NONE);    // paused for minutes: still held
        CHECK(p.holdsMedia(2000));
        p.yieldToCar();
        CHECK(p.startRelease(500001));
        p.released();
        CHECK(!p.needsSource(true, false));                                  // the phone's last media frames
        CHECK(p.needsSource(false, true) && p.tryBuild(510000));             // a Maps prompt on the radio
        p.granted(510100);
        CHECK(!p.holdsMedia(0));                                             // last_media reset at the yield
        CHECK(p.decide(513000 + 1001, false, 513000, 0) == RoutePolicy::RELEASE_PROMPT);
        CHECK(p.startRelease(514001));
        p.released();
        CHECK(p.endYield());                                                 // play pressed in AA
        CHECK(p.needsSource(true, false) && p.tryBuild(600000));
        p.granted(600100);
        CHECK(p.decide(600000 + 3600000, false, 600200, 600200) == RoutePolicy::NONE);   // held again
    }

    if (fails) { printf("test_route_policy: %d failure(s)\n", fails); return 1; }
    printf("test_route_policy OK\n");
    return 0;
}
