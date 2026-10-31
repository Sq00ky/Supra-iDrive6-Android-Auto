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
// Up to two contacts in the real HMI layout (touch_frame.hpp). state 0 = contact not listed.
static void send3(int s0, int x0, int y0, int s1, int x1, int y1) {
    TouchContact c[2];
    int n = 0;
    if (s0) { c[n].id = 0; c[n].state = s0; c[n].x = x0; c[n].y = y0; c[n].size = 80; ++n; }
    if (s1) { c[n].id = 1; c[n].state = s1; c[n].x = x1; c[n].y = y1; c[n].size = 80; ++n; }
    unsigned char f[112];
    buildTouchFrame(f, c, n, s_ctr);
    s_ctr = (unsigned char)(s_ctr + 0x40);
    if (getenv("TOUCH_SPLIT")) {          // 1440x540 head units: one message over several short datagrams
        static const int parts[3] = { 2, 5, 5 };
        unsigned char dg[3 * 112];
        int sz[3];
        const int nd = splitTouchFrame(f, parts, 3, dg, sz);
        for (int k = 0; k < nd; ++k) sendto(s_sock, dg + 112 * k, (size_t)sz[k], 0, (sockaddr*)&s_to, sizeof(s_to));
    } else {
        sendto(s_sock, f, sizeof(f), 0, (sockaddr*)&s_to, sizeof(s_to));
    }
    usleep(30000);
}
int main() {
    setenv("NBT_TOUCH_SY", "1.0", 1); setenv("NBT_TOUCH_H", "480", 1);
    assert(startTouchForward(30599, &cb, NULL));
    usleep(200000);
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
    // lost "up": finger down, then a second down on the same slot -> synthesized release then press; then silence -> idle release
    size_t base = g_ev.size();
    pthread_mutex_unlock(&g_mx);
    send3(1, 500, 100, 0, 0, 0);
    send3(1, 600, 120, 0, 0, 0);
    usleep(700000);
    pthread_mutex_lock(&g_mx);
    for (size_t i = base; i < g_ev.size(); ++i) printf("lost-up ev action=%d n=%d\n", g_ev[i].action, g_ev[i].n);
    assert(g_ev.size() >= base + 4);
    assert(g_ev[base].action == 0 && g_ev[base + 1].action == 1 && g_ev[base + 2].action == 0 && g_ev[base + 3].action == 1);
    pthread_mutex_unlock(&g_mx);
    printf("test_touch OK\n");
    return 0;
}
