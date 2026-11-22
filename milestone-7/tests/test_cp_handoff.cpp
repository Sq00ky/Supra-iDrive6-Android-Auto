// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "cp_handoff.hpp"
#include <assert.h>
#include <stdio.h>

using namespace nbt;

static CpContext context(int id, const char* widget, time_t modified) {
    char text[256];
    snprintf(text, sizeof(text), "vc %d\n{\"graphicalContext\":{\"visibleContext\":%d,\"widgetType\":\"%s\",\"menuTitle\":\"\"}}\n",
             id, id, widget);
    return parseCpContext(text, modified);
}
static CpContext menu(time_t modified) { return context(3, "LT_Table_ID6L_Pad_Title", modified); }
static CpContext cp(time_t modified, int id = 2287) { return context(id, "eWidgetType_ProjectedUIWidget", modified); }
static CpHandoff::Input minimized() {
    CpHandoff::Input in;
    in.active = in.minimized = in.hidden = in.restoreEnabled = in.yieldEnabled = true;
    return in;
}

int main() {
    // Actual CDS layout, exact widget matching and native context range; truncated/mismatched files fail closed.
    assert(cp(100).valid && cp(100).native && cp(100).projected);
    assert(cp(100, 2300).projected);   // no Build66 context-number pin
    assert(!cp(100, 50002).native);
    assert(!context(3, "GFXIDENTIFIER_INVALID", 100).native);
    assert(!context(3, "", 100).native);
    assert(!context(3, "eWidgetType_ProjectedUIWidgetSuffix", 100).projected);
    assert(!parseCpContext("vc 3\n{\"visibleContext\":3,\"widgetType\":\"x\"", 100).valid);
    assert(!parseCpContext("vc 3\n{\"visibleContext\":4,\"widgetType\":\"x\"}", 100).valid);
    assert(!parseCpContext("vc 0\n{}", 100).valid);
    assert(!parseCpContext("vc 9999999999999999999999\n{}", 100).valid);
    assert(!parseCpContext(NULL, 100).valid);

    // MENU can arrive before the daemon's hide flag, including across the wall-clock second boundary.
    {
        CpHandoff h;
        CpHandoff::Input in = minimized();
        in.minimized = false;
        assert(!h.step(900, 100, in, menu(100)).restore);
        in.minimized = true;
        in.hidden = false;
        assert(!h.step(1100, 101, in, menu(100)).restore);
        in.hidden = true;
        assert(h.step(1200, 101, in, menu(100)).restore);
        // Model a failed restore: factoryMode stays false. No retry on ticks, context updates or ten seconds.
        for (uint32_t t = 1450; t < 22000; t += 250)
            assert(!h.step(t, 101 + t / 1000, in, menu(101 + t / 1000)).restore);
        in.minimized = false;
        h.step(22000, 123, in, context(50002, "GFXIDENTIFIER_INVALID", 123));
        in.minimized = true;
        assert(h.step(22250, 123, in, menu(123)).restore);
    }
    // Compatibility/hand-off/purge policy disables early restore; temporary car views and visible AA block it.
    {
        CpHandoff h;
        CpHandoff::Input in = minimized();
        in.restoreEnabled = false;
        assert(!h.step(0, 100, in, menu(100)).restore);
        in.restoreEnabled = true;
        in.blocked = true;
        assert(!h.step(250, 100, in, menu(100)).restore);   // reverse/PDC/camera/drive-mode/favourites
        in.blocked = false;
        in.hidden = false;
        assert(!h.step(500, 100, in, menu(100)).restore);
        in.hidden = true;
        assert(h.step(750, 100, in, menu(100)).restore);
    }
    // Restoring surface 1280 is not an activation: MENU never yields, even with factoryMode set.
    {
        CpHandoff h;
        CpHandoff::Input in = minimized();
        assert(h.step(0, 100, in, menu(100)).restore);
        in.factoryMode = true;
        for (uint32_t t = 250; t < 20000; t += 250) {
            CpHandoff::Action a = h.step(t, 100 + t / 1000, in, menu(100));
            assert(!a.yield && !a.restore);
        }
    }
    // Captured connection-only spikes: 263 ms and 5 ms, separated by device/menu contexts. Neither may yield.
    {
        CpHandoff h;
        CpHandoff::Input in = minimized();
        assert(h.step(2300000, 100, in, menu(100)).restore);
        in.factoryMode = true;
        assert(!h.step(2354492, 154, in, cp(154)).yield);
        assert(!h.step(2354750, 154, in, cp(154)).yield);
        assert(!h.step(2354755, 154, in, context(1425, "LT_RadioButton_2Row_2Text", 154)).yield);
        assert(!h.step(2355000, 155, in, cp(155)).yield);
        assert(!h.step(2355005, 155, in, context(1425, "LT_RadioButton_2Row_2Text", 155)).yield);
        // A permanent opening takes elapsed >= 500 ms of the same identity, not just two samples.
        assert(!h.step(2695087, 495, in, cp(495)).yield);
        assert(!h.step(2695586, 495, in, cp(495)).yield);
        assert(h.step(2695587, 495, in, cp(495)).yield);
        assert(!h.step(2695600, 495, in, cp(495, 2300)).yield);
        assert(!h.step(2696099, 496, in, cp(495, 2300)).yield);
        assert(h.step(2696100, 496, in, cp(495, 2300)).yield);
    }
    // Polling can miss the device screen between those spikes. The new file revision restarts the timer too.
    {
        CpHandoff h;
        CpHandoff::Input in = minimized();
        assert(h.step(2300000, 100, in, menu(100)).restore);
        in.factoryMode = true;
        assert(!h.step(2354501, 154, in, cp(154)).yield);
        assert(!h.step(2354751, 154, in, cp(154)).yield);
        assert(!h.step(2355001, 155, in, cp(155)).yield);
        assert(!h.step(2355251, 155, in, menu(155)).yield);
    }
    // Old projected state cannot stop a new daemon, nor can a missing/rewritten/future-dated file.
    {
        CpHandoff h;
        CpHandoff::Input in = minimized();
        in.factoryMode = true;
        for (uint32_t t = 0; t < 20000; t += 250) assert(!h.step(t, 200 + t / 1000, in, cp(100)).yield);
        assert(!h.step(20000, 220, in, CpContext()).yield);
        assert(!h.step(21000, 221, in, cp(222)).yield);
        assert(!h.step(21250, 221, in, cp(221)).yield);
        assert(!h.step(21749, 221, in, cp(221)).yield);
        assert(h.step(21750, 221, in, cp(221)).yield);
        in.blocked = true;
        assert(!h.step(22000, 222, in, cp(221)).yield);
        in.blocked = false;
        assert(!h.step(22250, 222, in, cp(221)).yield);
        in.yieldEnabled = false;
        assert(!h.step(23000, 223, in, cp(221)).yield);
        in.yieldEnabled = true;
        assert(!h.step(23250, 223, in, cp(221)).yield);
        assert(h.step(23750, 223, in, cp(221)).yield);
    }
    // Explicit return/reset and a session end rearm the once-per-minimize attempt.
    {
        CpHandoff h;
        CpHandoff::Input in = minimized();
        assert(h.step(0, 100, in, menu(100)).restore);
        assert(!h.step(1000, 101, in, menu(101)).restore);
        h.reset();
        assert(h.step(1250, 101, in, menu(101)).restore);
        in.active = false;
        assert(!h.step(1500, 101, in, menu(101)).restore);
        in.active = true;
        assert(h.step(1750, 101, in, menu(101)).restore);
    }
    printf("test_cp_handoff OK\n");
    return 0;
}
