// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
//
// volcap -- standalone volume-key capture / prompter. Nothing else in nbt-aad needs to run.
//
//   volcap [logfile]         (default logfile /mnt/umass01200t7/volcap.log)
//
// Prints, in order:   press volume up 5x / wait 5 s / press volume down 5x / wait 5 s
// while logging every MOST control message it is allowed to see, with a timestamp, to stdout and to the logfile.
//
// Why this exists: the volume buttons are on FBlock 0x15 (MFL 0x15/02 = CAudioVolumeButton fkt 0x0401, centre-console
// CSE 0x15/0x0C = fkt 0x0C01) and the AudioMaster level is FBlock 0x20 inst 0 (CVolumeActual fkt 0x0200). Those telegrams
// are routed point-to-point by DSYSMOSTServer, so whether an observer sees them at all is the open question. This tool
// registers for those FBlocks on top of the keys nbt-aad already asks for, so one run answers it: five lines at your
// presses = captured; silence while the on-screen indicator moves = never routed to us. Lines for 0x15/0x20 are marked
// "<<" so they stand out.

#include "dsi_client.hpp"
#include "most_msg.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

namespace nbt {

namespace {

FILE* g_log = NULL;
uint32_t g_client = 0xffffffffu;
int g_stage = 0;              // 0 name sent, 1 registered, 2 dev ctrls sent (connection thread only)
unsigned g_msgs = 0;
volatile bool g_armed = false;
struct timespec g_t0;

double nowSec() {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)(t.tv_sec - g_t0.tv_sec) + (double)(t.tv_nsec - g_t0.tv_nsec) / 1e9;
}

// stdout first (line buffered, so the person standing at the car sees it immediately), then the logfile.
void both(const char* s) {
    fputs(s, stdout);
    fflush(stdout);
    if (g_log) { fputs(s, g_log); fflush(g_log); }
}

void say(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    both(buf);
}

void onAttached(DsiClient* c, void*) {
    say("[volcap] attached to MOSTSubSystem.DSYSMOSTServer, registering MOST client\n");
    std::vector<uint8_t> p;
    DsiClient::putString(p, "NBTAAD_VOLCAP");
    g_stage = 1;
    c->sendRequest(0xF, p);
    static const uint32_t kAttrs[] = { 0xC0000002u, 0xC0000003u, 0xC0000001u, 0x80000001u, 0x80000002u,
                                       0x80000003u, 0x80000004u, 0x80000005u, 0x80000006u, 0x80000007u };
    for (size_t i = 0; i < sizeof(kAttrs) / sizeof(kAttrs[0]); ++i) c->setNotification(kAttrs[i]);
}

void onData(DsiClient* c, uint32_t, uint32_t id, uint32_t, const uint8_t* pl, uint32_t len, void*) {
    if (id == 0x80000000u && len >= 6) {
        g_client = mostRdU32(pl);
        unsigned dev = mostRdU16(pl + 4);
        say("[volcap] registered: clientId=%u deviceId=0x%04X%s\n", g_client, dev,
            g_client == 0xffffffffu ? "  (REGISTRATION REFUSED)" : "");
        if (g_client != 0xffffffffu && g_stage < 2) {
            c->setNotification(0xC0000000u);
            // The keys nbt-aad already registers, plus the two volume-bearing FBlock instances and AudioMaster, so the
            // server's own filtering cannot hide them from us any more than it already does.
            static const uint16_t kKeys[] = { 0xE510, 0xE503, 0xE505, 0xE512, 0xE514, 0xE501, 0xE502,
                                              0x1502, 0x150C, 0x2000, 0x2001 };
            std::vector<uint16_t> keys(kKeys, kKeys + sizeof(kKeys) / sizeof(kKeys[0]));
            g_stage = 2;
            c->sendRequest(1, buildRegisterDevCtrls(g_client, keys));
            say("[volcap] registerDevCtrls sent (%u keys: 10x iDrive/MFL + 1502/150C + AudioMaster 2000/2001)\n",
                (unsigned)keys.size());
            g_armed = true;
        }
        return;
    }
    if (id != 0x80000001u) return;

    MostMsg m;
    if (!parseMostMessage(pl, len, m)) return;
    ++g_msgs;

    char hex[64] = "";
    unsigned n = (unsigned)m.data.size();
    if (n > 12) n = 12;
    for (unsigned i = 0; i < n; ++i) snprintf(hex + i * 3, sizeof(hex) - i * 3, "%02X ", m.data[i]);

    // "<<" marks the FBlocks that carry volume (0x15 control elements, 0x20 AudioMaster).
    const char* mark = (m.fblock == 0x15 || m.fblock == 0x20) ? " <<" : "";

    say("+%7.3f fb=%02X i=%02X fkt=%04X op=%02X len=%2u | %s%s\n",
        nowSec(), m.fblock, m.inst, m.fkt, m.opType, (unsigned)m.data.size(), hex, mark);
}

} // namespace

} // namespace nbt

int main(int argc, char** argv) {
    using namespace nbt;
    clock_gettime(CLOCK_MONOTONIC, &g_t0);

    const char* logPath = (argc > 1) ? argv[1] : "/mnt/umass01200t7/volcap.log";
    g_log = fopen(logPath, "w");
    setvbuf(stdout, NULL, _IOLBF, 0);

    say("volcap -- volume key capture\n");
    if (g_log) say("logfile: %s\n", logPath);
    else       say("logfile: %s (cannot write, stdout only)\n", logPath);

    DsiClient c;
    c.setQuiet(true);
    c.setCallbacks(onAttached, onData, NULL);
    if (!c.open("DSYSMOSTServer", "MOSTSubSystem", 4, 5)) {
        say("[volcap] cannot open MOSTSubSystem.DSYSMOSTServer -- presses will not be captured\n");
    }

    // Give the registration a few seconds so the presses land inside a live capture.
    for (int i = 0; i < 100 && !g_armed; ++i) sleep(1);
    if (g_armed) say("[volcap] armed (%u messages so far)\n", g_msgs);
    else         say("[volcap] NOT armed -- registration did not complete; presses will not be captured\n");

    say("\n>>> press volume up 5x\n\n");
    sleep(5);
    say(">>> press volume down 5x\n\n");
    sleep(5);
    sleep(2);   // tail, so the last press is already on disk

    say("[volcap] done: %u messages captured, logfile %s\n", g_msgs, g_log ? logPath : "(none)");
    if (g_log) fclose(g_log);
    return 0;
}
