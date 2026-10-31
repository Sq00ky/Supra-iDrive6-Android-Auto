// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
//
// NBT-AAD: native wireless Android Auto daemon for the BMW NBT Evo ID6 head unit (Toyota GR Supra J29).
// Official source: https://github.com/Sq00ky/Supra-iDrive6-Android-Auto
//
// This file (and the whole of NBT-AAD) is licensed under the following terms:
//
// # PolyForm Noncommercial License 1.0.0
//
// <https://polyformproject.org/licenses/noncommercial/1.0.0>
//
// ## Acceptance
//
// In order to get any license under these terms, you must agree to them as both strict obligations and conditions to all your licenses.
//
// ## Copyright License
//
// The licensor grants you a copyright license for the software to do everything you might do with the software that would otherwise infringe the licensor's copyright in it for any permitted purpose.  However, you may only distribute the software according to [Distribution License](#distribution-license) and make changes or new works based on the software according to [Changes and New Works License](#changes-and-new-works-license).
//
// ## Distribution License
//
// The licensor grants you an additional copyright license to distribute copies of the software.  Your license to distribute covers distributing the software with changes and new works permitted by [Changes and New Works License](#changes-and-new-works-license).
//
// ## Notices
//
// You must ensure that anyone who gets a copy of any part of the software from you also gets a copy of these terms or the URL for them above, as well as copies of any plain-text lines beginning with `Required Notice:` that the licensor provided with the software.  For example:
//
// > Required Notice: Copyright Yoyodyne, Inc. (http://example.com)
//
// ## Changes and New Works License
//
// The licensor grants you an additional copyright license to make changes and new works based on the software for any permitted purpose.
//
// ## Patent License
//
// The licensor grants you a patent license for the software that covers patent claims the licensor can license, or becomes able to license, that you would infringe by using the software.
//
// ## Noncommercial Purposes
//
// Any noncommercial purpose is a permitted purpose.
//
// ## Personal Uses
//
// Personal use for research, experiment, and testing for the benefit of public knowledge, personal study, private entertainment, hobby projects, amateur pursuits, or religious observance, without any anticipated commercial application, is use for a permitted purpose.
//
// ## Noncommercial Organizations
//
// Use by any charitable organization, educational institution, public research organization, public safety or health organization, environmental protection organization, or government institution is use for a permitted purpose regardless of the source of funding or obligations resulting from the funding.
//
// ## Fair Use
//
// You may have "fair use" rights for the software under the law. These terms do not limit them.
//
// ## No Other Rights
//
// These terms do not allow you to sublicense or transfer any of your licenses to anyone else, or prevent the licensor from granting licenses to anyone else.  These terms do not imply any other licenses.
//
// ## Patent Defense
//
// If you make any written claim that the software infringes or contributes to infringement of any patent, your patent license for the software granted under these terms ends immediately. If your company makes such a claim, your patent license ends immediately for work on behalf of your company.
//
// ## Violations
//
// The first time you are notified in writing that you have violated any of these terms, or done anything with the software not covered by your licenses, your licenses can nonetheless continue if you come into full compliance with these terms, and take practical steps to correct past violations, within 32 days of receiving notice.  Otherwise, all your licenses end immediately.
//
// ## No Liability
//
// ***As far as the law allows, the software comes as is, without any warranty or condition, and the licensor will not be liable to you for any damages arising out of these terms or the use or nature of the software, under any kind of legal claim.***
//
// ## Definitions
//
// The **licensor** is the individual or entity offering these terms, and the **software** is the software the licensor makes available under these terms.
//
// **You** refers to the individual or entity agreeing to these terms.
//
// **Your company** is any legal entity, sole proprietorship, or other kind of organization that you work for, plus all organizations that have control over, are under the control of, or are under common control with that organization.  **Control** means ownership of substantially all the assets of an entity, or the power to direct its management and policies by vote, contract, or otherwise.  Control can be direct or indirect.
//
// **Your licenses** are all the licenses granted to you for the software under these terms.
//
// **Use** means anything you do with the software requiring one of your licenses.

#include "nbt_env.hpp"
#include "car_config.hpp"
#include "screen_video.h"
#include "audio_manager.hpp"
#include "video_manager.hpp"
#include "license_text.hpp"
#include "tcp_server.hpp"
#include "usb_accessory.hpp"
#include "aa_session.hpp"
#include "bluetooth_manager.hpp"
#include "wireless_setup.hpp"
#include "touch_probe.hpp"
#include "ccs_input.hpp"
#include "crash_handler.hpp"
#include "a2i_stream.hpp"
#include "a2i_client.h"
#include "voice_scenario.hpp"
#include "bt_play.hpp"
#include "usb_log.hpp"
#include "spp_rereg.hpp"
#include "spp_link_policy.hpp"
#include "spp_direction.hpp"
#include "neutral_resume.hpp"
#include <termios.h>

#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#ifdef __QNX__
#include <sys/sockio.h>
#endif
#include <net/if.h>

static volatile sig_atomic_t g_running = 1;
// Seconds since boot for the lifecycle log lines (the daemon log had no timestamps, so a bootloop/slow-quit log could
// not be timed against the iDrive app's log, which uses the same clock).
static double upSec() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}
static uint32_t upMs() { return (uint32_t)(upSec() * 1000.0); }
static nbt::AASession g_session;

// ---- Single instance + control port (NBT_CTL=0 disables, NBT_CTL_PORT overrides 52790) -------------------
// The iDrive menu entry (plugin/libhmiaad.so) simply starts /opt/aa/nbt-aad every time it is selected. The first
// instance owns UDP 127.0.0.1:52790; a later instance finds the port taken, sends "show" to the running daemon
// and exits before touching the screen, firewall or Bluetooth. "show" puts the overlay back on top, "quit" stops.
static volatile sig_atomic_t g_showReq = 0;
static volatile sig_atomic_t g_hideReq = 0;   // iDrive app: a hard key took iDrive away from our held state (Fork F)
static int g_ctlFd = -1;

// ---- Screen ownership: who should see the display right now ---------------------------------------------
// The overlay is shown only while nothing else claims the screen:
//   g_userExit  - the driver left Android Auto (phone's "exit to car" icon, VideoFocusRequest UNFOCUSED); cleared by
//                 the iDrive Android Auto entry ("show"), by the phone asking for focus again, or by a new session.
//   g_gear      - car gear from the iDrive app (CDS "driving.gear", ctl message "gear <n>", also read from
//                 /dev/shmem/nbt-aad-gear at start). Reverse (NBT_GEAR_REVERSE, default 2) gives iDrive the screen
//                 for the camera; leaving reverse brings Android Auto back unless the driver had exited.
//   g_pdc       - park distance control / park assist state from the iDrive app (CDS "sensors.PDCStatus", ctl
//                 message "pdc <n>", /dev/shmem/nbt-aad-pdc at start). While it is active iDrive gets the screen for
//                 the parking view, from wherever the driver is - like native CarPlay when the park-assist button is
//                 pressed. NBT_PDC_ACTIVE lists the active values (default 1).
//   g_inputToPhone - touch / knob events reach the phone only while the overlay is up.
static volatile sig_atomic_t g_userExit = 0;
static volatile sig_atomic_t g_carExit = 0;   // minimized by the BACK hold: only the iDrive entry / a new session undoes it
// Phone focus and mic open/close used to be one slot. The main loop reads a value and then stores 0,
// so a newer request written in between was lost. A short queue keeps both.
enum { kSlotQ = 8 };
static int g_focusQ[kSlotQ];
static int g_focusN = 0;
static int g_micQ[kSlotQ];
static int g_micN = 0;
static pthread_mutex_t g_slotMu = PTHREAD_MUTEX_INITIALIZER;
static void pushSlot(int* q, int* n, int v) {
    pthread_mutex_lock(&g_slotMu);
    if (*n < kSlotQ) q[(*n)++] = v;
    else {
        memmove(q, q + 1, (kSlotQ - 1) * sizeof(int));
        q[kSlotQ - 1] = v;
    }
    pthread_mutex_unlock(&g_slotMu);
}
static bool popSlot(int* q, int* n, int* v) {
    pthread_mutex_lock(&g_slotMu);
    if (*n <= 0) { pthread_mutex_unlock(&g_slotMu); return false; }
    *v = q[0];
    memmove(q, q + 1, (size_t)(*n - 1) * sizeof(int));
    --*n;
    pthread_mutex_unlock(&g_slotMu);
    return true;
}
static volatile int g_gear = -1;
static volatile int g_pdc = -1;                    // -1 = unknown (no report yet)

// ---- driving mode (Sport / Comfort / ECO PRO ...), like reverse -------------------------------------------------
// The iDrive app relays CDS driving.mode / driving.mode35up ('{"mode":%d}', libhmiremoting CCdsPropertyDrivingMode::
// onStatus, from the vehicle's FES VehicleDynamicSwitchHMI; 15 / 63 = not valid) as "drivemode <prop> <n>". With the
// iDrive menu checkbox "Show driving mode changes" (flag file below, read on every change) a change of mode makes
// Android Auto step aside for NBT_DRIVEMODE_SHOW_MS (default 3500) so iDrive's own driving-mode popup is visible, then
// it comes back by itself. The first value after start is only the baseline. Not car-verified.
static const char* const kDriveModeFlag   = "/var/opt/aa/show-drivemode";
static const char* const kDriveModeManual = "/opt/aa/show-drivemode";
static volatile int g_driveMode = -1;              // last valid mode (-1 = none yet)
static volatile uint32_t g_dmPeekUntil = 0;        // upMs() until which iDrive keeps the screen (0 = never set)
static volatile uint32_t g_dmPeekFrom = 0;         // upMs() from which it does (change + NBT_DRIVEMODE_DELAY_MS)
static volatile sig_atomic_t g_dmShown = 0;        // main loop: currently stepped aside for it
static volatile sig_atomic_t g_mainWake = 0;       // cuts the main loop's 250 ms sleep short (driving-mode change)
static bool showDriveModeOn() { return access(kDriveModeFlag, F_OK) == 0 || access(kDriveModeManual, F_OK) == 0; }
static int driveModeShowMs() {
    const char* e = getenv("NBT_DRIVEMODE_SHOW_MS");
    int v = (e && *e) ? atoi(e) : 3500;
    return v < 500 ? 500 : (v > 15000 ? 15000 : v);
}
// v1.8.45 stepped aside ~0.3 s after the CDS value, before iDrive had drawn its driving-mode popup: the driver caught
// iDrive's blank Android Auto page in between (Supra, 2026-10-29). Wait this long first. 0 = at once.
static int driveModeDelayMs() {
    const char* e = getenv("NBT_DRIVEMODE_DELAY_MS");
    int v = (e && *e) ? atoi(e) : 400;
    return v < 0 ? 0 : (v > 3000 ? 3000 : v);
}
static bool driveModePeekDue() {   // a pending step-aside whose start time has come
    uint32_t until = g_dmPeekUntil, from = g_dmPeekFrom, now = upMs();
    return !g_dmShown && until != 0 && (int32_t)(now - from) >= 0 && (int32_t)(until - now) > 0;
}
static void onDriveModeMsg(const char* s) {
    // "<prop> <n> [<prev>]": rhmiapp-22 adds the value it had before (-1 = none, -2 = its first value came long after
    // subscribing). The app gets the current mode when it subscribes at iDrive start, but the daemon starts later and
    // only hears about changes, so the first press used to become the daemon's baseline (Supra nbt-aad-28: mode 3 at
    // 130.747, HMI popup took focus 30 ms later, AA stayed on top). With <prev> it is a change (car-verified,
    // nbt-aad-29: '4 -> 3' on the first press, iDrive visible ~0.4 s after the CDS value).
    int prop = -1, v = -1, prev = -1;
    int nf = sscanf(s, "%d %d %d", &prop, &v, &prev);
    if (nf < 2) { printf("[DriveMode] bad message '%s'\n", s); return; }
    if (nf < 3) prev = -1;
    const char* name = prop == 1 ? "driving.mode35up" : "driving.mode";
    bool valid = v >= 0 && !(prop == 1 ? v == 63 : v == 15);
    if (!valid) { printf("[DriveMode] %s = %d (not valid)\n", name, v); return; }
    int was = g_driveMode;
    g_driveMode = v;
    if (was < 0) {
        bool prevOk = prev >= 0 && !(prop == 1 ? prev == 63 : prev == 15) && prev != v;
        if (prevOk) was = prev;                                  // daemon restarted under a running iDrive app
        else if (prev != -2) { printf("[DriveMode] [%9.3f] %s = %d (initial mode)\n", upSec(), name, v); return; }
        else printf("[DriveMode] [%9.3f] %s = %d: first value, but long after the app subscribed (a change)\n",
                    upSec(), name, v);
    }
    if (was == v) return;
    if (!showDriveModeOn()) {
        printf("[DriveMode] %s %d -> %d (Show driving mode changes is off)\n", name, was, v);
        return;
    }
    const uint32_t now = upMs();
    const int delay = driveModeDelayMs();
    if (g_dmShown) {
        // Already stepped aside: another change only makes iDrive keep the screen longer.
        uint32_t until = now + (uint32_t)driveModeShowMs();
        g_dmPeekUntil = until ? until : 1;
    } else {
        uint32_t from = now + (uint32_t)delay;
        g_dmPeekFrom = from;
        uint32_t until = from + (uint32_t)driveModeShowMs();
        g_dmPeekUntil = until ? until : 1;
        if (!delay) g_mainWake = 1;
    }
    printf("[DriveMode] [%9.3f] %s %d -> %d: iDrive gets the screen%s%s for %d ms for its driving-mode display\n",
           upSec(), name, was, v, g_dmShown ? " (still)" : "", g_dmShown || !delay ? "" : " after NBT_DRIVEMODE_DELAY_MS",
           driveModeShowMs());
}
static const char* const kPdcFile = "/dev/shmem/nbt-aad-pdc";
// Starts at 0: the first main-loop tick writes the iDrive state file before it computes this, and a 1 here made
// every daemon start report "Android Auto on screen" for one tick (app log: hold fired, then 'stepped aside: event
// 50 -> iDrive main menu' a second later, before any phone was connected).
static volatile sig_atomic_t g_inputToPhone = 0;
static const char* const kGearFile = "/dev/shmem/nbt-aad-gear";
// Car day/night (iDrive app: CDS vehicle.dayNightChangeOver, message "daynight <n>", /dev/shmem/nbt-aad-daynight at
// start). Mapped the way factory CarPlay does (HmiMain3D CHmiCarPlayService::onDayNightChangeOver @0x4583a4):
// 2 = night, 1 = day, 0 = "no change", 3 = "not a valid signal". Sent to the phone as Android Auto night mode.
static volatile int g_dayNight = -1;
static const char* const kDayNightFile = "/dev/shmem/nbt-aad-daynight";
static int dayNightToNight(int v) { return v == 2 ? 1 : (v == 1 ? 0 : -1); }
static void readDayNightFile() {
    FILE* f = fopen(kDayNightFile, "r");
    if (!f) return;
    int v = -1;
    if (fscanf(f, "daynight %d", &v) == 1) {
        g_dayNight = v;
        printf("[Night] initial day/night change-over %d (from %s)\n", v, kDayNightFile);
    }
    fclose(f);
}

static void readPdcFile() {
    FILE* f = fopen(kPdcFile, "r");
    if (!f) return;
    int v = -1;
    if (fscanf(f, "pdc %d", &v) == 1) {
        g_pdc = v;
        printf("[Park] initial park-assist state %d (from %s)\n", v, kPdcFile);
    }
    fclose(f);
}

// sensors.PDCStatus values (libhmiremoting CCdsPropertyPDCStatus_Function::onStatus @0x2181ac, Ghidra): CAN status
// nibble 0xA_ -> 1, 0x7_ -> 0, 0xFF -> 3 (signal invalid), anything else -> 2. 1 is taken as "park assist on".
// NBT_PDC_ACTIVE (comma list, e.g. "1,2") overrides. Unknown (-1, no report yet) is never active.
static bool pdcActive(int v) {
    if (v < 0) return false;
    static int parsed = 0, n = 0, list[16];
    if (!parsed) {
        parsed = 1;
        const char* e = getenv("NBT_PDC_ACTIVE");
        for (const char* p = e; p && *p && n < 16;) {
            char* end = NULL;
            long x = strtol(p, &end, 0);
            if (end == p) break;
            list[n++] = (int)x;
            p = (*end == ',') ? end + 1 : end;
        }
    }
    if (n) { for (int k = 0; k < n; ++k) if (list[k] == v) return true; return false; }
    return v == 1;
}

static void readGearFile() {
    FILE* f = fopen(kGearFile, "r");
    if (!f) return;
    int g = -1;
    if (fscanf(f, "gear %d", &g) == 1) {
        g_gear = g;
        printf("[Gear] initial gear %d (from %s)\n", g, kGearFile);
    }
    fclose(f);
}

static uint16_t ctlPort() {
    const char* p = getenv("NBT_CTL_PORT");
    int v = (p && *p) ? atoi(p) : 52790;
    return (uint16_t)((v > 0 && v < 65536) ? v : 52790);
}

static void* ctlThread(void*) {
    char buf[32];
    while (g_running) {
        struct sockaddr_in from;
        socklen_t flen = sizeof(from);
        ssize_t n = recvfrom(g_ctlFd, buf, sizeof(buf) - 1, 0, (struct sockaddr*)&from, &flen);
        if (n <= 0) { if (n < 0 && errno != EINTR) usleep(200000); continue; }
        buf[n] = 0;
        if (!strncmp(buf, "ping", 4)) {
            const char* pong = "pong";
            sendto(g_ctlFd, pong, 4, 0, (struct sockaddr*)&from, flen);
            continue;
        }
        if (!strncmp(buf, "show", 4)) { printf("[Ctl] 'show' from a second launch (iDrive menu): overlay to front.\n"); g_showReq = 1; }
        else if (!strncmp(buf, "gear ", 5)) {
            int g = atoi(buf + 5);
            if (g != g_gear) printf("[Gear] gear %d -> %d (from iDrive app)\n", (int)g_gear, g);
            g_gear = g;
        }
        else if (!strncmp(buf, "pdc ", 4)) {
            int v = atoi(buf + 4);
            if (v != g_pdc) printf("[Park] park-assist state %d -> %d (from iDrive app)\n", (int)g_pdc, v);
            g_pdc = v;
        }
        else if (!strncmp(buf, "daynight ", 9)) {
            int v = atoi(buf + 9);
            if (v != g_dayNight) printf("[Night] day/night change-over %d -> %d (from iDrive app)\n", (int)g_dayNight, v);
            g_dayNight = v;
        }
        else if (!strncmp(buf, "drivemode ", 10)) onDriveModeMsg(buf + 10);
        else if (!strncmp(buf, "quit", 4)) { printf("[Ctl] [%9.3f] 'quit' received.\n", upSec()); g_running = 0; }
        else if (!strncmp(buf, "hide", 4)) { printf("[Ctl] 'hide' from the iDrive app: a hard key took iDrive elsewhere.\n"); g_hideReq = 1; }
        else printf("[Ctl] unknown request '%s' ignored.\n", buf);
    }
    return NULL;
}

// Returns false when another instance is running (request forwarded, caller exits).
static bool ctlClaimOrForward() {
    if (nbt::envFlag("NBT_CTL") == 0) { printf("[Ctl] NBT_CTL=0: single-instance control port off.\n"); return true; }
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons(ctlPort());
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) { printf("[Ctl] socket failed (errno %d): continuing without control port.\n", errno); return true; }
    if (bind(fd, (struct sockaddr*)&a, sizeof(a)) == 0) {
        g_ctlFd = fd;
        pthread_t t;
        if (pthread_create(&t, NULL, &ctlThread, NULL) == 0) pthread_detach(t);
        printf("[Ctl] control port 127.0.0.1:%u (single instance).\n", (unsigned)ctlPort());
        return true;
    }
    int e = errno;
    if (e == EADDRINUSE) {
        const char* req = "show";
        sendto(fd, req, strlen(req), 0, (struct sockaddr*)&a, sizeof(a));
        close(fd);
        printf("[Ctl] nbt-aad is already running: asked it to come to the front. Exiting.\n");
        return false;
    }
    close(fd);
    printf("[Ctl] bind failed (errno %d): continuing without control port.\n", e);
    return true;
}


// ---- Native wireless bring-up (default on; NBT_WIRELESS=0 disables) ---------------------------------
// Replaces "Start Head Unit Server" on the phone: the phone connects to us over
// Bluetooth RFCOMM (/dev/btspp<N>, served by Connectivity), we send the CarPlay AP
// credentials and our ip:port, and the phone then opens the TCP session itself.
static uint16_t g_aaPort = 5277;
static volatile int g_btLink = 0;              // wireless thread: phone's Bluetooth setup link (/dev/btspp0) is open

// Menu-entry status for the iDrive plugin (libNbtAad.so polls it once a second): "<state> <pid> <overlay>" in
// /dev/shmem/nbt-aad-state, state = idle | connecting | connected, overlay = 1 while Android Auto is on screen and
// the knob drives the phone (the plugin then ignores Force Stop / Restart: those presses can only be stray knob
// input reaching the iDrive menu under the overlay), 3 = same, MENU / COM / MEDIA / NAV / MAP went to the phone in the last 3 s
// (the plugin re-holds iDrive instead of treating MENU as a hard key), 2 = same, and BACK is being held (hold timer running): iDrive
// sees the BACK press too and leaves the blank state, and the plugin must not put it back while the hold may still
// minimize Android Auto (car 2026-10-28, rhmiapp-11: refocus fought the minimize).
static const char* const kUiStateFile = "/dev/shmem/nbt-aad-state";
// The iDrive app's "Start with the car" bootloop guard (rhmiapp-20): it records each autostart here and skips the
// autostart after 3 within 15 min. A clean exit or 10 min of uptime clears it (persistent: survives the reset).
static const char* const kBootGuardFile = "/var/opt/aa/bootguard";
static void writeUiState(const char* state, int overlay) {
    // One fixed record, written over the previous bytes before the file is shortened. QNX /dev/shmem
    // has no rename(), and opening with O_TRUNC left a moment where the plugin read nothing and then
    // tried to bind the control port itself.
    // "%-11.11s %10d %d\n" is 25 bytes for a one-digit overlay (0 shown, 1 on screen, 2 BACK held, 3 menu key).
    // fscanf("%23s %d %d") still matches.
    char rec[32];
    int n = snprintf(rec, sizeof(rec), "%-11.11s %10d %d\n", state ? state : "idle", (int)getpid(), overlay);
    if (n < 0) return;
    int fd = open(kUiStateFile, O_WRONLY | O_CREAT, 0666);
    static bool s_warned = false;
    if (fd < 0) {
        if (!s_warned) { printf("[UiState] cannot write %s: %s\n", kUiStateFile, strerror(errno)); s_warned = true; }
        return;
    }
    fchmod(fd, 0666);   // readable by HmiMain3D whatever our umask is
    if (lseek(fd, 0, SEEK_SET) == (off_t)-1 && !s_warned) {
        printf("[UiState] seek failed on %s\n", kUiStateFile);
        s_warned = true;
    }
    ssize_t w = write(fd, rec, (size_t)n);
    if (w == n) ftruncate(fd, n);   // drop a longer old record only after the new line is in place
    else if (!s_warned) { printf("[UiState] short write to %s\n", kUiStateFile); s_warned = true; }
    close(fd);
}
static volatile bool g_sessionActive = false;   // AA session running (set by connect/end callbacks)
static nbt::VideoManager* g_video = NULL;       // for the session callbacks (valid between init and teardown)
static nbt::AudioManager* g_audio = NULL;       // same lifetime rule as g_video
// Bluetooth audio mode (default; NBT_AUDIO_BT=0 turns it off): AA audio stays on the phone's Bluetooth A2DP link; we only select the car's BT source.
static bool g_btAudio = false;
static nbt::AudioRoute g_btRoute;
static uint32_t g_btSrc = 0x3a;                 // eSRC_A2DP (audioconn -l: ConnId 0xc7 AuxInBTAudio)
static uint32_t g_btSink = 0x1;                 // eSNK_SPEAKERS
// NBT_BT_CONN=<connId> (default 0xc7; 0 = old AudioManagement focus form `-b <src> <sink>`): request the car's
// Bluetooth source through the ConnectionMaster (`audioconn -b <connId>`, BuildHighSyncConnection: the one call the
// iDrive HMI makes when Bluetooth audio is picked, HmiMain3D RE 2026-10-29). CAR-VERIFIED (MINI F60, v1.8.35, 3 of 3
// sessions): granted at once and music starts by itself, even from FM radio, where the focus form was refused for all
// retries. `audioconn -l` lists eSRC_A2DP as ConnId 0xc7 (AuxInBTAudio srcNr 1) on the Supra and the MINI F60.
static uint32_t g_btConn = 0xc7;
static nbt::BtPlay g_btPlay;                    // NBT_BT_PLAY experiment: SourceActivity PLAY once the source is granted

static void onBtGranted(void*) { g_btPlay.play("car granted the Bluetooth source"); }   // audioconn worker thread
// NBT_BT_ROUTE_DELAY_MS (default 1500, 0 = at once): wait this long after the session starts before running
// audioconn. A phone that says ByeBye right after the ServiceDiscovery and reconnects every ~3 s (tester bootloop log,
// 2026-10-29) then never starts an audioconn at all: onSessionEnded() cancels the request while it waits.
static int btRouteDelayMs() {
    const char* e = getenv("NBT_BT_ROUTE_DELAY_MS");
    if (!e || !*e) return 1500;
    int v = atoi(e);
    return v < 0 ? 0 : (v > 10000 ? 10000 : v);
}
static bool btRequestSource() {
    if (g_btConn) return g_btRoute.build(g_btConn, &onBtGranted, NULL, btRouteDelayMs());
    return g_btRoute.buildFocus(g_btSrc, g_btSink, &onBtGranted, NULL, btRouteDelayMs());
}
static void btReleaseSource() {
    g_btPlay.stop("session ended");
    if (g_btConn) g_btRoute.remove(g_btConn);
    else g_btRoute.removeFocus(g_btSrc, g_btSink);
}
static void btDescribe(char* out, size_t n) {
    if (g_btConn) snprintf(out, n, "connection 0x%x (ConnectionMaster)", g_btConn);
    else snprintf(out, n, "source 0x%x -> sink 0x%x", g_btSrc, g_btSink);
}

// NBT_SPEECH=1 (Bluetooth audio mode only, OFF by default, test parked first): play Android Auto guidance / speech
// (channel 8, 16 kHz mono) on an A2I playback endpoint. In BT mode ch 8 must stay in the ServiceDiscovery (the phone
// needs >= 1 audio sink), so without this switch every spoken nav prompt is received, acked and dropped.
//   NBT_SPEECH_EP     endpoint (default /input_stereo_AirPlayAlt, opened at start-up as in the stable Milestone 2)
//   NBT_SPEECH_SRC    AM source requested with `audioconn -b <src> <sink>` while a prompt plays (default 0 = none
//                     since v1.6.7: AM refuses 0xba anyway and each request touches AM while BT holds focus),
//                     released with `-r` when the prompt ends
//   NBT_SPEECH_SINK   AM sink (default 0x1, speakers)
// Earlier A2I *write* experiments crashed the head unit; this uses only the factory prime/Start/GetBuffer/Out_Write
// loop (car-verified for 20k+ writes), opens the endpoint once on its own thread and never closes it mid-run.
static nbt::A2IPlaybackStream* g_speech = NULL;   // set by the open thread, then never changes
static volatile bool g_speechReady = false;
static nbt::PcmResampler g_speechRs;               // used on the session thread only
static std::vector<uint8_t> g_speechBuf;
static uint32_t g_speechSrc = 0;
static uint32_t g_speechSink = 0x1;
static bool g_speechRouted = false;
static uint32_t g_speechFrames = 0;
static uint32_t g_speechBytes = 0;
static int g_speechPeak = 0;
static uint32_t g_speechPrompts = 0;

static bool speechEnabled() {
    const char* e = getenv("NBT_SPEECH");
    return e && strcmp(e, "1") == 0;
}

static void* speechOpenThread(void*) {
    const char* ep = getenv("NBT_SPEECH_EP");
    if (!ep || !*ep) ep = A2I_ENDPOINT_NAV_ALT;
    // Ring holds ~12 s at 44.1 kHz stereo: the phone sends a prompt faster than real time (only max_unacked
    // limits it) and the ring drops the OLDEST bytes when full, which would cut the start of a long prompt.
    nbt::A2IPlaybackStream* s = new nbt::A2IPlaybackStream("Speech", 44100u * 4u * 12u);
    uint32_t rates[2] = { A2I_SAMPLE_RATE_44K, A2I_SAMPLE_RATE_48K };
    bool ok = false;
    for (int i = 0; i < 2 && !ok; ++i) {
        nbt::A2IStreamConfig cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.endpoint          = ep;
        cfg.fallback_endpoint = NULL;
        cfg.sample_rate       = rates[i];
        cfg.channels          = A2I_CHANNELS_STEREO;
        cfg.buffer_size       = A2I_BUFFER_SIZE_STEREO;
        cfg.cookie_primary    = A2I_COOKIE_LEGACY_PLAYBACK;
        cfg.cookie_alternate  = A2I_COOKIE_FACTORY;
        cfg.timeout_ms        = 2000;
        ok = s->open(cfg);
    }
    if (!ok) {
        printf("[Speech] could not open %s: spoken prompts stay off\n", ep);
        delete s;
        return NULL;
    }
    g_speechRs.configure(16000, 1, s->sampleRate(), s->channels());
    if (!s->start(true)) {
        printf("[Speech] playback worker did not start on %s: spoken prompts stay off\n", ep);
        return NULL;   // keep the handle (no close mid-run), just never feed it
    }
    g_speech = s;
    __sync_synchronize();   // resampler + pointer visible before the session thread sees ready
    g_speechReady = true;
    printf("[Speech] ready: %s %u Hz x%u, route %s 0x%x -> 0x%x\n", s->endpoint() ? s->endpoint() : ep,
           s->sampleRate(), s->channels(), g_speechSrc ? "audioconn" : "off", g_speechSrc, g_speechSink);
    return NULL;
}

static void speechInit() {
    if (!g_btAudio || !speechEnabled()) {
        printf("[Speech] spoken nav prompts: off%s\n", g_btAudio ? " (NBT_SPEECH=1 enables)" : "");
        return;
    }
    g_speechSrc = (uint32_t)nbt::envHex("NBT_SPEECH_SRC", 0);
    g_speechSink = (uint32_t)nbt::envHex("NBT_SPEECH_SINK", 0x1);
    printf("[Speech] NBT_SPEECH=1: opening the speech endpoint in the background\n");
    pthread_t t;
    if (pthread_create(&t, NULL, &speechOpenThread, NULL) == 0) pthread_detach(t);
    else printf("[Speech] could not start the open thread\n");
}

// Session thread: one ch 8 PCM frame (16 kHz mono s16le).
static void speechFrame(const uint8_t* pcm, size_t len) {
    if (!g_speechReady || !g_speech) return;
    for (size_t i = 0; i + 1 < len; i += 2) {
        int v = (int16_t)(pcm[i] | (pcm[i + 1] << 8));
        if (v < 0) v = -v;
        if (v > g_speechPeak) g_speechPeak = v;
    }
    ++g_speechFrames;
    g_speechBytes += (uint32_t)len;
    g_speechBuf.clear();
    g_speechRs.processBytes(pcm, len, g_speechBuf);
    if (!g_speechBuf.empty()) g_speech->write(&g_speechBuf[0], g_speechBuf.size());
}

// Session thread: ch 8 AV START / STOP indication.
static void speechState(bool started) {
    if (!g_speechReady || !g_speech) return;
    if (started) {
        ++g_speechPrompts;
        g_speechFrames = 0;
        g_speechBytes = 0;
        g_speechPeak = 0;
        g_speechRs.reset();
        printf("[Speech] prompt #%u started\n", g_speechPrompts);
        if (g_speechSrc && !g_speechRouted && g_btRoute.available())
            g_speechRouted = g_btRoute.buildFocus(g_speechSrc, g_speechSink);
    } else {
        // No flush: the queued tail still plays out (the STOP arrives right after the last frame).
        printf("[Speech] prompt #%u ended: %u frames, %u ms of audio, peak %d, endpoint writes %u underruns %u\n",
               g_speechPrompts, g_speechFrames, g_speechBytes / 32u, g_speechPeak, g_speech->writes(),
               g_speech->underruns());
        if (g_speechRouted) {
            g_btRoute.removeFocus(g_speechSrc, g_speechSink);
            g_speechRouted = false;
        }
    }
}

// Prints the Wi-Fi clients the head unit currently knows on the AP subnet (ARP table), so a log
// shows whether the phone actually joined the CarPlay network.
static void logStations(const char* tag) {
    FILE* p = popen("arp -an", "r");
    if (!p) return;
    char line[256];
    int n = 0;
    while (fgets(line, sizeof(line), p)) {
        if (strstr(line, "172.16.224.")) {
            size_t l = strlen(line);
            while (l && (line[l - 1] == '\n' || line[l - 1] == '\r')) line[--l] = 0;
            printf("[Wireless] AP client (%s): %s\n", tag, line);
            ++n;
        }
    }
    pclose(p);
    if (!n) printf("[Wireless] AP clients (%s): none in ARP table\n", tag);
}

// The head unit numbers its SPP links /dev/btspp0, 1, ... in connect order, so another Bluetooth
// serial user (BMW Connected / MyBMW) can own btspp0 while Android Auto lands on btspp1. Scan the
// nodes and skip the ones recognised as not ours until they disappear. NBT_BTSPP_DEV pins one node.
static const int kSppNodes = 8;
static bool g_sppForeign[kSppNodes];
static nbt::SppSilencePolicy g_sppSilence;   // silent (non-Android-Auto) links: MyBMW / BMW Connected / AAIdrive
static uint32_t monoSecs();

// BMW/MINI default: 2 silent links in a row put a node on hold for 60 s, so the car's own BMW Apps handler
// (NBTMediaMainApp A4AHandler on /dev/btspp0) keeps the MyBMW link that AAIdrive rides on. Toyota: off (no BMW
// Apps). NBT_SPP_SILENT_STRIKES=0 turns it off, NBT_SPP_SILENT_HOLD_S sets the hold.
static void configureSppSilence() {
    static bool s_done = false;
    if (s_done) return;
    s_done = true;
    const nbt::CarConfig& cc = nbt::carConfig();
    const char* e = getenv("NBT_SPP_SILENT_STRIKES");
    int limit = (e && *e) ? atoi(e) : (cc.bmw ? 2 : 0);
    const char* h = getenv("NBT_SPP_SILENT_HOLD_S");
    int hold = (h && *h) ? atoi(h) : 60;
    g_sppSilence.configure(limit, hold > 0 ? (uint32_t)hold : 60u);
    const char* pk = getenv("NBT_SPP_PEEK_MS");
    int peek = (pk && *pk) ? atoi(pk) : (cc.bmw ? 300 : 0);
    nbt::setSppPeekMs(peek);
    if (peek > 0)
        printf("[Wireless] listening %d ms on each new Bluetooth serial link before writing (a MyBMW link that speaks "
               "first is left alone; NBT_SPP_PEEK_MS=0 turns this off)\n", peek);
    if (limit > 0)
        printf("[Wireless] a Bluetooth serial link that never answers like Android Auto %d times in a row is left to the "
               "car's BMW Apps (MyBMW / BMW Connected / AAIdrive) for %u s (NBT_SPP_SILENT_STRIKES=0 turns this off)\n",
               limit, g_sppSilence.holdSeconds());
}

// Who opened each link (src/spp_direction.hpp): Android Auto links are opened by the phone (a GAP ServiceRequest
// comes first), BMW Apps links (MyBMW / BMW Connected / AAIdrive) by the car. Once this car has shown that (an Android
// Auto reply on a phone-opened link; remembered in /var/opt/aa/spp-model), car-opened links are never read or written:
// the car's A4AHandler keeps them. NBT_SPP_DIRECTION: 0 off, 1 always, 2 learn first (BMW/MINI default; Toyota 0).
static const char* const kSppModelFile = "/var/opt/aa/spp-model";
static bool g_sppSeen[kSppNodes];      // node existed at the last scan
static bool g_sppPhone[kSppNodes];     // ... and the phone opened it
static bool g_sppCarOwned[kSppNodes];  // ... the car opened it and the rule applies: leave it alone until it is gone

static void saveSppModel(int model) {
    mkdir("/var/opt/aa", 0755);
    FILE* f = fopen(kSppModelFile, "w");
    if (!f) { printf("[Wireless] could not save %s: %s\n", kSppModelFile, strerror(errno)); return; }
    fprintf(f, "%s\n", nbt::SppDirection::modelName((nbt::SppDirection::Model)model));
    fclose(f);
}

static void configureSppDirection() {
    static bool s_done = false;
    if (s_done) return;
    s_done = true;
    const nbt::CarConfig& cc = nbt::carConfig();
    const char* e = getenv("NBT_SPP_DIRECTION");
    int mode = (e && *e) ? atoi(e) : (cc.bmw ? 2 : 0);
    if (mode < 0 || mode > 2) mode = 2;
    const char* w = getenv("NBT_SPP_DIRECTION_WINDOW_S");
    int win = (w && *w) ? atoi(w) : 15;
    int model = nbt::SppDirection::kUnknown;
    FILE* f = fopen(kSppModelFile, "r");
    if (f) {
        char buf[32] = { 0 };
        if (fgets(buf, sizeof(buf), f)) model = nbt::SppDirection::parseModel(buf);
        fclose(f);
    }
    nbt::sppDirConfigure(mode, win > 0 ? (uint32_t)win : 15u, model);
    if (mode == 0) return;
    printf("[Wireless] Bluetooth link direction check %s (NBT_SPP_DIRECTION=%d, model '%s' from %s): Android Auto links "
           "are opened by the phone; links the car opens itself are its BMW Apps link (MyBMW / BMW Connected / "
           "AAIdrive)%s\n", mode == 1 ? "on" : "learning", mode, nbt::SppDirection::modelName((nbt::SppDirection::Model)model),
           kSppModelFile, nbt::sppDirGate() ? " and are left to the car" : "; they are left to the car once Android Auto "
           "has answered on a phone-opened link");
}

// A link we handled ended: update the learned model. spoke = an Android Auto frame came back.
static void noteSppDirection(const char* path, bool spoke) {
    int i = -1;
    if (!path || sscanf(path, "/dev/btspp%d", &i) != 1 || i < 0 || i >= kSppNodes) return;
    int m = nbt::sppDirLinkResult(g_sppPhone[i], spoke);
    if (m < 0) return;
    if (m == nbt::SppDirection::kPhoneOpensAa)
        printf("[Wireless] Android Auto answered on a link the phone opened: from now on links the car opens itself "
               "(its BMW Apps link: MyBMW / BMW Connected / AAIdrive) are left to the car\n");
    else
        printf("[Wireless] Android Auto answered on a link the car opened: the direction check is switched off on this "
               "car (delete %s to learn again)\n", kSppModelFile);
    saveSppModel(m);
}

static int sppIndex(const char* path) {
    int i = -1;
    if (!path || sscanf(path, "/dev/btspp%d", &i) != 1 || i < 0 || i >= kSppNodes) return -1;
    return i;
}

// A link ended without a session. spoke = the peer sent at least one Android Auto wireless frame.
static void noteSppLinkEnd(const char* path, bool spoke) {
    int i = sppIndex(path);
    if (i < 0) return;
    if (!spoke && g_sppSilence.limit() > 0)
        printf("[Wireless] %s never answered like Android Auto (strike %d of %d)\n", path, g_sppSilence.strikes(i) + 1,
               g_sppSilence.limit());
    if (g_sppSilence.onLinkEnd(i, spoke, monoSecs()))
        printf("[Wireless] leaving %s alone for %u s: it is most likely MyBMW / BMW Connected (the link AAIdrive uses), "
               "and Android Auto setup messages would break it\n", path, g_sppSilence.holdSeconds());
}

static int openSppNode(const char* pinned, char* path, size_t pathLen) {
    if (pinned) {
        snprintf(path, pathLen, "%s", pinned);
        return open(path, O_RDWR);
    }
    configureSppSilence();
    const uint32_t now = monoSecs();
    int fd = -1;
    for (int i = 0; i < kSppNodes; ++i) {
        char p[32];
        snprintf(p, sizeof(p), "/dev/btspp%d", i);
        if (access(p, F_OK) != 0) {
            if (g_sppForeign[i] || g_sppSilence.onHold(i, now) || g_sppCarOwned[i])
                printf("[Wireless] %s is gone; it will be tried again when it comes back\n", p);
            g_sppForeign[i] = false;
            g_sppSilence.nodeGone(i);
            g_sppSeen[i] = g_sppPhone[i] = g_sppCarOwned[i] = false;
            continue;
        }
        if (!g_sppSeen[i]) {   // new link: who opened it?
            g_sppSeen[i] = true;
            std::string svc;
            g_sppPhone[i] = nbt::sppDirTakePhoneOpened(&svc);
            if (nbt::sppDirMode() != 0) {
                if (g_sppPhone[i])
                    printf("[Wireless] %s appeared: opened by the phone (ServiceRequest '%s'), so it can be Android "
                           "Auto\n", p, svc.c_str());
                else
                    printf("[Wireless] %s appeared: opened by the car (no ServiceRequest from the phone just before; %u "
                           "seen so far), so it is the car's BMW Apps link (MyBMW / BMW Connected / AAIdrive)%s\n", p,
                           nbt::sppDirRequests(), nbt::sppDirGate() ? ": leaving it to the car" : "");
                g_sppCarOwned[i] = !g_sppPhone[i] && nbt::sppDirGate();
            }
        }
        if (g_sppSilence.releaseIfExpired(i, now))
            printf("[Wireless] hold on %s is over: trying it again\n", p);
        if (g_sppForeign[i] || g_sppSilence.onHold(i, now) || g_sppCarOwned[i] || fd >= 0) continue;
        fd = open(p, O_RDWR);
        if (fd >= 0) snprintf(path, pathLen, "%s", p);
    }
    if (fd < 0) { snprintf(path, pathLen, "/dev/btspp0"); errno = ENOENT; }
    return fd;
}

static void markSppForeign(const char* path) {
    int i = -1;
    if (sscanf(path, "/dev/btspp%d", &i) == 1 && i >= 0 && i < kSppNodes) g_sppForeign[i] = true;
    printf("[Wireless] releasing %s (another app's Bluetooth link) and looking for the Android Auto one\n", path);
}

static bool cpLiveRetune() {
    const char* e = getenv("NBT_CP_LIVE_RETUNE");
    return nbt::VideoManager::cpHandoff() && !(e && e[0] == '0');
}

static nbt::TcpServer* g_tcp = NULL;           // set once the listener runs (wireless recovery re-creates it)
static nbt::BluetoothManager* g_btMgr = NULL;   // set once the AA Bluetooth service registration is under way

static uint32_t monoSecs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)ts.tv_sec;
}

// 1 = the interface has an IPv4 address, 0 = it has none or does not exist (the car's Wi-Fi access point is off).
static int ifaceHasIpv4(const char* ifn) {
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) return -1;
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifn, sizeof(ifr.ifr_name) - 1);
    int up = 0;
    if (ioctl(s, SIOCGIFADDR, &ifr) == 0) {
        const sockaddr_in* sin = reinterpret_cast<const sockaddr_in*>(&ifr.ifr_addr);
        up = (sin->sin_family == AF_INET && sin->sin_addr.s_addr != 0) ? 1 : 0;
    }
    close(s);
    return up;
}

// Ignition off/on while the head unit stays up (it only shuts down ~10 min after ignition off) switches the car's
// Wi-Fi access point off and on again under a daemon that keeps running. Car reports 2026-10-29 (F31, Supra): after
// that the phone takes the Wi-Fi details over Bluetooth but never connects, until Force Stop + Start, i.e. until
// a new process runs 'pfctl -d' and opens a new listening socket. Redo both here.
static void recoverWifiPath(const char* why) {
    if (nbt::envFlag("NBT_KEEP_PF") != 1) {
        int rc = system("pfctl -d");
        printf("[Wireless] %s: 'pfctl -d' returned %d (%s)\n", why, rc,
               rc == 0 ? "the packet filter was ON again and is now off" : "packet filter already off");
    }
    if (g_tcp) g_tcp->requestRebind(why);
    fflush(stdout);
}

static void* wirelessThread(void*) {
    const char* pinned = getenv("NBT_BTSPP_DEV");
    if (pinned && !*pinned) pinned = NULL;
    char devBuf[64];
    const char* dev = devBuf;
    // AP config: NBT_AP_CFG pins one file. Otherwise iap2.cfg (which Connectivity keeps in sync with the live AP)
    // is used, iap2_Wifi.cfg only as a fallback: when the phone takes the AP info but never joins the Wi-Fi
    // (wrong passphrase) twice, the next distinct passphrase is offered.
    const char* cfg = getenv("NBT_AP_CFG");
    if (cfg && !*cfg) cfg = NULL;
    int apIdx = 0, apNoJoin = 0;
    std::string apShown;
    const char* ifn = getenv("NBT_AP_IF");
    if (!ifn || !*ifn) ifn = "uap0";

    int apState = -1;           // ifn: -1 unknown, 0 no IP (access point off), 1 has an IP
    int apPoll = 0;
    // No Android Auto Bluetooth link for a while although the phone may be connected over Bluetooth (MINI F60:
    // cold start, and after the head unit slept): register the Bluetooth service again, as Restart does.
    nbt::SppReregTimer rereg;
    rereg.configure(nbt::sppReregBaseSec());
    if (rereg.enabled())
        printf("[Wireless] while no phone opens the Android Auto Bluetooth link, the Bluetooth service is registered "
               "again (like Restart does) every %d s for %d s, then every %d s (NBT_SPP_REREG_S=0 turns this off)\n",
               nbt::sppReregBaseSec(), nbt::kSppReregFastPhaseSec,
               nbt::sppReregIntervalSec(1 << 20, nbt::sppReregBaseSec()));
    else
        printf("[Wireless] automatic Bluetooth service re-registration off (NBT_SPP_REREG_S=0)\n");
    bool idleWait = false;      // waiting for a link; the re-registration timer runs from when this started
    uint32_t idleStart = 0;
    while (g_running) {
        // Nothing to do while a projection session is running; re-arm after it ends.
        if (g_sessionActive) { idleWait = false; sleep(1); continue; }

        // Watch the access point while idle (every ~2 s): when it comes back after being off, the car was switched
        // off and on again under us -> refresh the firewall and the listener before the phone tries again.
        if ((apPoll++ % 4) == 0) {
            int st = ifaceHasIpv4(ifn);
            if (st >= 0 && st != apState) {
                if (st == 0) printf("[Wireless] %s has no IP address: the car's Wi-Fi access point is off\n", ifn);
                else if (apState == 0) {
                    recoverWifiPath("the car's Wi-Fi access point came back");
                    idleWait = false;   // the car woke up: start the re-registration schedule from its first step
                }
                apState = st;
            }
        }

        // The SPP node only exists while the phone's Bluetooth link is up: poll for it first and
        // only then read the AP config (keeps the log quiet while idle).
        int fd = openSppNode(pinned, devBuf, sizeof(devBuf));
        if (fd < 0) {
            static int quiet = 0;
            if ((quiet++ % 30) == 0)
                printf("[Wireless] [%9.3f] waiting for %s (%s)\n", upSec(), dev, strerror(errno));
            uint32_t now = monoSecs();
            if (!idleWait) { idleWait = true; idleStart = now; rereg.reset(now); }
            else if (g_btMgr && rereg.due(now)) {
                char why[96];
                snprintf(why, sizeof(why), "no Android Auto Bluetooth link for %u s (try %d)",
                         (unsigned)(now - idleStart), rereg.attempts());
                g_btMgr->reregister(why);
                if (rereg.nextIntervalSec() != nbt::sppReregIntervalSec(rereg.attempts() - 1, nbt::sppReregBaseSec()))
                    printf("[Wireless] still no Android Auto Bluetooth link: re-registering every %d s from now on\n",
                           rereg.nextIntervalSec());
            }
            usleep(500000);
            continue;
        }
        idleWait = false;
        nbt::ApInfo ap;
        std::string apPath;
        size_t apCount = 1;
        if (cfg) {
            if (!nbt::loadCarPlayApConfig(cfg, ap)) {
                printf("[Wireless] AP config unreadable, retry in 10 s\n");
                close(fd);
                sleep(10);
                continue;
            }
            apPath = cfg;
        } else {
            std::vector<nbt::ApCandidate> cands = nbt::collectApCandidates(nbt::kApCfgPaths, nbt::kApCfgPathCount);
            if (cands.empty()) {
                printf("[Wireless] no readable CarPlay AP config (iap2_Wifi.cfg / iap2.cfg), retry in 10 s\n");
                close(fd);
                sleep(10);
                continue;
            }
            apCount = cands.size();
            const nbt::ApCandidate& c = cands[(size_t)apIdx % apCount];
            ap = c.ap;
            apPath = c.path;
            char tag[400];
            snprintf(tag, sizeof(tag), "%s|%s|%u", c.path.c_str(), c.ap.ssid.c_str(), (unsigned)(apIdx % apCount));
            if (apShown != tag) {
                apShown = tag;
                printf("[Wireless] CarPlay AP: ssid='%s', passphrase from %s (%u of %u distinct)\n", ap.ssid.c_str(),
                       apPath.c_str(), (unsigned)(apIdx % apCount) + 1, (unsigned)apCount);
            }
        }
        nbt::fillApNetworkInfo(ifn, ap);
        if (ap.ip.empty()) { printf("[Wireless] no IP on %s, retry\n", ifn); close(fd); sleep(5); continue; }
        // Make the serial device a completely transparent 8-bit stream (no line editing, no signal
        // characters, no flow control, no CR/NL or parity/break mangling). Log what the driver says.
        struct termios tio;
        if (tcgetattr(fd, &tio) == 0) {
            printf("[Wireless] termios before: iflag=0x%x oflag=0x%x cflag=0x%x lflag=0x%x\n",
                   (unsigned)tio.c_iflag, (unsigned)tio.c_oflag, (unsigned)tio.c_cflag, (unsigned)tio.c_lflag);
            tio.c_iflag = 0;
            tio.c_oflag = 0;
            tio.c_lflag = 0;
            tio.c_cflag &= ~(CSIZE | PARENB);
            tio.c_cflag |= CS8 | CREAD | CLOCAL;
            for (int i = 0; i < NCCS; ++i) tio.c_cc[i] = 0;
            tio.c_cc[VMIN] = 1;
            tio.c_cc[VTIME] = 0;
            int rc = tcsetattr(fd, TCSANOW, &tio);
            struct termios chk;
            if (tcgetattr(fd, &chk) == 0)
                printf("[Wireless] termios set rc=%d now: iflag=0x%x oflag=0x%x cflag=0x%x lflag=0x%x\n", rc,
                       (unsigned)chk.c_iflag, (unsigned)chk.c_oflag, (unsigned)chk.c_cflag, (unsigned)chk.c_lflag);
        } else {
            printf("[Wireless] tcgetattr failed: %s (device left in its default mode!)\n", strerror(errno));
        }
        printf("[Wireless] [%9.3f] opened %s (fd %d), starting handshake\n", upSec(), dev, fd);
        g_btLink = 1;
        // Handshake style: the proactive one (AP info sent without waiting to be asked) is what worked
        // on the car (w5) and does not depend on reading the phone's WifiInfoRequest; on consecutive
        // failures rotate through the others. NBT_WPP_MODE=0/1/2 pins one.
        static int s_fails = 0;
        static const int kOrder[3] = { 2, 0, 1 };
        int mode = kOrder[s_fails % 3];
        const char* me = getenv("NBT_WPP_MODE");
        if (me && *me) mode = atoi(me);
        logStations("before handshake");
        bool ok = nbt::runWirelessHandshake(fd, ap, g_aaPort, 10000, mode);
        if (!ok && nbt::lastLinkForeign()) {
            g_btLink = 0;
            close(fd);
            if (!pinned) markSppForeign(dev);
            else { printf("[Wireless] NBT_BTSPP_DEV=%s is not an Android Auto link\n", pinned); sleep(5); }
            continue;
        }
        printf("[Wireless] handshake %s (mode %d)\n", ok ? "done: waiting for the phone to join the Wi-Fi and connect" : "FAILED", mode);
        if (!ok && !pinned) noteSppLinkEnd(dev, nbt::lastLinkSpokeWpp());
        if (!ok && !pinned) noteSppDirection(dev, nbt::lastLinkSpokeWpp());
        if (ok) {
            s_fails = 0;
            // Keep the Bluetooth link open and serviced (ping replies, repeated AP info requests) for
            // as long as the phone wants it: closing it early makes the phone abort the wireless setup.
            bool everActive = false;
            int tick = 0;
            while (g_running) {
                bool alive = nbt::wppService(fd, 1000, ap, g_aaPort);
                ++tick;
                if (g_sessionActive) everActive = true;
                if (!alive && nbt::lastLinkForeign()) { if (!pinned) markSppForeign(dev); break; }
                if (!alive) {
                    printf("[Wireless] [%9.3f] Bluetooth link closed by the phone\n", upSec());
                    if (!g_sessionActive && !everActive && !nbt::lastLinkSpokeWpp())
                        printf("[Wireless] the other end never sent an Android Auto reply (no WifiStartResponse): this "
                               "Bluetooth link was probably another app's (e.g. BMW Connected / MyBMW), so Android Auto "
                               "never got the Wi-Fi details\n");
                    if (!g_sessionActive && !everActive && !pinned) noteSppLinkEnd(dev, nbt::lastLinkSpokeWpp());
                    break;
                }
                if (!g_sessionActive && (tick % 10) == 5) logStations("waiting for phone TCP connect");
                if (!everActive && tick > 90) { printf("[Wireless] phone did not connect within 90 s\n"); break; }
                if (everActive && !g_sessionActive) { printf("[Wireless] session ended, releasing the Bluetooth link\n"); break; }
            }
            g_btLink = 0;
            close(fd);
            if (!pinned) noteSppDirection(dev, nbt::lastLinkSpokeWpp() || everActive);
            // A phone that joined the Wi-Fi usually drops Bluetooth before its TCP connect arrives: give it a
            // few seconds before counting this as a "never joined" try.
            for (int w = 0; !everActive && g_running && w < 40; ++w) {
                if (g_sessionActive) everActive = true;
                else usleep(200000);
            }
            if (!everActive) recoverWifiPath("phone took the Wi-Fi details but did not connect");
            if (everActive) {
                apNoJoin = 0;
            } else if (++apNoJoin >= 2) {
                apNoJoin = 0;
                if (!cfg && apCount > 1) {
                    printf("[Wireless] no TCP connection from the phone after 2 tries with the AP details from %s: "
                           "trying the next passphrase candidate\n", apPath.c_str());
                    ++apIdx;
                } else {
                    printf("[Wireless] no TCP connection from the phone after 2 tries with the AP details from %s "
                           "(not proof of a wrong passphrase: a dead listener or the packet filter look the same)\n",
                           apPath.c_str());
                }
            }
        } else {
            g_btLink = 0;
            close(fd);
            ++s_fails;
            // The phone closed a link that was already open without accepting the Wi-Fi start.
            // Drop and re-add the Android Auto Bluetooth service so the next try is a fresh registration.
            if (g_btMgr) g_btMgr->requestSppBounce();
            sleep(2);
        }
    }
    return NULL;
}

static void signalHandler(int sig) {
    (void)sig;       // printf is not async-signal-safe; main() reports the shutdown
    g_running = 0;   // session is stopped from main() (joining a thread in a signal handler can hang)
}

// ---- Car microphone -> phone (Assistant / voice search), AA channel 14 ------------------------------------
// The phone opens the mic with AVInputOpenRequest; the main loop then starts the factory echo-cancelled uplink
// (A2I /output_mono_AirPlayUL, 16 kHz mono, the CarPlay Siri path) and the session streams it back. In
// Bluetooth-audio mode the A2I playback pipeline stays closed, so the capture runs standalone. NBT_MIC=0 or
// /opt/aa/mic.disable keeps the mic closed (the open request is still answered).
static void onMicRequest(bool open, void* user) {
    (void)user;
    pushSlot(g_micQ, &g_micN, open ? 1 : 2);
}

// Level meter (once per ~1 s of audio) and optional raw dump, to tell real speech from a silent / unrouted uplink:
// NBT_MIC_DUMP=1 writes the first ~30 s of each run to /dev/shmem/nbt-aad-mic.raw (S16LE 16 kHz mono).
static void onMicData(const uint8_t* pcm_data, size_t size, void* user_data) {
    (void)user_data;
    g_session.postMicData(pcm_data, size);   // echo-cancelled chunk -> AA mic channel (dropped unless open)
    if (!g_session.micOpen()) return;         // capture keeps running between Assistant requests: meter/dump only while open

    static uint32_t acc_n = 0, acc_peak = 0, zero_chunks = 0, chunks = 0;
    static double acc_sq = 0;
    static FILE* dump = NULL;
    static int dump_state = -1;              // -1 unknown, 0 off, 1 on, 2 full
    static uint32_t dumped = 0;
    const int16_t* s = reinterpret_cast<const int16_t*>(pcm_data);
    size_t n = size / 2;
    bool all_zero = true;
    for (size_t i = 0; i < n; ++i) {
        int v = s[i];
        if (v) all_zero = false;
        uint32_t a = (uint32_t)(v < 0 ? -v : v);
        if (a > acc_peak) acc_peak = a;
        acc_sq += (double)v * v;
    }
    acc_n += (uint32_t)n;
    ++chunks;
    if (all_zero) ++zero_chunks;
    if (acc_n >= 16000) {
        double rms = 0;
        { double m = acc_sq / acc_n; double r = m > 0 ? m : 0; /* sqrt without libm dependency issues */
          double x = r > 1 ? r : 1; for (int k = 0; k < 20; ++k) x = 0.5 * (x + r / x); rms = r > 0 ? x : 0; }
        printf("[Mic] level: peak %u rms %d (of 32767), %u/%u chunks all-zero, chunk %u bytes\n",
               acc_peak, (int)rms, zero_chunks, chunks, (unsigned)size);
        acc_n = acc_peak = zero_chunks = chunks = 0;
        acc_sq = 0;
    }
    if (dump_state < 0) dump_state = getenv("NBT_MIC_DUMP") ? 1 : 0;
    if (dump_state == 1) {
        if (!dump) dump = fopen("/dev/shmem/nbt-aad-mic.raw", "wb");
        if (dump) {
            fwrite(pcm_data, 1, size, dump);
            dumped += (uint32_t)size;
            if (dumped >= 30u * 32000u) { fclose(dump); dump = NULL; dump_state = 2; printf("[Mic] dump complete (/dev/shmem/nbt-aad-mic.raw)\n"); }
            else fflush(dump);
        }
    }
}

// Default ON since v1.7.7 (car-verified nbt-aad-22-2-2: with it the Assistant's reply is heard).
// NBT_BT_REFOCUS=0 / off / no / false disables.
static bool btRefocus() {
    return nbt::envFlag("NBT_BT_REFOCUS") != 0;   // unset stays on
}

static bool micEnabled() {
    if (nbt::envFlag("NBT_MIC") == 0) return false;
    return access("/opt/aa/mic.disable", F_OK) != 0;
}

static bool micStart(nbt::A2ICaptureStream& mic) {
    if (mic.isRunning()) return true;
    if (!mic.isOpen()) {
        nbt::A2IStreamConfig cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.endpoint          = A2I_ENDPOINT_MIC_UL;
        cfg.fallback_endpoint = NULL;
        cfg.sample_rate       = 16000;
        cfg.channels          = A2I_CHANNELS_MONO;
        cfg.buffer_size       = A2I_BUFFER_SIZE_MONO;
        cfg.cookie_primary    = A2I_COOKIE_LEGACY_CAPTURE;
        cfg.cookie_alternate  = A2I_COOKIE_FACTORY;
        cfg.timeout_ms        = 2000;
        if (!mic.open(cfg)) {
            printf("[Mic] could not open %s\n", A2I_ENDPOINT_MIC_UL);
            return false;
        }
    }
    return mic.start(&onMicData, NULL);
}

static void onVideoFrame(uint32_t frame_index, const uint8_t* nal_data, size_t nal_len, uint64_t pts, void* user_data) {
    nbt::VideoManager* video = reinterpret_cast<nbt::VideoManager*>(user_data);
    if (video) {
        // Feed raw H.264 slice straight into the car's hardware video decoder (/dev/name/local/mdev)
        video->postVideoFrame(nal_data, nal_len, pts);
        video->onVideoFrameReceived(frame_index, nal_len);
    }
}

static void onCodecConfig(const uint8_t* config_data, size_t config_len, void* user_data) {
    nbt::VideoManager* video = reinterpret_cast<nbt::VideoManager*>(user_data);
    if (video) {
        video->postCodecConfig(config_data, config_len);
    }
}

static void onAudioFrame(uint8_t channel, const uint8_t* pcm, size_t pcm_len, uint64_t pts, void* user_data) {
    (void)pts;
    (void)user_data;
    if (g_audio) g_audio->onAaAudio(channel, pcm, pcm_len);
    else if (channel == 8) speechFrame(pcm, pcm_len);
}

static void onAudioState(uint8_t channel, bool started, void* user_data) {
    (void)user_data;
    if (g_audio) g_audio->onAaStreamState(channel, started);
    else if (channel == 8) speechState(started);
}

static double g_sessionStartS = 0;   // upSec() of the last onClientConnected
static int g_shortSessions = 0;      // sessions in a row that ended within kShortSessionS of connecting
static const double kShortSessionS = 15.0;
static void onSessionEnded(void* user_data) {
    g_sessionActive = false;
    nbt::TcpServer* server = reinterpret_cast<nbt::TcpServer*>(user_data);
    double now = upSec(), dur = g_sessionStartS > 0 ? now - g_sessionStartS : -1;
    printf("[Main] [%9.3f] Android Auto session ended after %.1f s. Resuming auto-connector probing...\n", now, dur);
    if (dur >= 0 && dur < kShortSessionS) ++g_shortSessions; else g_shortSessions = 0;
    if (g_shortSessions == 3 || (g_shortSessions > 3 && g_shortSessions % 10 == 0))
        printf("[Main] the phone ended %d sessions in a row within %.0f s of connecting (see the ByeByeRequest / "
               "'No data' lines above): Android Auto on the phone keeps giving up; its logcat (CAR.SERVICE) says why\n",
               g_shortSessions, kShortSessionS);
    if (server) {
        server->notifySessionEnded();
    }
    // Runs on the session thread (AASession::start() waits for it): drop stale frames and the
    // cached SPS/PPS. The overlay itself is handled by the main loop.
    if (g_video) g_video->endSession();
    if (g_audio) g_audio->onSessionEnded();
    // Drop the Bluetooth-source request (and its retries) this session queued, so a phone that keeps reconnecting
    // cannot stack audioconn processes until the head unit runs out of memory and resets (tester log 2026-10-29).
    g_btRoute.cancelPending();
    if (g_speechRouted) {   // session died mid-prompt: give the speech source back
        g_btRoute.removeFocus(g_speechSrc, g_speechSink);
        g_speechRouted = false;
    }
    if (g_speechReady && g_speech) g_speech->flush();
    if (g_btAudio && getenv("NBT_BT_RELEASE") && strcmp(getenv("NBT_BT_RELEASE"), "1") == 0) {
        char d[64];
        btDescribe(d, sizeof(d));
        printf("[BtAudio] session ended: releasing %s\n", d);
        btReleaseSource();
    }
}

// UsbAccessory: no new USB session (and no accessory switch) while a phone is already connected.
static bool sessionBusy(void*) {
    return g_sessionActive;
}

static void onClientConnected(int client_fd, void* user_data) {
    nbt::TcpServer* server = reinterpret_cast<nbt::TcpServer*>(user_data);
    g_sessionStartS = upSec();
    printf("[Main] [%9.3f] Android Auto client connected on fd %d. Initializing aasdk session...\n", g_sessionStartS,
           client_fd);
    g_sessionActive = true;
    if (g_video && nbt::VideoManager::cpHandoff()) g_video->prepareForSession();   // decoder ready before frames
    if (!g_session.start(client_fd)) {
        // start() closed the fd; make sure the server does not wait for a session that never began
        g_sessionActive = false;
        if (server) server->notifySessionEnded();
        return;
    }
    // start() joined any previous session thread, so that thread's onSessionEnded() has finished.
    // Re-assert "active" now: a stale end-of-session callback may have cleared the flags between
    // the assignments above and the join (phone reconnecting while the old session was dying).
    g_sessionActive = true;
    if (server) server->markSessionActive();
    if (g_btAudio) {
        char d[64];
        btDescribe(d, sizeof(d));
        printf("[BtAudio] session started: requesting car Bluetooth audio %s\n", d);
        if (!btRequestSource()) printf("[BtAudio] could not queue audioconn\n");
    }
}

// One-shot diagnostics for "frames reach the decoder but nothing is visible": dump the LayerManager
// state and the QNX Screen window properties (order / above / below / visible) of our window and of
// the HMI's windows. Runs in its own thread so a slow cat can never stall the main loop.
static void* diagThread(void*) {
    char cmd[320];
    int me = (int)getpid();
    printf("[Diag] --- LayerManager ---\n");
    fflush(stdout);
    system("/opt/sys/bin/LayerManagerControl get surface 1280");
    system("/opt/sys/bin/LayerManagerControl get surface 256");
    system("/opt/sys/bin/LayerManagerControl get layer 0x80000000");
    system("/opt/sys/bin/LayerManagerControl get screen 0");
    printf("[Diag] --- Screen windows of this daemon (pid %d) ---\n", me);
    fflush(stdout);
    snprintf(cmd, sizeof(cmd), "ls -l /dev/screen/%d", me);
    system(cmd);
    snprintf(cmd, sizeof(cmd), "cat /dev/screen/%d/*/*", me);
    system(cmd);
    int hmi = 0;
    FILE* f = popen("pidin arg", "r");
    if (f) {
        char line[512];
        while (fgets(line, sizeof(line), f)) {
            if (strstr(line, "HmiMain3D")) { hmi = atoi(line); if (hmi > 0) break; }
        }
        pclose(f);
    }
    if (hmi > 0) {
        printf("[Diag] --- Screen windows of HmiMain3D (pid %d) ---\n", hmi);
        fflush(stdout);
        snprintf(cmd, sizeof(cmd), "ls -l /dev/screen/%d", hmi);
        system(cmd);
        snprintf(cmd, sizeof(cmd), "cat /dev/screen/%d/*/*", hmi);
        system(cmd);
    } else {
        printf("[Diag] HmiMain3D pid not found\n");
    }
    printf("[Diag] --- end ---\n");
    fflush(stdout);
    return NULL;
}

static void onTouchEvent(const nbt::TouchEventOut& ev, void* user) {
    if (!g_inputToPhone) return;   // iDrive owns the screen (driver exited AA or reverse camera)
    static_cast<nbt::AASession*>(user)->postTouchEvent(ev);
}

// Session thread: the phone answered its own VideoFocusRequest (Toyota/"exit" icon = native, or back to projection).
static void onVideoFocusRequest(bool projected, uint32_t reason, void* user) {
    (void)user;
    printf("[Focus] phone requests %s (reason %u)\n", projected ? "PROJECTION" : "NATIVE UI (exit to iDrive)", reason);
    pushSlot(g_focusQ, &g_focusN, projected ? 1 : 2);
}

// iDrive knob / tilt / hard keys (default on, NBT_KNOB=0 disables) -> phone (MOST DSI connection thread).
// ---- BACK held -> minimize Android Auto ---------------------------------------------------------------------
// Holding the iDrive BACK key for NBT_BACK_HOLD_MS (default 2500, 0 = off: BACK goes straight to the phone) gives
// iDrive the screen, like the phone's exit-to-car icon; the iDrive Android Auto entry brings it back. To tell a hold
// from a press, BACK is not forwarded on key-down: a release before the hold time sends down+up to the phone (Android
// acts on BACK at key-up anyway); a completed hold is swallowed.
static volatile uint32_t g_backDownMs = 0;
static volatile sig_atomic_t g_backState = 0;   // 0 idle, 1 held (not yet sent), 2 hold fired
static uint32_t monoMs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)ts.tv_sec * 1000u + (uint32_t)(ts.tv_nsec / 1000000);
}
static uint32_t backHoldMs() {
    static int v = -1;
    if (v < 0) {
        const char* e = getenv("NBT_BACK_HOLD_MS");
        v = (e && *e) ? atoi(e) : 2500;
        if (v < 0) v = 0;
    }
    return (uint32_t)v;
}

// ---- MENU key while Android Auto is on screen ------------------------------------------------------------------
// iDrive always sees MENU too (the knob events are copies) and leaves our blank state for its main menu.
// Default: MENU belongs to Android Auto (its home / app launcher); the daemon reports the press in the state file
// (overlay 3 for 3 s) and the iDrive app puts iDrive straight back on the blank state instead of treating it as a hard
// key. "MENU key opens iDrive" (iDrive menu checkbox, flag file below): MENU is not sent to the phone and Android Auto
// minimizes at once, like MENU leaving native CarPlay.
static const char* const kMenuIdriveFlag   = "/var/opt/aa/menu-idrive";
static const char* const kMenuIdriveManual = "/opt/aa/menu-idrive";
static volatile uint32_t g_keyToPhoneMs = 0;   // monoMs() of the last MENU / COM / MEDIA / NAV / MAP press sent to the phone
static bool menuOpensIdrive() { return access(kMenuIdriveFlag, F_OK) == 0 || access(kMenuIdriveManual, F_OK) == 0; }

static void onKnobEvent(const nbt::CcsEvent& ev, void* user) {
    nbt::AASession* s = static_cast<nbt::AASession*>(user);
    const bool back_hold = (ev.kind == 1 && ev.keycode == nbt::AAK_BACK && backHoldMs() > 0);
    if (back_hold && !ev.down) {
        int st = g_backState;
        g_backState = 0;
        if (st == 0) return;   // the press went to iDrive (it owned the screen): no hold timer ran, nothing to report
        printf("[Knob] BACK released after %u ms (%s)\n", (unsigned)(monoMs() - g_backDownMs),
               st == 2 ? "hold already fired" : g_inputToPhone ? "short press -> phone" : "short press, iDrive owns the screen");
        if (st == 1 && g_inputToPhone) {        // short press: deliver it now
            g_keyToPhoneMs = monoMs() | 1u;       // iDrive popped back a state too: the iDrive app re-holds
            s->postKeyEvent(ev.keycode, true);
            s->postKeyEvent(ev.keycode, false);
        }
        return;                                   // st == 2: the hold already minimized AA, swallow the release
    }
    if (ev.what && !strcmp(ev.what, "PTT")) {     // steering-wheel voice button (NBT_PTT_ASSISTANT=1), any screen
        printf("[PTT] Assistant key %s -> phone\n", ev.down ? "down" : "up");
        s->postKeyEvent(ev.keycode, ev.down != 0);
        return;
    }
    if (!g_inputToPhone) return;   // knob drives iDrive while it owns the screen
    if (ev.kind == 1 && ev.keycode == nbt::AAK_HOME) {
        if (menuOpensIdrive()) {
            if (ev.down) {
                printf("[Knob] MENU: iDrive menu (menu-idrive on): minimizing Android Auto\n");
                g_hideReq = 1;
            }
            return;                               // never reaches the phone in this mode
        }
        if (ev.down) {
            g_keyToPhoneMs = monoMs() | 1u;        // | 1: never 0
            printf("[Knob] MENU: Android Auto's menu (iDrive app puts iDrive back on the blank state)\n");
        }
    }
    // COM / MEDIA / NAV / MAP go to the phone too (Android Auto's dialer / media / maps), and iDrive also acts on its copy
    // and leaves our blank state. Report them like MENU (overlay 3), so the iDrive app puts iDrive back instead of taking
    // it for a hard key that leaves Android Auto (car 2026-10-29: presses within 3 s of a MENU press stayed, the others minimized).
    // NBT_KEY_COM / _MEDIA / _NAV / _MAP=0 keeps a key for iDrive only (then it leaves Android Auto as before).
    if (ev.kind == 1 && ev.down && ev.what &&
        (!strcmp(ev.what, "COM") || !strcmp(ev.what, "MEDIA") || !strcmp(ev.what, "NAV") || !strcmp(ev.what, "MAP"))) {
        g_keyToPhoneMs = monoMs() | 1u;
        printf("[Knob] %s: Android Auto (iDrive app puts iDrive back on the blank state)\n", ev.what);
    }
    if (back_hold && ev.down) {
        if (g_backState == 0) {                   // ignore auto-repeat downs: they must not restart the timer
            g_backDownMs = monoMs();
            g_backState = 1;
            printf("[Knob] BACK down: hold timer started (%u ms)\n", backHoldMs());
        }
        return;
    }
    if (ev.kind == 2) s->postRelativeEvent(ev.keycode, ev.delta);
    else {
        // Every key that reaches the phone: iDrive got the same key and may have left the blank state (BACK pops its
        // history, which after a MENU / hard-key re-hold is the iDrive main menu, not our menu: car 2026-10-29 short
        // BACK minimized Android Auto). Overlay 3 makes the iDrive app put it back instead of minimizing.
        if (ev.down) g_keyToPhoneMs = monoMs() | 1u;
        s->postKeyEvent(ev.keycode, ev.down != 0);
    }
}

#define NBT_AAD_BUILD "v1.8.52-neutral5"

// ---- log destination ------------------------------------------------------------------------------------------
// Default: no log file and nothing kept in RAM (the iDrive app sends our stdout to /dev/null; from a shell it stays on
// the terminal). NBT_LOG_FILE=<path> forces a file. The iDrive app's "Save logs to USB" checkbox (flag
// /var/opt/aa/usblog, usb_log.hpp) makes us append to <stick>/nbt-aad.log on the first writable /mnt/umass* stick;
// checked at start and every 5 s, so a stick plugged in later is picked up and a removed one is let go.
static bool g_logForced = false;
static char g_usbLogPath[192] = "";

static bool logPersistent() { return g_logForced || g_usbLogPath[0] != 0; }

static void stdoutToNull() {
    fflush(stdout);
    if (!freopen("/dev/null", "a", stdout)) return;
    setvbuf(stdout, NULL, _IOLBF, 0);
    dup2(fileno(stdout), 2);
}

static void serviceUsbLog(bool atStart) {
    if (g_logForced) return;
    const bool on = nbt::usblog::enabled();
    if (g_usbLogPath[0]) {
        char dir[192];
        snprintf(dir, sizeof(dir), "%s", g_usbLogPath);
        char* slash = strrchr(dir, '/');
        if (slash) *slash = 0;
        if (on && nbt::usblog::isWritableDir(dir)) return;
        printf("[Log] %s: USB log closed\n", on ? "USB stick gone" : "Save logs to USB turned off");
        g_usbLogPath[0] = 0;
        stdoutToNull();
        return;
    }
    char stick[128];
    if (!on || !nbt::usblog::findStick(stick, sizeof(stick))) return;
    char path[192];
    nbt::usblog::logPath(path, sizeof(path), stick, nbt::usblog::kDaemonName);
    if (isatty(1)) printf("[Log] Save logs to USB is on: logging to %s\n", path);
    fflush(stdout);
    if (!freopen(path, "a", stdout)) { stdoutToNull(); return; }
    setvbuf(stdout, NULL, _IOLBF, 0);
    dup2(fileno(stdout), 2);
    snprintf(g_usbLogPath, sizeof(g_usbLogPath), "%s", path);
    printf("---- nbt-aad log appended to %s (Save logs to USB, fsync every 2 s) ----\n", path);
    if (!atStart)
        printf("  Build: " NBT_AAD_BUILD " (%s %s): USB stick found while running, earlier lines were not saved\n",
               __DATE__, __TIME__);
}

int main(int argc, char* argv[]) {
    if (argc > 1 && (!strcmp(argv[1], "--license") || !strcmp(argv[1], "--licence"))) {
        nbt::printNbtAadLicense();
        return 0;
    }
    // NBT_LOG_FILE=/mnt/umass01200t7/nbt-aad-live.log: log straight to that file (fsync'd every 2 s by the main
    // loop). Otherwise the "Save logs to USB" checkbox decides (serviceUsbLog).
    const char* log_file = getenv("NBT_LOG_FILE");
    if (log_file && *log_file) g_logForced = (freopen(log_file, "a", stdout) != NULL);
    else serviceUsbLog(true);
    nbt::installProcessGuards();   // first: line-buffered log, SIGPIPE ignored, fatal-signal report (setvbuf must precede any output)
    if (g_logForced) printf("---- nbt-aad log appended to %s (fsync every 2 s) ----\n", log_file);
    printf("====================================================\n");
    printf("  NBT-AAD: Native Wireless Android Auto Daemon      \n");
    printf("  Platform: BMW NBT Evo ID6 / Toyota GR Supra (J29) \n");
    printf("  OS: QNX Neutrino RTOS (ARMv7LE)                   \n");
    printf("  Build: " NBT_AAD_BUILD " (%s %s)\n", __DATE__, __TIME__);
    nbt::printNbtAadBannerNotice();
    printf("====================================================\n");
    printf("----------------------------------------------------\n");

    if (!ctlClaimOrForward()) return 0;   // a daemon is already running (second start from the iDrive menu)

    // Child processes (audioconn, LayerManagerControl, pfctl, arp, ifconfig) inherit our environment, which comes from
    // the HMI. The bootloop log showed `ldd:FATAL: Could not preload library libEGL_X.gpp.so` from audioconn and
    // every LayerManagerControl run prints `unknown symbol: kdMain referenced from libKD.so.1`, i.e. each child loads
    // the graphics stack it does not use. NBT_CHILD_NO_PRELOAD=1 (experiment, not car-verified) removes LD_PRELOAD
    // for all children; the daemon itself is already loaded and keeps its libraries.
    {
        const char* pre = getenv("LD_PRELOAD");
        const char* np = getenv("NBT_CHILD_NO_PRELOAD");
        bool strip = np && np[0] == '1';
        printf("[Main] LD_PRELOAD=%s%s\n", pre ? pre : "(unset)",
               pre && strip ? " -> removed for child processes (NBT_CHILD_NO_PRELOAD=1)" : "");
        if (pre && strip) unsetenv("LD_PRELOAD");
    }

    // Drop the packet filter so the phone can reach the stock AA port (5277) on uap0.
    // Set NBT_KEEP_PF=1 to leave the firewall alone.
    if (nbt::envFlag("NBT_KEEP_PF") == 1) {
        printf("[Main] NBT_KEEP_PF=1: leaving packet filter untouched.\n");
    } else {
        int rc = system("pfctl -d");
        printf("[Main] 'pfctl -d' (disable packet filter) returned %d\n", rc);
    }

    signal(SIGINT,  signalHandler);
    signal(SIGTERM, signalHandler);

    // Vehicle + panel profile (NBT_VEHICLE / NBT_SCREEN, else the car's carplay.ini, else Toyota 1280x480).
    // Parsed here, before any thread starts.
    const nbt::CarConfig& car = nbt::carConfig();
    printf("[Car] vehicle %s (%s; Manufacturer=%s, logo %s), screen %dx%d (%s), carplay.ini %s%s%s%s\n",
           nbt::carMakeName(car.make), car.vehicleFrom,
           car.manufacturer.empty() ? "-" : car.manufacturer.c_str(), car.oemIcon.empty() ? "-" : car.oemIcon.c_str(), car.screenW, car.screenH, car.screenFrom,
           car.iniPath.empty() ? "not found" : car.iniPath.c_str(),
           car.coded.empty() ? "" : " (coded: ", car.coded.c_str(), car.coded.empty() ? "" : ")");
    printf("[Car] display (LayerManager screen 0): %dx%d, CarPlay picture coded %dx%d\n",
           car.dispW, car.dispH, car.iniW, car.iniH);
    if (nbt::vidNative())
        printf("[Car] phone canvas 1280x720 shown 1:1 (NBT_VID_NATIVE): top-left %dx%d visible, MMR dst %dx%d, touch scale 1.000\n",
               nbt::canvasColsShown(), nbt::canvasRowsShown(), nbt::mmrDstWidth(), nbt::mmrDstHeight());
    else
        printf("[Car] phone canvas 1280x720 shown at panel width: %d rows visible, MMR dst %dx%d, touch scale %.3f\n",
               nbt::canvasRowsShown(), nbt::mmrDstWidth(), nbt::mmrDstHeight(), nbt::touchScale());

    // 1. Initialize Video Pipeline (libscreen.so window at the panel size)
    nbt::VideoManager video;
    if (!video.init((uint32_t)car.screenW,
                    (uint32_t)car.screenH,
                    NBT_SCREEN_MAX_FPS)) {
        printf("[Main] Warning: Video initialization encountered an issue.\n");
    }
    g_video = &video;

    // 2. Initialize Audio Pipeline (A2I Media + Nav + Echo-Cancelled Mic)
    nbt::AudioManager audio;
    g_btAudio = nbt::btAudioEnabled();
    if (g_btAudio) {
        g_btSrc = (uint32_t)nbt::envHex("NBT_BT_SRC", 0x3a);
        g_btSink = (uint32_t)nbt::envHex("NBT_BT_SINK", 0x1);
        printf("[BtAudio] NBT_AUDIO_BT on: AA media sink left out of the ServiceDiscovery (NBT_SDR_DROP=%s),\n"
               "[BtAudio] phone audio stays on Bluetooth A2DP; A2I pipeline not opened. Source 0x%x -> sink 0x%x.\n",
               getenv("NBT_SDR_DROP") ? getenv("NBT_SDR_DROP") : "9", g_btSrc, g_btSink);
        if (!g_btRoute.configure()) printf("[BtAudio] audioconn not found: select Bluetooth audio in iDrive manually.\n");
        g_btConn = (uint32_t)nbt::envHex("NBT_BT_CONN", 0xc7);
        if (g_btConn)
            printf("[BtAudio] Bluetooth source via ConnectionMaster `audioconn -b 0x%x` (NBT_BT_CONN=0: focus form)\n",
                   g_btConn);
        else
            printf("[BtAudio] NBT_BT_CONN=0: Bluetooth source via AudioManagement focus `audioconn -b 0x%x 0x%x`\n",
                   g_btSrc, g_btSink);
        g_btPlay.init();
        g_audio = NULL;
        speechInit();
    } else {
        if (!audio.init()) {
            printf("[Main] Warning: Audio initialization encountered an issue.\n");
        }
        g_audio = &audio;
    }

    // 3. Input: the iDrive controller (ZBE) is read observe-only over MOSTSubSystem.DSYSMOSTServer by
    //    src/ccs_input.cpp (started below); iDrive keeps receiving every knob event itself.

    // Microphone upload is not wired into AA yet; leave callback unset so the
    // A2I uplink reader does not flood slog while debugging video bring-up.

    // Start all subsystems
    video.start();
    if (!g_btAudio) audio.start();

    // 4. Start Wireless TCP Server on the stock Android Auto port 5277 (override: NBT_AA_PORT)
    {
        const char* ep = getenv("NBT_AA_PORT");
        if (ep && *ep) g_aaPort = (uint16_t)atoi(ep);
    }
    nbt::TcpServer tcp_server(g_aaPort);
    tcp_server.setConnectionCallback(&onClientConnected, &tcp_server);
    g_session.setDisconnectCallback(&onSessionEnded, &tcp_server);
    g_session.setVideoFrameCallback(&onVideoFrame, &video);
    g_session.setCodecConfigCallback(&onCodecConfig, &video);
    g_session.setAudioFrameCallback(&onAudioFrame, NULL);
    g_session.setAudioStateCallback(&onAudioState, NULL);
    g_session.setVideoFocusCallback(&onVideoFocusRequest, NULL);
    g_session.setMicRequestCallback(&onMicRequest, NULL);

    if (!tcp_server.start()) {
        printf("[Main] Fatal: Failed to start TCP server on port %u\n", (unsigned)g_aaPort);
        return 1;
    }
    g_tcp = &tcp_server;

    // 4b. Android Auto over USB (NBT_USB=1, experimental). Same connect callback and user data as the TCP server, so
    // its "session active" state also covers a USB phone and the auto-connector stays quiet.
    nbt::UsbAccessory usb(&onClientConnected, &tcp_server, &sessionBusy, NULL);
    if (nbt::usbEnabled()) {
        if (!usb.start()) printf("[Main] Android Auto over USB failed to start.\n");
    }

    // 5. Initialize Bluetooth SDP Subsystem (Advertise Wireless AA UUID)
    nbt::BluetoothManager bluetooth;
    if (nbt::wirelessEnabled()) configureSppDirection();   // before the Bluetooth client attaches (it reads the mode)
    if (nbt::wirelessEnabled() && bluetooth.init()) {
        if (bluetooth.registerAndroidAutoSdp()) g_btMgr = &bluetooth;
    }

    if (nbt::envFlag("NBT_TOUCH_FWD") != 0 && !getenv("NBT_TOUCH_PROBE")) {   // default: forward touches to the phone
        const char* tp = getenv("NBT_TOUCH_PORT");
        uint16_t port = (tp && *tp) ? (uint16_t)atoi(tp) : 30567;
        if (!nbt::startTouchForward(port, &onTouchEvent, &g_session))
            printf("[Main] Touch forwarder failed to start.\n");
    } else if (getenv("NBT_TOUCH_PROBE")) {
        const char* tp = getenv("NBT_TOUCH_PORT");
        const char* ts = getenv("NBT_TOUCH_PROBE_SECS");
        nbt::startTouchProbe((uint16_t)(tp ? atoi(tp) : 30567), ts ? atoi(ts) : 60);
    }

    if (nbt::knobEnabled()) {
        if (!nbt::startCcsInput(&onKnobEvent, &g_session))
            printf("[Main] iDrive knob input failed to start.\n");
    } else {
        printf("[Main] iDrive knob input off (NBT_KNOB=0).\n");
    }

    if (nbt::wirelessEnabled()) {
        pthread_t wt;
        if (pthread_create(&wt, NULL, &wirelessThread, NULL) == 0) {
            pthread_detach(wt);
            printf("[Main] Wireless bring-up thread started (port %u)\n", (unsigned)g_aaPort);
        }
    } else {
        printf("[Main] Wireless off (NBT_WIRELESS=0): outbound phone scanner mode.\n");
    }

    printf("[Main] Daemon running. Press Ctrl+C to terminate.\n");

    // Daemon main loop. The overlay follows the session: up while a phone session is running, down
    // (iDrive visible) otherwise. This is the only place that calls setVisible() while the daemon runs,
    // so the 3-5 system() calls of two transitions cannot interleave, stop() cannot race them, and a
    // session that dies right after it started cannot leave the overlay on top of iDrive.
    // Default (known-good on the car): the overlay stays up for the whole daemon run.
    // NBT_HIDE_IDLE=1 gives iDrive back while no phone is connected (hide/show cycle is UNTESTED).
    // Give iDrive the screen while no phone session is up (default since v1.6.5: a dropped link must not leave the
    // last frame on screen). NBT_HIDE_IDLE=0 keeps the overlay up for the whole daemon run (old behaviour).
    const bool hide_idle = nbt::envFlag("NBT_HIDE_IDLE") != 0;        // default on; NBT_HIDE_IDLE=0 keeps the overlay up
    const bool gear_action = nbt::envFlag("NBT_GEAR_ACTION") != 0;    // default on; NBT_GEAR_ACTION=0 ignores the gear
    const int gear_reverse = (int)nbt::envHex("NBT_GEAR_REVERSE", 2);   // CDS driving.gear value for R (verify in the log)
    const bool reverse_focus = nbt::envFlag("NBT_REVERSE_FOCUS") == 1; // also tell the phone it lost the screen in R
    readGearFile();
    readPdcFile();
    readDayNightFile();
    const bool pdc_action = nbt::envFlag("NBT_PDC_ACTION") != 0;       // default on; NBT_PDC_ACTION=0 ignores park assist
    bool was_park = false;
    bool park_dismissed = false;   // iDrive Android Auto entry chosen while park assist is active: AA until it ends
    // Manual gearbox (feature request 2026-10-31): reverse -> neutral + handbrake leaves park assist (and with it
    // iDrive's parking view) up, since only P or driving off ends it. NBT_NEUTRAL_RESUME_S=<s> (default 5, 0 = off)
    // brings Android Auto back after that many seconds in neutral straight after reverse, as if the driver had picked
    // the iDrive Android Auto entry. NBT_GEAR_NEUTRAL: gear code for N (default 1). Not car-verified.
    const uint32_t neutral_resume_ms = (uint32_t)nbt::envHex("NBT_NEUTRAL_RESUME_S", 5) * 1000u;
    const int gear_neutral = (int)nbt::envHex("NBT_GEAR_NEUTRAL", 1);
    nbt::NeutralResume neutral_resume;
    bool neutral_wait_logged = false;
    if (neutral_resume_ms)
        printf("[Gear] manual gearbox: Android Auto comes back %u s after reverse -> neutral (gear %d) "
               "(NBT_NEUTRAL_RESUME_S, 0 = off)\n", neutral_resume_ms / 1000u, gear_neutral);
    printf("[Main] Screen ownership: reverse gear value %d (%s), exit-to-iDrive via the phone's car icon, "
           "back via the iDrive Android Auto entry.\n", gear_reverse, gear_action ? "on" : "off (NBT_GEAR_ACTION=0)");
    bool overlay_on = false;              // video.init() leaves the window hidden
    bool was_active = false;
    bool was_reverse = false;
    bool was_dm_peek = false;
    bool force_show = false;
    int reassert_ticks = -1;     // >= 0: ticks since the overlay came back up from the iDrive entry / park / reverse
    int active_ticks = 0;
    bool diag_done = false;
    const bool mic_on = micEnabled();
    nbt::A2ICaptureStream mic("mic");
    bool mic_live = false;
    unsigned log_sync_ticks = 0;
    printf("[Mic] car microphone for the Assistant: %s\n", mic_on ? "on" : "off (NBT_MIC=0 or /opt/aa/mic.disable)");
    if (g_audio) g_audio->setMicCallback(&onMicData, NULL);
    nbt::VoiceScenario voice;   // default on (NBT_VOICE_SCENARIO=0 disables): AudioMatrix mic scenario while the phone has the mic open
    if (mic_on) voice.init();
    const char* ui_state = "";
    int ui_overlay = -1;
    int night_posted = -1;
    // CarPlay hand-off: 2.5 s without an Android Auto session -> decoder context gets CarPlay's settings (an iPhone
    // taking over right away gets an unzoomed picture). Any later Android Auto connection rebuilds the Android Auto
    // context first (prepareForSession), so a quick reconnect is safe too. Only once the context exists.
    int cp_idle_ticks = 0;
    unsigned cp_live_ticks = 0;
    while (g_running) {
        for (int w = 0; w < 5 && !g_mainWake && g_running && !driveModePeekDue(); ++w)
            usleep(50000);   // 250 ms tick, cut short on a wake or when a driving-mode step-aside is due
        g_mainWake = 0;
        bool act = g_session.isActive();
        if (act || g_sessionActive) cp_idle_ticks = 0;
        else if (++cp_idle_ticks == 10 && nbt::VideoManager::cpHandoff()) video.retuneForCarPlay();
        {
            int n = dayNightToNight(g_dayNight);           // -1: no change / invalid, keep what the phone has
            if (n >= 0 && n != night_posted) {
                printf("[Night] car is in %s mode: Android Auto %s\n", n ? "night" : "day", n ? "dark theme" : "light theme");
                g_session.postNightMode(n != 0);
                night_posted = n;
            }
        }
        {
            const char* st = act ? "connected" : ((g_btLink || g_sessionActive) ? "connecting" : "idle");
            uint32_t mp = g_keyToPhoneMs;
            bool menuRecent = mp && (uint32_t)(monoMs() - mp) < 3000u;   // a hard key went to the phone
            if (mp && !menuRecent) g_keyToPhoneMs = 0;
            int ov = g_inputToPhone ? (g_backState == 1 ? 2 : (menuRecent ? 3 : 1)) : 0;
            if (strcmp(st, ui_state) != 0 || ov != ui_overlay) {
                writeUiState(st, ov);
                if (strcmp(st, ui_state) != 0) printf("[UiState] iDrive menu entry: %s\n", st);
                ui_state = st;
                ui_overlay = ov;
            }
        }
        // The A2I uplink is opened on the phone's first mic request and then kept running until the session ends;
        // while the phone has the mic closed the session simply drops the PCM. Opening/closing the endpoint per
        // Assistant request preceded a full head-unit reset on the car (2026-10-24, 2nd open of the session), and
        // the known-stable M2 build kept its A2I endpoints open for the whole run.
        int mr = 0;
        bool mic_ev = popSlot(g_micQ, &g_micN, &mr);
        // Scenario first: the AudioMatrix routes the processed car mic into the AirPlay uplink our capture reads.
        if (mr == 1 && mic_on && act) voice.activate("phone opened the mic");
        else if (mr == 2) voice.deactivate("phone closed the mic");
        // Default on (NBT_BT_REFOCUS=0 disables): the Assistant pauses the music before it listens, so its reply restarts a
        // suspended A2DP stream. Re-request the car's BT audio source when the phone closes the mic, in case the
        // car does not pick the restarted stream up on its own.
        if (mr == 2 && act && g_btAudio && btRefocus()) {
            char d[64];
            btDescribe(d, sizeof(d));
            printf("[BtAudio] mic closed: re-requesting car Bluetooth audio %s for the reply\n", d);
            btRequestSource();
        }
        // Assistant reply loudness (quiet next to music): snapshot the car's audio connections while it listens and
        // while it replies, to see whether the car ducks the Bluetooth source during voice. Read-only, first 3 turns.
        static int s_snaps = 0;
        if ((mr == 1 || mr == 2) && act && g_btAudio && s_snaps < 6) {
            ++s_snaps;
            g_btRoute.listConnections(mr == 1 ? "Assistant listening" : "Assistant replying", mr == 1 ? 1000 : 2500);
        }
        if (!act && voice.active()) voice.deactivate("session ended");
        if (mr == 1 && mic_on && act && !mic_live) {
            bool ok = g_audio ? g_audio->startMic() : micStart(mic);
            printf("[Mic] phone opened the mic: capture %s (kept open until the session ends)\n", ok ? "started" : "FAILED");
            mic_live = ok;
        } else if (mic_ev) {
            printf("[Mic] phone %s the mic (capture %s)\n", mr == 1 ? "opened" : "closed", mic_live ? "running" : "not running");
        }
        if (mic_live && !act) {
            if (g_audio) g_audio->stopMic(); else mic.stop();
            printf("[Mic] capture stopped (session ended)\n");
            mic_live = false;
        }
        ++log_sync_ticks;
        if (log_sync_ticks == 2400 && unlink(kBootGuardFile) == 0)   // 10 min up: not part of a bootloop
            printf("[Main] running for 10 min: autostart bootloop guard cleared\n");
        if ((log_sync_ticks % 20) == 0) serviceUsbLog(false);   // every 5 s: stick plugged in / removed, checkbox
        if (logPersistent() && (log_sync_ticks % 8) == 0) { fflush(stdout); fsync(fileno(stdout)); }
        if (act && !was_active) { g_userExit = 0; g_carExit = 0; }   // a new phone session comes up in Android Auto
        if (g_backState == 1 && (uint32_t)(monoMs() - g_backDownMs) >= backHoldMs()) {
            // Car-side minimize, like reverse: only the overlay goes down. Telling the phone (unrequested UNFOCUSED,
            // NBT_BACK_HOLD_FOCUS=1) can make it ask for the screen right back, which re-showed AA at once.
            g_backState = 2;
            g_userExit = 1;
            g_carExit = 1;
            if (act && nbt::envFlag("NBT_BACK_HOLD_FOCUS") == 1) g_session.postVideoFocus(false);
            printf("[Focus] BACK held %u ms: minimizing Android Auto, iDrive gets the screen\n", backHoldMs());
        }
        int ff = 0;
        if (popSlot(g_focusQ, &g_focusN, &ff)) {
            if (ff == 1 && g_carExit) {
                printf("[Focus] phone asked for the screen while minimized by BACK hold: ignored (use the iDrive entry)\n");
            } else {
                g_userExit = (ff == 2) ? 1 : 0;
                printf("[Focus] %s\n", ff == 2 ? "driver exited Android Auto: iDrive gets the screen" : "phone took the screen back");
            }
        }
        if (g_hideReq) {                                 // like MENU leaving native CarPlay: only the iDrive entry undoes it
            g_hideReq = 0;
            g_userExit = 1;
            g_carExit = 1;
            printf("[Focus] iDrive left the Android Auto screen (hard key): minimizing Android Auto\n");
        }
        if (g_showReq) {                                 // iDrive Android Auto entry selected
            g_showReq = 0;
            g_userExit = 0;
            g_carExit = 0;
            if (act) g_session.postVideoFocus(true);     // phone resumes projecting (it may have paused while native)
            printf("[Focus] iDrive entry selected: back to Android Auto%s\n", act ? "" : " (no phone session yet)");
            force_show = true;                           // re-assert even if we think it is up (HMI may have reordered layers)
            if (was_park) { park_dismissed = true; printf("[Park] Android Auto chosen during park assist: kept until it ends\n"); }
        }
        bool reverse = gear_action && g_gear == gear_reverse;
        if (reverse != was_reverse) {
            printf("[Gear] %s\n", reverse ? "REVERSE: overlay down for the camera" : "out of reverse: restoring");
            if (reverse_focus && act && !g_userExit) g_session.postVideoFocus(!reverse);
            was_reverse = reverse;
        }
        bool park_raw = pdc_action && pdcActive(g_pdc);
        bool neutral_due = neutral_resume.update(gear_action ? (int)g_gear : -1, gear_reverse, gear_neutral, upMs(),
                                                 neutral_resume_ms);
        if (neutral_resume.waiting() && !neutral_wait_logged) {
            neutral_wait_logged = true;
            printf("[Gear] [%9.3f] reverse -> neutral: Android Auto back in %u s if it stays in neutral\n", upSec(),
                   neutral_resume_ms / 1000u);
        } else if (!neutral_resume.waiting()) {
            neutral_wait_logged = false;
        }
        if (neutral_due) {
            if (g_userExit) {
                printf("[Gear] [%9.3f] %u s in neutral after reverse: Android Auto stays minimized (driver exited it)\n",
                       upSec(), neutral_resume_ms / 1000u);
            } else {
                printf("[Gear] [%9.3f] %u s in neutral after reverse: back to Android Auto%s\n", upSec(),
                       neutral_resume_ms / 1000u, park_raw ? " over the park-assist view" : "");
                if (park_raw) park_dismissed = true;     // like the iDrive entry during park assist: AA until it ends
                force_show = true;                       // re-assert even if we think it is up (HMI may have raised the camera)
                if (act && !overlay_on) g_session.postVideoFocus(true);   // phone resumes projecting if it paused
            }
        }
        if (!park_raw) park_dismissed = false;
        bool park = park_raw && !park_dismissed;
        if (park != was_park) {
            printf("[Park] %s\n", park ? "PARK ASSIST active: iDrive gets the screen for the parking view"
                                        : "park assist off: restoring");
            if (reverse_focus && act && !g_userExit) g_session.postVideoFocus(!park && !reverse);
            was_park = park;
        }
        // CarPlay taking the screen while an Android Auto session is still open (iPhone joins while the Android phone
        // stays connected, MINI tester 2026-10-29): CarPlay plays through our decoder context, so without its factory
        // settings it opens zoomed (the hand-off retune used to run only after the AA session ended). While Android
        // Auto is minimized we keep surface 1280 hidden; when it turns visible anyway, iDrive is showing CarPlay on it.
        // NBT_CP_LIVE_RETUNE=0 disables. Not car-verified.
        if (act && !overlay_on && cpLiveRetune() && video.hasWindow() && (++cp_live_ticks % 8) == 0 &&
            !video.decoderInCarPlayMode() && nbt::VideoManager::surface1280Visible() == 1) {
            printf("[Main] CarPlay took the screen while Android Auto is still connected: giving the decoder CarPlay's "
                   "settings (the Android Auto entry brings Android Auto back)\n");
            g_userExit = 1;
            g_carExit = 1;                               // the phone asking for the screen must not cover CarPlay
            g_session.postVideoFocus(false);             // phone stops streaming; FOCUSED on return makes it restart
            video.retuneForCarPlay();
        }
        // Driving mode changed (checkbox on): step aside like reverse, for a fixed time.
        uint32_t dmu = g_dmPeekUntil, dmf = g_dmPeekFrom, dmnow = upMs();
        bool dm_peek = dmu != 0 && (int32_t)(dmu - dmnow) > 0 && (int32_t)(dmnow - dmf) >= 0;
        g_dmShown = dm_peek ? 1 : 0;
        if (dm_peek != was_dm_peek) {
            printf("[DriveMode] [%9.3f] %s\n", upSec(), dm_peek ? "Android Auto steps aside for iDrive's driving-mode display"
                                                                 : "driving-mode display time over: restoring Android Auto");
            was_dm_peek = dm_peek;
        }
        bool want = (hide_idle ? act : true) && !g_userExit && !reverse && !park && !dm_peek;
        if (want && act && !overlay_on && video.decoderInCarPlayMode()) {
            printf("[Main] back to Android Auto after CarPlay: rebuilding the decoder context\n");
            video.prepareForSession();
            g_session.postVideoFocus(true);
        }
        g_inputToPhone = (want && act) ? 1 : 0;
        nbt::setTouchOwned(g_inputToPhone != 0);      // UDP 30567 goes back to the HMI whenever iDrive has the screen
        if (want != overlay_on || (force_show && want)) {
            video.setVisible(want);
            overlay_on = want;
            reassert_ticks = want ? 0 : -1;
        }
        force_show = false;
        // Coming back mid-session (iDrive entry, end of park assist / reverse), the iDrive app puts iDrive on its blank
        // Android Auto state about a second later and the HMI can raise its own surface over ours on that state change:
        // car 2026-10-28 (nbt-aad-23) came back black after a BACK-hold minimize. Re-assert ~2 s and ~5 s later, the
        // same thing the session start does.
        if (reassert_ticks >= 0) {
            ++reassert_ticks;
            if (!want || !overlay_on) reassert_ticks = -1;
            else if (reassert_ticks == 8 || reassert_ticks == 20) {
                printf("[Main] Re-asserting overlay (%d s after it came back).\n", reassert_ticks / 4);
                video.setVisible(true);
                if (reassert_ticks == 20) reassert_ticks = -1;
            }
        }
        if (act) {
            if (!was_active) {
                // The HMI / LayerManager may have rearranged layers and window order since the daemon
                // started (minutes ago, with iDrive in use): put our window back on top for the session.
                active_ticks = 0;
                if (want) {
                    printf("[Main] Session active: re-asserting overlay (window z-order + LayerManager).\n");
                    video.setVisible(true);
                    overlay_on = true;
                }
            }
            ++active_ticks;
            if (active_ticks == 20 && want) {   // ~5 s in: again, in case the HMI reacted to the new source
                printf("[Main] Re-asserting overlay (5 s into the session).\n");
                video.setVisible(true);
            }
            // Window/LayerManager dump ~8 s in: opt-in now (NBT_DIAG=1). Its `cat /dev/screen/<pid>/*/*` pulled ~200
            // lines of screen/slog output into the log right where the Toyota-icon and reverse tests start.
            if (active_ticks == 32 && !diag_done && getenv("NBT_DIAG")) {   // ~8 s in
                diag_done = true;
                pthread_t dt;
                if (pthread_create(&dt, NULL, &diagThread, NULL) == 0) pthread_detach(dt);
            }
        } else {
            active_ticks = 0;
        }
        was_active = act;
    }

    // Cleanup & graceful teardown
    // Every step is timed: the iDrive app sends SIGTERM 3 s after 'quit' and SIGKILL (no clean-up) 10 s after, and
    // the tester logs (2026-10-29) needed SIGTERM on every Force Stop, so the slow step has to show in the log.
    const double t0 = upSec();
    printf("[Main] [%9.3f] Shutdown requested, stopping subsystems...\n", t0);
    g_btMgr = NULL;            // no Bluetooth service re-registration from the wireless thread while shutting down
    g_btRoute.cancelPending(); // no queued audioconn starts while we tear down
    video.setVisible(false);   // give iDrive back first: any of the stop() calls below may block
    printf("[Main] [+%.2f s] overlay hidden\n", upSec() - t0);
    if (mic_live) { if (g_audio) g_audio->stopMic(); else mic.stop(); }
    voice.deactivate("shutdown");   // never leave the car mic scenario active behind us
    tcp_server.stop();         // no more onClientConnected -> no session can start behind our back
    printf("[Main] [+%.2f s] listener stopped\n", upSec() - t0);
    usb.stop();                // no new USB session either; ends the USB bridge (and with it a USB session)
    printf("[Main] [+%.2f s] USB accessory stopped\n", upSec() - t0);
    g_session.stop();
    printf("[Main] [+%.2f s] session stopped\n", upSec() - t0);
    g_video = NULL;
    g_audio = NULL;
    if (!g_btAudio) audio.stop();
    if (g_speechReady && g_speech) g_speech->stop();
    video.stop();
    printf("[Main] [+%.2f s] video stopped\n", upSec() - t0);

    if (g_ctlFd >= 0) close(g_ctlFd);
    unlink(kUiStateFile);   // plugin falls back to the plain "Android Auto" entry
    unlink(kBootGuardFile); // clean exit: the iDrive app's autostart bootloop guard starts from zero
    printf("[Main] [%9.3f] nbt-aad shutdown complete (%.2f s).\n", upSec(), upSec() - t0);
    fflush(stdout);
    // Leave without running static destructors: detached threads (DSI sender loops, wireless bring-up) may still
    // be running, and tearing globals down under them crashed at exit (nbt-aad-21: SIGSEGV after "shutdown complete").
    _exit(0);
}
