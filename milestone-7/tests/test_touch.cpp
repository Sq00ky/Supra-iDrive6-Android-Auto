// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "touch_probe.hpp"
#include "touch_frame.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <vector>
using namespace nbt;

static std::vector<TouchEventOut> g_ev;
static pthread_mutex_t g_mx = PTHREAD_MUTEX_INITIALIZER;
static void cb(const TouchEventOut& e, void*) { pthread_mutex_lock(&g_mx); g_ev.push_back(e); pthread_mutex_unlock(&g_mx); }

static int s_sock;
static sockaddr_in s_to;
static unsigned char s_ctr = 0;
static unsigned char s_seq = 0;           // record seq, continuous across messages like the HMI's
static unsigned char s_last[112];
static void sendFrame(const unsigned char* f);
// Up to two contacts in the real HMI layout (touch_frame.hpp). state 0 = contact not listed.
static void send3(int s0, int x0, int y0, int s1, int x1, int y1) {
    TouchContact c[2];
    int n = 0;
    if (s0) { c[n].id = 0; c[n].state = s0; c[n].x = x0; c[n].y = y0; c[n].size = 80; ++n; }
    if (s1) { c[n].id = 1; c[n].state = s1; c[n].x = x1; c[n].y = y1; c[n].size = 80; ++n; }
    buildTouchFrame(s_last, c, n, s_ctr, s_seq);
    s_ctr = (unsigned char)(s_ctr + 0x40);
    s_seq = (unsigned char)(s_seq + 12);
    sendFrame(s_last);
}
static void loseMessage() { s_ctr = (unsigned char)(s_ctr + 0x40); s_seq = (unsigned char)(s_seq + 12); }
static void resendLast() { sendFrame(s_last); }
static void sendFrame(const unsigned char* f) {
    if (getenv("TOUCH_SPLIT")) {          // 1440x540 head units: one message over several short datagrams
        static const int parts[3] = { 2, 5, 5 };
        unsigned char dg[3 * 112];
        int sz[3];
        const int nd = splitTouchFrame(f, parts, 3, dg, sz);
        for (int k = 0; k < nd; ++k) sendto(s_sock, dg + 112 * k, (size_t)sz[k], 0, (sockaddr*)&s_to, sizeof(s_to));
    } else {
        sendto(s_sock, f, 112, 0, (sockaddr*)&s_to, sizeof(s_to));
    }
    usleep(30000);
}
// Presses (action 0 / 5) and releases (1 / 6) since event index base.
static void countTaps(size_t base, int* downs, int* ups) {
    *downs = *ups = 0;
    pthread_mutex_lock(&g_mx);
    for (size_t i = base; i < g_ev.size(); ++i) {
        printf("  ev action=%d n=%d\n", g_ev[i].action, g_ev[i].n);
        if (g_ev[i].action == 0 || g_ev[i].action == 5) ++*downs;
        if (g_ev[i].action == 1 || g_ev[i].action == 6) ++*ups;
    }
    pthread_mutex_unlock(&g_mx);
}
static size_t evCount() { pthread_mutex_lock(&g_mx); size_t n = g_ev.size(); pthread_mutex_unlock(&g_mx); return n; }
int main() {
    setenv("NBT_TOUCH_SY", "1.0", 1); setenv("NBT_TOUCH_H", "480", 1);
    assert(startTouchForward(30599, &cb, NULL));
    setTouchOwned(true);                  // the forwarder only binds the port while Android Auto is on screen
    usleep(400000);
    s_sock = socket(AF_INET, SOCK_DGRAM, 0);
    memset(&s_to, 0, sizeof(s_to)); s_to.sin_family = AF_INET; s_to.sin_port = htons(30599);
    s_to.sin_addr.s_addr = inet_addr("127.0.0.1");
    // single finger tap
    send3(1, 100, 200, 0, 0, 0);
    send3(2, 101, 200, 0, 0, 0);
    send3(3, 101, 200, 0, 0, 0);
    // pinch: finger 0 down, finger 1 down, both move, finger 1 up, finger 0 up
    send3(1, 300, 240, 0, 0, 0);
    send3(2, 300, 240, 1, 400, 240);
    send3(2, 280, 240, 2, 420, 240);
    send3(2, 280, 240, 3, 420, 240);
    send3(3, 280, 240, 0, 0, 0);
    usleep(200000);
    pthread_mutex_lock(&g_mx);
    for (size_t i = 0; i < g_ev.size(); ++i) {
        printf("ev%u action=%d idx=%d n=%d", (unsigned)i, g_ev[i].action, g_ev[i].index, g_ev[i].n);
        for (int j = 0; j < g_ev[i].n; ++j) printf(" (%d:%d,%d)", g_ev[i].p[j].id, g_ev[i].p[j].x, g_ev[i].p[j].y);
        printf("\n");
    }
    // expected: 0 down, move, up(1) | press(0) | pointer_down(5, idx1) | drag(2,n2) | pointer_up(6, idx1) | release(1)
    assert(g_ev.size() >= 8);
    assert(g_ev[0].action == 0 && g_ev[0].n == 1);
    assert(g_ev[1].action == 2);
    assert(g_ev[2].action == 1);
    assert(g_ev[3].action == 0);
    assert(g_ev[4].action == 5 && g_ev[4].index == 1 && g_ev[4].n == 2);
    assert(g_ev[5].action == 2 && g_ev[5].n == 2);
    assert(g_ev[6].action == 6 && g_ev[6].index == 1 && g_ev[6].n == 2);
    assert(g_ev[7].action == 1 && g_ev[7].n == 1);
    // lost "up": finger down, the "up" message lost, a second down on the same slot -> synthesized release then press;
    // then silence -> idle release
    size_t base = g_ev.size();
    pthread_mutex_unlock(&g_mx);
    send3(1, 500, 100, 0, 0, 0);
    loseMessage();
    send3(1, 600, 120, 0, 0, 0);
    usleep(700000);
    pthread_mutex_lock(&g_mx);
    for (size_t i = base; i < g_ev.size(); ++i) printf("lost-up ev action=%d n=%d\n", g_ev[i].action, g_ev[i].n);
    assert(g_ev.size() >= base + 4);
    assert(g_ev[base].action == 0 && g_ev[base + 1].action == 1 && g_ev[base + 2].action == 0 && g_ev[base + 3].action == 1);
    pthread_mutex_unlock(&g_mx);

    // Each of these is ONE physical tap and must reach the phone as exactly one press and one release.
    int downs = 0, ups = 0;
    printf("repeated down:\n");                   // "down" repeated in consecutive messages
    base = evCount();
    send3(1, 700, 100, 0, 0, 0);
    send3(1, 700, 100, 0, 0, 0);
    send3(1, 702, 100, 0, 0, 0);
    send3(3, 702, 100, 0, 0, 0);
    usleep(100000);
    countTaps(base, &downs, &ups);
    assert(downs == 1 && ups == 1);

    printf("duplicated datagrams:\n");            // every message arrives twice
    base = evCount();
    send3(1, 300, 300, 0, 0, 0); resendLast();
    send3(2, 300, 300, 0, 0, 0); resendLast();
    send3(3, 300, 300, 0, 0, 0); resendLast();
    usleep(100000);
    countTaps(base, &downs, &ups);
    assert(downs == 1 && ups == 1);

    printf("contact missing from one message:\n");
    base = evCount();
    send3(1, 400, 200, 0, 0, 0);
    send3(0, 0, 0, 0, 0, 0);                      // finger not listed: released at once
    send3(2, 400, 200, 0, 0, 0);                  // ...but it is still there: must not press again
    send3(2, 401, 200, 0, 0, 0);
    send3(3, 401, 200, 0, 0, 0);
    usleep(100000);
    countTaps(base, &downs, &ups);
    assert(downs == 1 && ups == 1);

    printf("pause longer than the idle release, then the same touch goes on:\n");
    base = evCount();
    send3(1, 900, 300, 0, 0, 0);
    usleep(700000);                               // idle release
    send3(2, 900, 300, 0, 0, 0);
    send3(2, 905, 300, 0, 0, 0);
    send3(3, 905, 300, 0, 0, 0);
    usleep(100000);
    countTaps(base, &downs, &ups);
    assert(downs == 1 && ups == 1);

    printf("a new touch after a released one still works:\n");
    base = evCount();
    send3(1, 100, 100, 0, 0, 0);
    send3(3, 100, 100, 0, 0, 0);
    usleep(100000);
    countTaps(base, &downs, &ups);
    assert(downs == 1 && ups == 1);

    printf("touch whose down was lost after an idle release: still a tap\n");
    base = evCount();
    send3(1, 200, 100, 0, 0, 0);
    usleep(700000);                               // idle release ("up" lost)
    loseMessage();                                // ...and the next touch's "down" lost too
    send3(2, 250, 150, 0, 0, 0);
    send3(3, 250, 150, 0, 0, 0);
    usleep(100000);
    countTaps(base, &downs, &ups);
    assert(downs == 2 && ups == 2);

    printf("test_touch OK\n");
    return 0;
}
