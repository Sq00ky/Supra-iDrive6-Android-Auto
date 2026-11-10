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
    // 0. Camera contexts (rhmiapp-46): default 692,1023; NBT_AA_CAMERA_CTX replaces the list
    {
        int l[16];
        int n = nbt::parseCameraContexts(NULL, l, 16);
        assert(n == 2 && nbt::isCameraContext(1023, l, n) && nbt::isCameraContext(692, l, n));
        assert(!nbt::isCameraContext(3, l, n) && !nbt::isCameraContext(50002, l, n));
        n = nbt::parseCameraContexts("", l, 16);
        assert(n == 2);
        n = nbt::parseCameraContexts("1023, 1856 ,x,0,-5,700", l, 16);
        assert(n == 3 && l[0] == 1023 && l[1] == 1856 && l[2] == 700);
        n = nbt::parseCameraContexts("1,2,3,4", l, 2);
        assert(n == 2);
        printf("camera contexts OK\n");
    }
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
    // 11. The safety button twice (Supra, rhmiapp-36 log 2026-11-05): CDS reports our blank state as 50002 /
    //     GFXIDENTIFIER_INVALID, so iDrive coming back to it is known from state 13's focus notification. The second
    //     press of the same screen is a change again, and closing it onto 13 brings Android Auto back.
    {
        OverlayWatch w;
        w.onContext(20, false, false, NO_AA, 0);
        w.onOwnState(OverlayWatch::kAaState, 1000);                        // event 52 taken: focus on 13
        w.onContext(50002, true, false, AA, 1000);                         // its CDS echo: INVALID, ignored
        w.onContext(745, false, false, AA, 5000, false);                   // 'Toyota Supra Safety'
        assert(w.poll(5400, AA, false, false) == OverlayWatch::STEP_ASIDE);
        assert(w.aside() == 745 && w.asideFrom() == OverlayWatch::kAaState);
        w.onOwnState(OverlayWatch::kAaState, 9000);                        // menu closed: focus back on 13
        assert(w.poll(10000, NO_AA, false, false) == OverlayWatch::NONE);
        assert(w.poll(11000, NO_AA, false, false) == OverlayWatch::COME_BACK);
        w.onContext(745, false, false, AA, 20000, false);                  // pressed again: same id, still a change
        assert(w.poll(20400, AA, false, false) == OverlayWatch::STEP_ASIDE);
        // A pending screen iDrive leaves for 13 again before the poll was a flash.
        OverlayWatch f;
        f.onOwnState(OverlayWatch::kAaState, 0);
        f.onContext(745, false, false, AA, 5000, false);
        f.onOwnState(OverlayWatch::kAaState, 5100);
        assert(f.poll(5400, AA, false, false) == OverlayWatch::NONE);
    }
    // 12. A knob push (daemon overlay 5) cannot open a screen from our focused blank state: the safety button 1.4 s
    //     after a push is still the car's. Where 13 did not have the focus (a unit that never takes it) the push may have.
    {
        OverlayWatch w;
        w.onOwnState(OverlayWatch::kAaState, 0);
        w.onContext(745, false, false, AA, 5000, false);
        assert(w.poll(5400, AA, false, false, true) == OverlayWatch::STEP_ASIDE);
        OverlayWatch u;
        u.onContext(20, false, false, NO_AA, 0);
        u.onContext(55, false, true, AA, 5000, true);                      // knob counted at the event
        assert(u.poll(5400, AA, false, false, false) == OverlayWatch::DRIVER);
        OverlayWatch v;
        v.onContext(20, false, false, NO_AA, 0);
        v.onContext(55, false, false, AA, 5000, true);                     // push reached the state file after the event
        assert(v.poll(5400, AA, false, false, true) == OverlayWatch::DRIVER);
        // A key (overlay 3 / 4) counts wherever the focus was.
        OverlayWatch k;
        k.onOwnState(OverlayWatch::kAaState, 0);
        k.onContext(638, false, false, AA, 5000, false);
        assert(k.poll(5400, AA, true, false, false) == OverlayWatch::DRIVER);
    }
    // 13. A screen the watch let through - here 0.8 s after our own event 52 (car 2026-11-05: the safety menu came back
    //     at 147.370, event 52 at 146.615) - that then holds iDrive's focus with no input: the app steps aside for it
    //     with takeAside(); back on 13 for 2 s brings Android Auto back. Never for 13 / 10 / ignored ids, and only once.
    {
        OverlayWatch w;
        w.addIgnore(1856);
        w.onOwnState(OverlayWatch::kAaState, 0);
        assert(!w.takeAside(500));                                          // on our own state: nothing to do
        w.own(146615);
        w.onContext(745, false, false, AA, 147370, false);                 // inside the own-move window: taken as home
        assert(w.poll(147700, AA, false, false) == OverlayWatch::NONE);
        assert(w.takeAside(149000) && w.isAside() && w.aside() == 745 && w.asideFrom() == OverlayWatch::kAaState);
        assert(!w.takeAside(149500));                                       // once
        assert(w.poll(150000, NO_AA, false, false) == OverlayWatch::NONE); // still up
        w.onOwnState(OverlayWatch::kAaState, 159400);
        assert(w.poll(160000, NO_AA, false, false) == OverlayWatch::NONE);
        assert(w.poll(161400, NO_AA, false, false) == OverlayWatch::COME_BACK);
        w.onContext(1856, false, false, AA, 170000);                        // ignored id: never taken aside
        assert(w.poll(170400, AA, false, false) == OverlayWatch::NONE);
        assert(!w.takeAside(171000));
        w.onOwnState(OverlayWatch::kMenuState, 172000);
        assert(!w.takeAside(173000));
    }
    // 14. The Settings page (14) is part of our menu, not a car screen: focus there never causes a step-aside, and
    //     while aside it counts as a way back like 10 / 13 (rhmiapp-39).
    {
        OverlayWatch w;
        w.onOwnState(OverlayWatch::kSettingsState, 0);
        assert(!w.takeAside(500));
        w.onOwnState(OverlayWatch::kAaState, 1000);
        w.own(1200);
        w.onContext(692, false, false, AA, 1400, false);                   // inside the own-move window: taken as home
        assert(w.poll(1700, AA, false, false) == OverlayWatch::NONE);
        assert(w.takeAside(2000) && w.aside() == 692);
        w.onOwnState(OverlayWatch::kSettingsState, 3000);                  // driver went on into Settings
        assert(w.poll(4000, NO_AA, false, false) == OverlayWatch::NONE);
        assert(w.poll(5000, NO_AA, false, false) == OverlayWatch::COME_BACK);
    }
    // rhmiapp-51: GFXIDENTIFIER_INVALID with a title is a real iDrive screen (Max eDRIVE 'eDRIVE mode', 1534)
    assert(nbt::betweenScreens(1534, "GFXIDENTIFIER_INVALID", "") == true);
    assert(nbt::betweenScreens(1023, "GFXIDENTIFIER_INVALID", "") == true);
    assert(nbt::betweenScreens(50002, "GFXIDENTIFIER_INVALID", "Android Auto") == true);
    assert(nbt::betweenScreens(1534, "GFXIDENTIFIER_INVALID", "eDRIVE mode") == false);
    assert(nbt::betweenScreens(1534, "LT_Button_1Row", "eDRIVE mode") == false);
    assert(nbt::betweenScreens(20, "LT_Button_1Row_1CheckLeft_1IconLeft", "Media/Radio") == false);
    {
        // Max eDRIVE: only ever GFXIDENTIFIER_INVALID + title, no driver input, Android Auto up -> steps aside
        nbt::OverlayWatch w;
        w.onOwnState(nbt::OverlayWatch::kAaState, 1000);
        w.onContext(1534, nbt::betweenScreens(1534, "GFXIDENTIFIER_INVALID", "eDRIVE mode"), false, true, 5000);
        assert(w.poll(5100, true, false, false) == nbt::OverlayWatch::NONE);       // settling
        assert(w.poll(9000, true, false, false) == nbt::OverlayWatch::STEP_ASIDE);
    }
    printf("test_overlay_watch: all passed\n");
    return 0;
}
