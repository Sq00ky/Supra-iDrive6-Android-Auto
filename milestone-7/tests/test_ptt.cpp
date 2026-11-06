// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test: the steering-wheel voice button timing (car/ptt.hpp).
#include "car/ptt.hpp"
#include <assert.h>
#include <stdio.h>

using namespace nbt::car;

int main() {
    PttTracker p;
    PttTracker::Result r;
    assert(!p.held());

    // Short press.
    r = p.feed(1, 1001);
    assert(r.kind == PttTracker::kPressed && p.held());
    r = p.feed(0, 1101);
    assert(r.kind == PttTracker::kReleasedShort && r.heldMs == 100 && !p.held());

    // The long-press limit is the HMI's 750 ms.
    p.feed(1, 2001);
    r = p.feed(0, 2750);
    assert(r.kind == PttTracker::kReleasedShort && r.heldMs == 749);
    p.feed(1, 3001);
    r = p.feed(0, 3751);
    assert(r.kind == PttTracker::kReleasedLong && r.heldMs == 750);

    // State 2 changes nothing: the press is still being timed.
    p.feed(1, 4001);
    r = p.feed(2, 4100);
    assert(r.kind == PttTracker::kIgnored && p.held());
    r = p.feed(0, 5001);
    assert(r.kind == PttTracker::kReleasedLong && r.heldMs == 1000);

    // Cancel ends the press and is never a long press.
    p.feed(1, 6001);
    r = p.feed(3, 9001);
    assert(r.kind == PttTracker::kCancelled && !p.held());

    // A release with no press seen (the press happened before we started) is short, with no time.
    r = p.feed(0, 10000);
    assert(r.kind == PttTracker::kReleasedShort && r.heldMs == 0);

    // A press at time 0 still counts as held.
    p.feed(1, 0);
    assert(p.held());

    // Press and release in the same millisecond, at an even or an odd time: after a stall both are handled back to back.
    p.feed(1, 12000);
    r = p.feed(0, 12000);
    assert(r.kind == PttTracker::kReleasedShort && r.heldMs == 0);
    p.feed(1, 12001);
    r = p.feed(0, 12001);
    assert(r.kind == PttTracker::kReleasedShort && r.heldMs == 0);

    // reset forgets a press: the next release cannot be long.
    p.feed(1, 11001);
    p.reset();
    assert(!p.held());
    r = p.feed(0, 20000);
    assert(r.kind == PttTracker::kReleasedShort && r.heldMs == 0);

    // The millisecond counter wraps.
    p.feed(1, 0xFFFFFF01u);
    r = p.feed(0, 0x000002FFu);
    assert(r.kind == PttTracker::kReleasedLong && r.heldMs == 0x3FE);

    printf("test_ptt OK\n");
    return 0;
}
