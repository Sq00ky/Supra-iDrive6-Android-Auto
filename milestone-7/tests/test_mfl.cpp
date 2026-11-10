// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test: the steering wheel's media keys (car/mfl.hpp): "fkt" and "fkt:value" bindings from the environment, the
// J29's roller sending 00/02 (and 01 the other way) on one function, edge detection (repeats are not presses) and
// that a periodic function never looks like a press.
#include "car/mfl.hpp"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

using namespace nbt::car;
using namespace nbt;

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

int main() {
    const uint8_t zero[1] = { 0x00 };
    const uint8_t one[1] = { 0x01 };
    const uint8_t two[1] = { 0x02 };

    // ---- defaults: the J29 roller, 511 value 02 = next, 01 = previous
    {
        unsetenv("NBT_MFL_NEXT"); unsetenv("NBT_MFL_PREV"); unsetenv("NBT_MFL_PLAYPAUSE");
        MflConfig c = readMflConfig();
        CHECK(c.n == 2);
        CHECK(c.b[0].fkt == 0x511 && c.b[0].value == 0x02 && c.b[0].key == AAK_MEDIA_NEXT);
        CHECK(c.b[1].fkt == 0x511 && c.b[1].value == 0x01 && c.b[1].key == AAK_MEDIA_PREVIOUS);
    }

    // ---- the roller: 00 -> 02 -> 00 -> 02 ... is one press per detent, and 01 is the other direction
    {
        MflConfig c = readMflConfig();
        c.log = false;
        MflDecoder d;
        d.setConfig(c);
        MflEvent ev[kMflBindings * 2];

        int n = d.feed(0x511, 0xC, two, 1, ev, kMflBindings * 2);
        CHECK(n == 1 && ev[0].down == 1 && ev[0].keycode == AAK_MEDIA_NEXT && !strcmp(ev[0].what, "MFL_NEXT"));
        CHECK(d.feed(0x511, 0xC, two, 1, ev, kMflBindings * 2) == 0);      // held: not a second press
        n = d.feed(0x511, 0xC, zero, 1, ev, kMflBindings * 2);
        CHECK(n == 1 && ev[0].down == 0 && ev[0].keycode == AAK_MEDIA_NEXT);
        CHECK(d.feed(0x511, 0xC, zero, 1, ev, kMflBindings * 2) == 0);
        n = d.feed(0x511, 0xC, two, 1, ev, kMflBindings * 2);              // second detent
        CHECK(n == 1 && ev[0].down == 1);

        // the other direction on the same function
        d.feed(0x511, 0xC, zero, 1, ev, kMflBindings * 2);
        n = d.feed(0x511, 0xC, one, 1, ev, kMflBindings * 2);
        CHECK(n == 1 && ev[0].down == 1 && ev[0].keycode == AAK_MEDIA_PREVIOUS && !strcmp(ev[0].what, "MFL_PREV"));
        n = d.feed(0x511, 0xC, zero, 1, ev, kMflBindings * 2);
        CHECK(n == 1 && ev[0].down == 0 && ev[0].keycode == AAK_MEDIA_PREVIOUS);

        // a value nobody mapped (03) neither presses nor releases anything
        CHECK(d.feed(0x511, 0xC, (const uint8_t*)"\x03", 1, ev, kMflBindings * 2) == 0);
    }

    // ---- "fkt" without a value: any change away from 0 is a press (the voice-style 01/00 pair)
    {
        setenv("NBT_MFL_NEXT", "C31", 1);
        setenv("NBT_MFL_PREV", "", 1);
        setenv("NBT_MFL_LOG", "0", 1);
        MflConfig c = readMflConfig();
        CHECK(c.n == 1 && c.b[0].fkt == 0xC31 && c.b[0].value < 0 && c.b[0].key == AAK_MEDIA_NEXT);
        MflDecoder d;
        d.setConfig(c);
        MflEvent ev[kMflBindings * 2];
        int n = d.feed(0xC31, 0xC, one, 1, ev, kMflBindings * 2);
        CHECK(n == 1 && ev[0].down == 1 && ev[0].keycode == AAK_MEDIA_NEXT);
        n = d.feed(0xC31, 0xC, (const uint8_t*)"\x7f", 1, ev, kMflBindings * 2);
        CHECK(n == 0);                                                   // already down
        n = d.feed(0xC31, 0xC, zero, 1, ev, kMflBindings * 2);
        CHECK(n == 1 && ev[0].down == 0);
        n = d.feed(0xC31, 0xC, (const uint8_t*)"\x7f", 1, ev, kMflBindings * 2);
        CHECK(n == 1 && ev[0].down == 1);                                 // 7f works as a press code
    }

    // ---- lists, several bindings per key, and the play/pause line
    {
        setenv("NBT_MFL_NEXT", "511:2,C31", 1);
        setenv("NBT_MFL_PREV", "511:1", 1);
        setenv("NBT_MFL_PLAYPAUSE", "513", 1);
        MflConfig c = readMflConfig();
        CHECK(c.n == 4);
        CHECK(c.b[0].fkt == 0x511 && c.b[0].value == 2 && c.b[1].fkt == 0xC31 && c.b[1].value < 0);
        CHECK(c.b[2].fkt == 0x511 && c.b[2].value == 1 && c.b[2].key == AAK_MEDIA_PREVIOUS);
        CHECK(c.b[3].fkt == 0x513 && c.b[3].key == AAK_MEDIA_PLAY_PAUSE);
        MflDecoder d;
        d.setConfig(c);
        MflEvent ev[kMflBindings * 2];
        int n = d.feed(0x513, 0xC, one, 1, ev, kMflBindings * 2);
        CHECK(n == 1 && ev[0].keycode == AAK_MEDIA_PLAY_PAUSE && !strcmp(ev[0].what, "MFL_PLAYPAUSE"));
        n = d.feed(0xC31, 0xC, one, 1, ev, kMflBindings * 2);
        CHECK(n == 1 && ev[0].keycode == AAK_MEDIA_NEXT && !strcmp(ev[0].what, "MFL_NEXT"));
    }

    // ---- a periodic function that repeats one value must never look like a press
    {
        MflConfig c;
        mflParseBindings("401", c.b, kMflBindings, &c.n, AAK_MEDIA_NEXT, "MFL_NEXT");
        c.log = false;
        MflDecoder d;
        d.setConfig(c);
        MflEvent ev[kMflBindings * 2];
        const uint8_t v[4] = { 0x00, 0x00, 0x00, 0x01 };
        int total = 0;
        for (int i = 0; i < 50; ++i) total += d.feed(0x401, 0xC, v, 4, ev, kMflBindings * 2);
        CHECK(total == 0);                                               // first byte is always 0
    }

    // ---- junk in the environment maps nothing rather than fkt 0
    {
        setenv("NBT_MFL_NEXT", "zz", 1);        // replaces next, prev's default stays
        MflConfig j = readMflConfig();
        CHECK(j.n == 2 && j.b[0].key == AAK_MEDIA_PREVIOUS && j.b[1].key == AAK_MEDIA_PLAY_PAUSE);
        setenv("NBT_MFL_NEXT", "0", 1);
        CHECK(readMflConfig().n == 2);
        setenv("NBT_MFL_NEXT", "", 1);
        CHECK(readMflConfig().n == 2);
        unsetenv("NBT_MFL_NEXT"); unsetenv("NBT_MFL_PREV"); unsetenv("NBT_MFL_PLAYPAUSE");
    }

    if (fails) { printf("%d check(s) failed\n", fails); return 1; }
    printf("test_mfl: all checks passed\n");
    return 0;
}
