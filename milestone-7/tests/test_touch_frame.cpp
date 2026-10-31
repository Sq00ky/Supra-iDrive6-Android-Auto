// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test for the touch PDU parser with real captures from two head units:
//   Supra (1280x480), whole message per 112-byte datagram: two-finger pinch (w9.log, 2026-10-23)
//   F-series NBT Evo (1440x540), message split over 24..64-byte datagrams: corner touches (touch.log, 2026-10-26)
// g++ -std=gnu++98 -Wall -Wextra -Iinclude -Isrc -o /tmp/test_touch_frame tests/test_touch_frame.cpp
// /tmp/test_touch_frame tests/fixtures/touch_multi_2026-10-23_pinch.txt tests/fixtures/touch_fseries_2026-10-26_corners.txt
#include "touch_frame.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
using namespace nbt;

enum { kMaxFrames = 400 };
static unsigned char g_buf[kMaxFrames][128];
static size_t g_len[kMaxFrames];

static int loadFixture(const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) { printf("cannot open %s\n", path); exit(1); }
    char line[1024];
    int nf = 0;
    while (fgets(line, sizeof(line), f) && nf < kMaxFrames) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
        size_t n = 0;
        const char* p = line;
        unsigned b; int k;
        while (n < sizeof(g_buf[0]) && sscanf(p, " %2x%n", &b, &k) == 1) { g_buf[nf][n++] = (unsigned char)b; p += k; }
        g_len[nf++] = n;
    }
    fclose(f);
    return nf;
}

static void testSupraPinch(const char* path) {
    const int nf = loadFixture(path);
    int frames = 0, firstX0 = -1, firstX1 = -1, lastX0 = -1, lastX1 = -1;
    TouchReassembler r;
    for (int i = 0; i < nf; ++i) {
        assert(g_len[i] == 112);
        TouchContact c[kTouchMaxContacts];
        int declared = 0;
        int nc = parseTouchFrame(g_buf[i], g_len[i], c, kTouchMaxContacts, &declared);
        assert(nc == 2 && declared == 2);
        assert(c[0].id == 0 && c[1].id == 1);
        for (int j = 0; j < 2; ++j) {
            assert(c[j].state >= 1 && c[j].state <= 3);
            assert(c[j].x >= 0 && c[j].x < 1280 && c[j].y >= 0 && c[j].y < 480);
        }
        // the streaming reassembler (what the forwarder uses) gives the same result
        TouchMsg m[kTouchMaxMsgs];
        assert(r.feed(g_buf[i], g_len[i], m, kTouchMaxMsgs) == 1 && m[0].recs == 12);
        TouchContact c2[kTouchMaxContacts];
        int d2 = 0;
        assert(parseTouchStream(m[0], c2, kTouchMaxContacts, &d2) == 2 && d2 == 2);
        assert(c2[0].x == c[0].x && c2[1].y == c[1].y);
        if (frames == 0) {
            assert(c[0].state == 1 && c[1].state == 1);           // both fingers land in the first frame
            firstX0 = c[0].x; firstX1 = c[1].x;
        }
        lastX0 = c[0].x; lastX1 = c[1].x;
        ++frames;
    }
    printf("supra: frames=%d finger0 x %d->%d finger1 x %d->%d\n", frames, firstX0, lastX0, firstX1, lastX1);
    assert(frames == 60);
    assert(firstX0 == 959 && firstX1 == 683);
    assert((lastX0 - lastX1) < (firstX0 - firstX1));             // pinch: fingers moved together
}

static void testFSeriesCorners(const char* path) {
    const int nf = loadFixture(path);
    assert(nf == 200);
    TouchReassembler r;
    int msgs = 0, withContact = 0, ndown = 0, nup = 0, maxX = 0, maxY = 0;
    int downX[8], downY[8];
    for (int i = 0; i < nf; ++i) {
        assert(g_len[i] >= 24 && g_len[i] <= 64);                // never a whole message per datagram here
        TouchMsg m[kTouchMaxMsgs];
        const int nm = r.feed(g_buf[i], g_len[i], m, kTouchMaxMsgs);
        assert(nm >= 0);
        for (int k = 0; k < nm; ++k) {
            ++msgs;
            assert(m[k].recs == 12);
            assert(m[k].seq1 == (unsigned char)(m[k].seq0 + 11));
            TouchContact c[kTouchMaxContacts];
            int declared = 0;
            const int nc = parseTouchStream(m[k], c, kTouchMaxContacts, &declared);
            assert(nc == declared && nc <= 1);
            if (nc == 0) continue;
            ++withContact;
            assert(c[0].id == 1);
            assert(c[0].x >= 0 && c[0].x < 1440 && c[0].y >= 0 && c[0].y < 540);
            if (c[0].x > maxX) maxX = c[0].x;
            if (c[0].y > maxY) maxY = c[0].y;
            if (c[0].state == 1 && ndown < 8) { downX[ndown] = c[0].x; downY[ndown] = c[0].y; ++ndown; }
            if (c[0].state == 3) ++nup;
        }
    }
    printf("f-series: datagrams=%d messages=%d with-contact=%d downs=%d ups=%d max %d,%d dropped=%u\n",
           nf, msgs, withContact, ndown, nup, maxX, maxY, r.dropped());
    assert(msgs == 65 && withContact == 64 && ndown == 4 && nup == 1);
    assert(downX[0] == 28 && downY[0] == 533);      // bottom-left
    assert(downX[1] == 40 && downY[1] == 25);       // top-left
    assert(downX[2] == 1419 && downY[2] == 8);      // top-right
    assert(downX[3] == 1429 && downY[3] == 519);    // bottom-right
    assert(maxX > 1279 && maxY > 479);              // panel coordinates, 1440x540
    // A fragment alone is not a touch message.
    TouchContact c[kTouchMaxContacts];
    int d = 0;
    assert(parseTouchFrame(g_buf[0], g_len[0], c, kTouchMaxContacts, &d) == -1);
}

static void testSplitAndLoss() {
    TouchContact in[3] = { {0, 1, 100, 200, 50}, {1, 2, 1439, 539, 60}, {2, 3, 640, 0, 70} };
    unsigned char fr[112];
    buildTouchFrame(fr, in, 3, 0x80, 0xfa);                  // seq wraps inside the message
    TouchContact out[kTouchMaxContacts];
    int d = 0;
    assert(parseTouchFrame(fr, sizeof(fr), out, kTouchMaxContacts, &d) == 3 && d == 3);
    for (int i = 0; i < 3; ++i)
        assert(out[i].id == in[i].id && out[i].state == in[i].state && out[i].x == in[i].x && out[i].y == in[i].y && out[i].size == in[i].size);

    static const int splits[][4] = { {2, 5, 5, 0}, {2, 4, 6, 0}, {2, 6, 4, 0}, {2, 4, 5, 1}, {1, 1, 1, 9} };
    for (size_t s = 0; s < sizeof(splits) / sizeof(splits[0]); ++s) {
        unsigned char dg[4 * 112];
        int sz[4];
        const int parts = splitTouchFrame(fr, splits[s], 4, dg, sz);
        TouchReassembler r;
        TouchMsg m[kTouchMaxMsgs];
        int got = 0;
        for (int k = 0; k < parts; ++k) {
            const int nm = r.feed(dg + 112 * k, (size_t)sz[k], m, kTouchMaxMsgs);
            assert(nm == (k == parts - 1 ? 1 : 0));
            got += nm;
        }
        assert(got == 1 && parseTouchStream(m[0], out, kTouchMaxContacts, &d) == 3 && d == 3);
        assert(m[0].seq0 == 0xfa && m[0].seq1 == 0x05);
        assert(out[1].x == 1439 && out[1].y == 539 && out[2].state == 3);

        // lose the middle datagram: no message; the next start record resynchronises
        TouchReassembler r2;
        for (int k = 0; k < parts; ++k)
            if (k != 1) assert(r2.feed(dg + 112 * k, (size_t)sz[k], m, kTouchMaxMsgs) == 0);
        assert(r2.feed(fr, sizeof(fr), m, kTouchMaxMsgs) == 1);
    }
    // wrong ident -> -1
    fr[0] = 0;
    assert(parseTouchFrame(fr, sizeof(fr), out, kTouchMaxContacts, &d) == -1);
}

int main(int argc, char** argv) {
    testSupraPinch(argc > 1 ? argv[1] : "tests/fixtures/touch_multi_2026-10-23_pinch.txt");
    testFSeriesCorners(argc > 2 ? argv[2] : "tests/fixtures/touch_fseries_2026-10-26_corners.txt");
    testSplitAndLoss();
    printf("test_touch_frame OK\n");
    return 0;
}
