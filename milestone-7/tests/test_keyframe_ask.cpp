// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test: the keyframe request after a dropped video frame (src/keyframe_ask.hpp).
#include "keyframe_ask.hpp"
#include <assert.h>
#include <stdio.h>
using namespace nbt;

int main() {
    const KeyframeAsk::Action none = KeyframeAsk::kNone, off = KeyframeAsk::kFocusOff, on = KeyframeAsk::kFocusOn;

    // Nothing lost: nothing sent.
    {
        KeyframeAsk k;
        assert(k.step(1000, false, true, true, true) == none);
        assert(!k.pending());
    }
    // A lost frame: focus off at once, on again 500 ms later, nothing in between.
    {
        KeyframeAsk k;
        assert(k.step(1000, true, true, true, true) == off);
        assert(k.pending());
        assert(k.step(1250, false, true, true, true) == none);
        assert(k.step(1499, false, true, true, true) == none);
        assert(k.step(1500, false, true, true, true) == on);
        assert(!k.pending());
        assert(k.step(1750, false, true, true, true) == none);
    }
    // More lost frames: one ask in 10 s, counted from the last one.
    {
        KeyframeAsk k;
        assert(k.step(0, true, true, true, true) == off);
        assert(k.step(500, true, true, true, true) == on);
        assert(k.step(750, true, true, true, true) == none);
        assert(k.step(9999, true, true, true, true) == none);
        assert(k.step(10000, true, true, true, true) == off);
        assert(k.step(10500, true, true, true, true) == on);
        assert(k.step(19999, true, true, true, true) == none);
        assert(k.step(20000, true, true, true, true) == off);
    }
    // Not while iDrive has the screen, in a session's first seconds or during a decoder rebuild (canAsk false): the
    // request waits and goes out once Android Auto is back.
    {
        KeyframeAsk k;
        assert(k.step(0, true, false, false, true) == none);
        assert(k.step(250, true, false, true, true) == none);
        assert(!k.pending());
        assert(k.step(500, true, true, true, true) == off);
    }
    // iDrive takes the screen between the two halves: focus on waits until Android Auto is to be shown again.
    {
        KeyframeAsk k;
        assert(k.step(0, true, true, true, true) == off);
        assert(k.step(250, false, false, false, true) == none);
        assert(k.step(5000, false, false, false, true) == none);   // reversing, say
        assert(k.pending());
        assert(k.step(5250, false, false, true, true) == on);      // back: canRestore is enough, the picture is not up yet
        assert(!k.pending());
    }
    // The session ends between the two halves: nothing more is sent, also not in the next session.
    {
        KeyframeAsk k;
        assert(k.step(0, true, true, true, true) == off);
        assert(k.step(250, false, false, false, false) == none);
        assert(!k.pending());
        assert(k.step(60000, false, true, true, true) == none);
    }
    // The millisecond counter wraps (49 days): differences still come out right.
    {
        KeyframeAsk k;
        assert(k.step(0xFFFFFF00u, true, true, true, true) == off);
        assert(k.step(0x00000010u, false, true, true, true) == none);   // 272 ms later
        assert(k.step(0x00000100u, false, true, true, true) == on);     // 512 ms later
        assert(k.step(0x00001000u, true, true, true, true) == none);    // 4.3 s after the ask
        assert(k.step(0x00002700u, true, true, true, true) == off);     // 10.2 s after the ask
    }
    printf("test_keyframe_ask OK\n");
    return 0;
}
