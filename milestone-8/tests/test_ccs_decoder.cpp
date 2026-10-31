// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test: replays the E5 controller messages of the 2026-10-22 car run through CcsDecoder.
// g++ -std=gnu++98 -Wall -Wextra -Iinclude -Isrc -o /tmp/test_ccs tests/test_ccs_decoder.cpp && /tmp/test_ccs tests/fixtures/controls_2026-10-22_e5.txt
#include "ccs_decoder.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <string>
#include <vector>
using namespace nbt;

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "tests/fixtures/controls_2026-10-22_e5.txt";
    FILE* f = fopen(path, "r");
    if (!f) { printf("cannot open %s\n", path); return 1; }
    CcsDecoder dec;
    std::string seq;
    int cw = 0, ccw = 0;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;
        unsigned inst, fkt, op;
        int off = 0;
        if (sscanf(line, "%x %x %x%n", &inst, &fkt, &op, &off) != 3) continue;
        std::vector<uint8_t> d;
        const char* p = line + off;
        unsigned b; int k;
        while (sscanf(p, " %2x%n", &b, &k) == 1) { d.push_back((uint8_t)b); p += k; }
        CcsEvent ev[16];
        int n = dec.feed(0xE5, (uint8_t)inst, (uint16_t)fkt, (uint8_t)op, d.empty() ? NULL : &d[0], d.size(), ev, 16);
        for (int i = 0; i < n; ++i) {
            if (ev[i].kind == 2) {
                assert(ev[i].keycode == AAK_ROTARY_CONTROLLER);
                if (ev[i].delta > 0) cw += ev[i].delta; else ccw -= ev[i].delta;
                char c = ev[i].delta > 0 ? '+' : '-';
                if (seq.empty() || seq[seq.size() - 1] != c) seq += c;
            } else if (ev[i].down) {
                if (!seq.empty()) seq += ' ';
                seq += ev[i].what;
                char kc[16]; snprintf(kc, sizeof(kc), "=%u", ev[i].keycode); seq += kc;
            }
        }
    }
    fclose(f);
    printf("rotary: %d clockwise, %d counter-clockwise\nsequence: %s\n", cw, ccw, seq.c_str());
    // Car run: 7 detents CW, 12 CCW, tilts up/down/right/left (with a few stray detents while tilting),
    // then media, com, menu, map, nav and the two remaining keys (slot 2, then slot 1). Slot 1 is BACK since v1.6.4.
    assert(seq.find("+-") == 0);
    const char* order[] = { "TILT_UP=19", "TILT_DOWN=20", "TILT_RIGHT=22", "TILT_LEFT=21",
                            "MEDIA=65537", "COM=5", "MENU=3", "MAP=65538", "NAV=65538", "OPTIONS=0", "BACK=4" };
    size_t at = 0;
    for (size_t i = 0; i < sizeof(order) / sizeof(order[0]); ++i) {
        size_t f2 = seq.find(order[i], at);
        if (f2 == std::string::npos) { printf("MISSING in order: %s\n", order[i]); return 1; }
        at = f2 + strlen(order[i]);
    }
    // wrap-around and inversion
    CcsDecoder w; CcsEvent e[4];
    const uint8_t a[8] = { 1, 0xff, 0xff, 0, 0, 0, 0, 0xff }, c[8] = { 2, 0x00, 0x01, 0, 0, 0, 0, 0xff };
    assert(w.feed(0xE5, 3, 0xC46, 0xC, a, 8, e, 4) == 0);
    assert(w.feed(0xE5, 3, 0xC46, 0xC, c, 8, e, 4) == 1 && e[0].delta == 2);
    assert(w.feed(0xE5, 5, 0xC46, 0xC, a, 8, e, 4) == 0);   // other instance ignored
    w.setInvert(true);
    assert(w.feed(0xE5, 3, 0xC46, 0xC, a, 8, e, 4) == 1 && e[0].delta == 2);
    // Older CCS protocol (E5/01 fkt C10 knob, C20 key events; layout from the firmware).
    {
        nbt::CcsDecoder c;
        nbt::CcsEvent ev[8];
        uint8_t r0[7] = { 0x01, 0x1d, 0xff, 0x7f, 0xff, 0x1e, 0 };          // not configured: ignored
        assert(c.feed(0xE5, 1, 0xC10, 0xC, r0, 7, ev, 8) == 0);
        uint8_t r1[7] = { 0x01, 0x1d, 0x05, 0x7f, 0xff, 0x1e, 0 };          // first configured value: baseline
        assert(c.feed(0xE5, 1, 0xC10, 0xC, r1, 7, ev, 8) == 0);
        uint8_t r2[7] = { 0x01, 0x1d, 0x05, 0x80, 0x01, 0x1e, 0 };          // +2 clockwise
        assert(c.feed(0xE5, 1, 0xC10, 0xC, r2, 7, ev, 8) == 1 && ev[0].kind == 2 && ev[0].delta == 2);
        uint8_t r3[7] = { 0x01, 0x1d, 0x05, 0x7f, 0xfe, 0x1e, 0 };          // -3
        assert(c.feed(0xE5, 1, 0xC10, 0xC, r3, 7, ev, 8) == 1 && ev[0].delta == -3);
        uint8_t r4[7] = { 0x01, 0x1d, 0x06, 0x7f, 0xff, 0x1e, 0 };          // [2] is a message counter: no re-base, +1
        assert(c.feed(0xE5, 1, 0xC10, 0xC, r4, 7, ev, 8) == 1 && ev[0].delta == 1);
        uint8_t r5[7] = { 0x01, 0x1d, 0x07, 0x80, 0x03, 0x1e, 1 };          // overflow: re-base, no jump
        assert(c.feed(0xE5, 1, 0xC10, 0xC, r5, 7, ev, 8) == 0);
        uint8_t r6[7] = { 0x01, 0x1d, 0x08, 0x80, 0x04, 0x1e, 0 };          // +1 from the re-based 0x8003
        assert(c.feed(0xE5, 1, 0xC10, 0xC, r6, 7, ev, 8) == 1 && ev[0].delta == 1);
        assert(c.feed(0xE5, 3, 0xC10, 0xC, r2, 7, ev, 8) == 0);              // wrong instance ignored
        uint8_t push[7] = { 0x01, 0x1d, 0x05, 1, 0, 0x1e, 1 };
        assert(c.feed(0xE5, 1, 0xC20, 0xC, push, 7, ev, 8) == 1 && ev[0].keycode == nbt::AAK_DPAD_CENTER && ev[0].down == 1);
        push[3] = 2;                                                           // held: no repeat
        assert(c.feed(0xE5, 1, 0xC20, 0xC, push, 7, ev, 8) == 0);
        push[3] = 0;
        assert(c.feed(0xE5, 1, 0xC20, 0xC, push, 7, ev, 8) == 1 && ev[0].down == 0);
        uint8_t tilt[7] = { 0x01, 0x1d, 0x05, 1, 2, 0x1d, 0 };               // tilt right
        assert(c.feed(0xE5, 1, 0xC20, 0xC, tilt, 7, ev, 8) == 1 && ev[0].keycode == nbt::AAK_DPAD_RIGHT && ev[0].down == 1);
        tilt[3] = 0; tilt[4] = 0;                                             // release
        assert(c.feed(0xE5, 1, 0xC20, 0xC, tilt, 7, ev, 8) == 1 && ev[0].keycode == nbt::AAK_DPAD_RIGHT && ev[0].down == 0);
        uint8_t back[7] = { 0x01, 0x1d, 0x05, 1, 0, 0x00, 0x02 };            // sensor bit 1 = slot 1 BACK
        assert(c.feed(0xE5, 1, 0xC20, 0xC, back, 7, ev, 8) == 1 && ev[0].keycode == nbt::AAK_BACK && ev[0].down == 1);
        back[3] = 0;
        assert(c.feed(0xE5, 1, 0xC20, 0xC, back, 7, ev, 8) == 1 && ev[0].keycode == nbt::AAK_BACK && ev[0].down == 0);
        back[3] = 3;                                                           // "no change": ignored
        assert(c.feed(0xE5, 1, 0xC20, 0xC, back, 7, ev, 8) == 0);
        c.setSlotKey(nbt::CcsDecoder::slotByName("MEDIA"), 85);               // NBT_KEY_MEDIA override applies too
        uint8_t media[7] = { 0x01, 0x1d, 0x05, 1, 0, 0x00, 0x10 };
        assert(c.feed(0xE5, 1, 0xC20, 0xC, media, 7, ev, 8) == 1 && ev[0].keycode == 85);
        printf("test_ccs_decoder CCS (C10/C20) OK\n");
    }
    {   // G30 NBT EVO ID6 capture 2026-10-26 (v1.8.2 knob.log, issue #3): every C10 arrives twice, [2] counts up.
        nbt::CcsDecoder g;
        nbt::CcsEvent ev[8];
        const uint8_t m[3][7] = { { 0x01, 0x1d, 0x21, 0x80, 0x00, 0x1e, 0 }, { 0x01, 0x1d, 0x22, 0x80, 0x01, 0x1e, 0 },
                                  { 0x01, 0x1d, 0x23, 0x80, 0x02, 0x1e, 0 } };
        int cw = 0, events = 0;
        for (int i = 0; i < 3; ++i)
            for (int copy = 0; copy < 2; ++copy) {
                int n = g.feed(0xE5, 1, 0xC10, 0xC, m[i], 7, ev, 8);
                for (int j = 0; j < n; ++j) { assert(ev[j].kind == 2); cw += ev[j].delta; ++events; }
            }
        assert(cw == 2 && events == 2);                                       // baseline, then two clockwise detents
        printf("test_ccs_decoder CCS G30 capture OK\n");
    }
    printf("test_ccs_decoder OK\n");
    return 0;
}
