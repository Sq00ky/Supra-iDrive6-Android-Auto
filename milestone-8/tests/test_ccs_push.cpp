// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test: replays the 2026-10-23 centre-push / tilt / MEDIA-hold car run through CcsDecoder.
// g++ -std=gnu++98 -Wall -Wextra -Iinclude -Isrc -o /tmp/test_ccs_push tests/test_ccs_push.cpp && /tmp/test_ccs_push tests/fixtures/controls_2026-10-23_push.txt
#include "ccs_decoder.hpp"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <string>
#include <vector>
using namespace nbt;

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "tests/fixtures/controls_2026-10-23_push.txt";
    FILE* f = fopen(path, "r");
    if (!f) { printf("cannot open %s\n", path); return 1; }
    CcsDecoder dec;
    std::string seq;
    int rot = 0;
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
            if (ev[i].kind == 2) { ++rot; continue; }
            if (!seq.empty()) seq += ' ';
            seq += ev[i].what;
            seq += ev[i].down ? "+" : "-";
        }
    }
    fclose(f);
    printf("sequence: %s\n", seq.c_str());
    const char* want = "PUSH+ PUSH- PUSH+ PUSH- PUSH+ PUSH- PUSH+ PUSH- TILT_RIGHT+ TILT_RIGHT- "
                       "TILT_LEFT+ TILT_LEFT- MEDIA+ MEDIA-";
    if (seq != want) { printf("EXPECTED: %s\n", want); return 1; }
    assert(rot == 0);   // knob was not turned in this run
    // byte 5/6 alone must no longer count as a push
    CcsDecoder w; CcsEvent e[4];
    const uint8_t a[8] = { 1, 0x80, 0x05, 0, 0, 1, 1, 0xff };
    assert(w.feed(0xE5, 3, 0xC46, 0xC, a, 8, e, 4) == 0);
    printf("test_ccs_push OK\n");
    return 0;
}
