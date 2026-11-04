// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test for rhmiapp/entry_key.hpp: the "Open Android Auto" entry / favourite key.
#include "../rhmiapp/entry_key.hpp"
#include <assert.h>
#include <stdio.h>

using namespace nbt::entrykey;

int main() {
    // not running: start and show our menu (status visible)
    assert(decide(false, false, false, false) == kStartAndMenu);
    // starting up / waiting for the phone: menu only, never a second launch
    assert(decide(true, false, false, false) == kMenu);
    // connected: show, or nothing when it is already on screen
    assert(decide(true, true, false, false) == kShow);
    assert(decide(true, true, true, false) == kNothing);
    // Force Stop / Restart in progress: menu only, whatever the daemon says
    for (int r = 0; r < 2; ++r)
        for (int c = 0; c < 2; ++c)
            for (int s = 0; s < 2; ++s) assert(decide(r, c, s, true) == kMenu);

    // after an entry start: show on connect only while our menu is reported on top and the overlay is not up
    assert(showOnConnect(true, true, false));
    assert(!showOnConnect(true, false, false));    // the driver went elsewhere
    assert(!showOnConnect(false, false, false));   // focus never reported: don't guess
    assert(!showOnConnect(true, true, true));      // already on screen
    printf("test_entry_key OK\n");
    return 0;
}
