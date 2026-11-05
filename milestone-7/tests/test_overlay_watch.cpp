// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test for rhmiapp/overlay_watch.hpp: "Show iDrive warnings" driven by hmi.graphicalContext.
// Context ids from the F31 / NBTevo_Y24073J bench (2026-11-05): 681 Check Control (LT_ScrollLabel_CCM_Details),
// 20 Media/Radio (the menu under Android Auto when the hold does not take), 692 Top View camera (Supra log 3).
#include "../rhmiapp/overlay_watch.hpp"
#include <assert.h>
#include <stdio.h>

using nbt::OverlayWatch;

static const bool AA = true, NO_AA = false;

int main() {
    // 1. Check Control appears with no driver input: step aside on the first poll >= 300 ms later, not after 25 s.
    {
        OverlayWatch w;
        w.onContext(20, false, false, NO_AA, 1000);
        assert(w.poll(1500, AA, false, false) == OverlayWatch::NONE);   // 20 is just where iDrive is
        w.onContext(681, false, false, AA, 10000);
        assert(w.poll(10100, AA, false, false) == OverlayWatch::NONE); // < 300 ms: a hard key may still show up
        assert(w.poll(10400, AA, false, false) == OverlayWatch::STEP_ASIDE);
        assert(w.aside() == 681 && w.asideFrom() == 20);
        // 2. 681/20 ping-pong (~0.6 s) while the message is up: stay aside, no return.
        w.onContext(20, false, false, AA, 11000);
        assert(w.poll(11500, NO_AA, false, false) == OverlayWatch::NONE);
        w.onContext(681, false, false, AA, 11600);
        assert(w.poll(12000, NO_AA, false, false) == OverlayWatch::NONE);
        w.onContext(20, false, false, AA, 12200);
        w.onContext(681, false, false, AA, 12800);
        assert(w.poll(14000, NO_AA, false, false) == OverlayWatch::NONE);
        // 3. Message dismissed: back on 20 for 2 s -> Android Auto comes back once.
        w.onContext(20, false, true, AA, 20000);                             // dismissed with the knob: still a return
        assert(w.poll(21000, NO_AA, true, false) == OverlayWatch::NONE);
        assert(w.poll(22000, NO_AA, false, false) == OverlayWatch::COME_BACK);
        assert(!w.isAside());
        assert(w.poll(23000, NO_AA, false, false) == OverlayWatch::NONE); // once
    }
    // 4. A context the driver opened (input seen at the event or at the poll) is not a car overlay.
    {
        OverlayWatch w;
        w.onContext(20, false, false, NO_AA, 0);
        w.onContext(3, false, true, AA, 1000);
        assert(w.poll(1500, AA, false, false) == OverlayWatch::DRIVER);
        w.onContext(1296, false, false, AA, 5000);
        assert(w.poll(5500, AA, true, false) == OverlayWatch::DRIVER);
    }
    // 5. Our own moves (event 52, show, entry) are not overlays for 2 s; neither are 13, 10 or ignored ids.
    {
        OverlayWatch w;
        w.addIgnore(1856);
        w.onContext(20, false, false, NO_AA, 0);
        w.own(1000);
        w.onContext(55, false, false, AA, 1500);
        assert(w.poll(2500, AA, false, false) == OverlayWatch::NONE);
        w.onContext(13, false, false, AA, 4000);
        w.onContext(10, false, false, AA, 5000);
        w.onContext(1856, false, false, AA, 6000);
        assert(w.poll(7000, AA, false, false) == OverlayWatch::NONE);
    }
    // 6. GFXIDENTIFIER_INVALID is a transition (692 INVALID, then 692 TopViewCamera): only the real widget counts.
    {
        OverlayWatch w;
        w.onContext(20, false, false, NO_AA, 0);
        w.onContext(692, true, false, AA, 1000);
        assert(w.poll(2000, AA, false, false) == OverlayWatch::NONE);
        w.onContext(692, false, false, AA, 2100);
        assert(w.poll(2500, AA, false, false) == OverlayWatch::STEP_ASIDE);
        assert(w.asideFrom() == 20);
    }
    // 7. Reverse / park assist / a call: the daemon or the call logic owns the screen - no step-aside, no return.
    {
        OverlayWatch w;
        w.onContext(20, false, false, NO_AA, 0);
        w.onContext(692, false, false, AA, 1000);
        assert(w.poll(2000, AA, false, true) == OverlayWatch::BLOCKED);
        w.onContext(681, false, false, AA, 3000);
        assert(w.poll(3500, AA, false, false) == OverlayWatch::STEP_ASIDE);
        w.onContext(20, false, false, AA, 4000);
        assert(w.poll(7000, NO_AA, false, true) == OverlayWatch::NONE);  // blocked: wait
        assert(w.poll(7500, NO_AA, false, false) == OverlayWatch::COME_BACK);
    }
    // 8. A context that is gone again before the poll (a flash) does nothing.
    {
        OverlayWatch w;
        w.onContext(20, false, false, NO_AA, 0);
        w.onContext(681, false, false, AA, 1000);
        w.onContext(20, false, false, AA, 1200);
        assert(w.poll(2000, AA, false, false) == OverlayWatch::NONE);
    }
    // 9. Overlay closes onto some other screen (driver went on in iDrive): stay aside, the entry brings AA back.
    //    Android Auto back on screen (entry chosen) clears the aside state; reset() does it at once.
    {
        OverlayWatch w;
        w.onContext(20, false, false, NO_AA, 0);
        w.onContext(681, false, false, AA, 1000);
        assert(w.poll(1500, AA, false, false) == OverlayWatch::STEP_ASIDE);
        w.onContext(3, false, true, AA, 5000);
        assert(w.poll(9000, NO_AA, false, false) == OverlayWatch::NONE);
        assert(w.isAside());
        assert(w.poll(9500, AA, false, false) == OverlayWatch::NONE);   // AA up again 8 s after: aside over
        assert(!w.isAside());
        w.onContext(681, false, false, AA, 10000);
        assert(w.poll(10500, AA, false, false) == OverlayWatch::STEP_ASIDE);
        w.reset(11000);
        assert(!w.isAside());
    }
    // 10. Return to our own blank state (13) or our menu (10) also counts as "overlay closed".
    {
        OverlayWatch w;
        w.onContext(1171, false, false, NO_AA, 0);
        w.onContext(681, false, false, AA, 1000);
        assert(w.poll(1500, AA, false, false) == OverlayWatch::STEP_ASIDE);
        w.onContext(13, false, false, AA, 3000);
        assert(w.poll(5000, NO_AA, false, false) == OverlayWatch::COME_BACK);
    }
    printf("test_overlay_watch: all passed\n");
    return 0;
}
