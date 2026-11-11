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
#include "audio/config.hpp"
#include "car_config.hpp"
#include "screen_video.h"
#include "audio/a2i_audio.hpp"
#include "audio/backend.hpp"
#include "audio/browser_audio.hpp"
#include "volume.hpp"
#include "volume_osd.hpp"
#include "audio/bt_audio.hpp"
#include "audio/mic_uplink.hpp"
#include "video_manager.hpp"
#include "license_text.hpp"
#include "tcp_server.hpp"
#include "usb_accessory.hpp"
#include "session_owner.hpp"
#include "aa_session.hpp"
#include "bluetooth_manager.hpp"
#include "wireless_setup.hpp"
#include "alt_wifi.hpp"
#include "keyframe_ask.hpp"
#include "touch_probe.hpp"
#include "aa_keys.hpp"
#include "car/ccs_config.hpp"
#include "car/ccs_input.hpp"
#include "car/volume_input.hpp"
#include "car/input_activity.hpp"
#include "car/gps.hpp"
#include "car/vehicle.hpp"
#include "most/client.hpp"
#include "most/trace.hpp"
#include "crash_handler.hpp"
#include "a2i_client.h"
#include "audio/media_state.hpp"
#include "audio/voice_scenario.hpp"
#include "usb_log.hpp"
#include "spp_rereg.hpp"
#include "spp_link_policy.hpp"
#include "../tools/pf_aa.hpp"
#include "spp_direction.hpp"
#include "neutral_resume.hpp"
#include "cluster/nav_core.hpp"
#include "cluster/ric_media.hpp"
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
static nbt::kombinav::KombiNav g_kombiNav;   // Android Auto turn arrows on the J29 cluster / HUD (opt-in)
static nbt::riclist::KombiMedia g_kombiMedia;   // the track title in the cluster's track list (NBT_KOMBI_MEDIA=1)
static nbt::most::Client g_most;             // the one MOST observe client, started once its consumers have attached
static nbt::car::CcsInput g_ccs;             // iDrive controller and voice button: a consumer of g_most
static nbt::car::VolumeInput g_carVolume;    // the car's master volume for the overlay: a consumer of g_most
static nbt::car::InputActivity g_carInput;   // any car control the driver touched: a consumer of g_most
static nbt::car::CarGps g_gps;               // the car's GPS fix: a consumer of g_most
static nbt::car::CarVehicle g_vehicle;       // the car's wheel speed, gear and parking brake: a consumer of g_most
static nbt::most::Trace g_mostTrace;         // NBT_MOST_LOG=1: logs what g_most sees

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
static volatile sig_atomic_t g_cpYielded = 0;   // CarPlay took the screen: Android Auto yielded completely (v1.9.48)
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
// iDrive camera view (rhmiapp-46 "camera <ctx>"): the iDrive app saw a camera context (NBT_AA_CAMERA_CTX) in
// hmi.graphicalContext - the button-opened 360 / Top View, which neither reverse nor PDCStatus reports. 0 = none.
static volatile int g_camera = 0;

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
// DSC / DTC button ("same as sport mode"): the app sends "dsc <n> <prev>" when CDS driving.DSCActive changes, so
// iDrive's own DSC/DTC display gets the screen for the same fixed time as a driving-mode change and Android Auto
// comes back by itself.
static void onDscMsg(const char* s) {
    int v = -1, prev = -1;
    sscanf(s, "%d %d", &v, &prev);
    if (!showDriveModeOn()) { printf("[DSC] %d (Show driving mode changes is off)\n", v); return; }
    const uint32_t now = upMs();
    const uint32_t until = now + (uint32_t)driveModeShowMs();
    if (g_dmShown) {
        g_dmPeekUntil = until ? until : 1;
        printf("[DSC] [%9.3f] %d -> %d: iDrive keeps the screen longer for its DSC display\n", upSec(), prev, v);
        return;
    }
    g_dmPeekFrom = now;
    g_dmPeekUntil = until ? until : 1;
    g_mainWake = 1;
    printf("[DSC] [%9.3f] %d -> %d: iDrive gets the screen for %d ms for its DSC/DTC display\n",
           upSec(), prev, v, driveModeShowMs());
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
        g_vehicle.gearFromApp(g);
    }
    fclose(f);
}

static uint16_t ctlPort() {
    const char* p = getenv("NBT_CTL_PORT");
    int v = (p && *p) ? atoi(p) : 52790;
    return (uint16_t)((v > 0 && v < 65536) ? v : 52790);
}

// Control port "media ..." (now-playing record or a transport key). False: nothing to reply.
static bool mediaCtlPrepare(const char* cmd, std::string& reply);
static void resetMedia(const char* why);   // now-playing record (defined with the media block below)

static void* ctlThread(void*) {
    char buf[512];
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
            g_vehicle.gearFromApp(g);
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
        else if (!strncmp(buf, "media", 5) && (buf[5] == 0 || buf[5] == ' ' || buf[5] == '\n')) {
            std::string reply;
            if (mediaCtlPrepare(buf + 5, reply))
                sendto(g_ctlFd, reply.data(), reply.size(), 0, (struct sockaddr*)&from, flen);
        }
        else if (!strncmp(buf, "drivemode ", 10)) onDriveModeMsg(buf + 10);
        else if (!strncmp(buf, "dsc ", 4)) onDscMsg(buf + 4);
        else if (!strncmp(buf, "camera ", 7)) {
            int v = atoi(buf + 7);
            if (v != g_camera) printf("[Camera] iDrive camera view %d -> %d (from iDrive app)\n", (int)g_camera, v);
            g_camera = v;
        }
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
// (the plugin re-holds iDrive instead of treating MENU as a hard key), 4 = same, another car control in the last 3 s,
// 5 = same, only the knob turned or was pushed in the last 3 s (v1.9.19), 2 = same, and BACK is being held (hold timer running): iDrive
// sees the BACK press too and leaves the blank state, and the plugin must not put it back while the hold may still
// minimize Android Auto (car 2026-10-28, rhmiapp-11: refocus fought the minimize).
static const char* const kUiStateFile = "/dev/shmem/nbt-aad-state";
// The iDrive app's "Start automatically" bootloop guard (rhmiapp-20): it records each autostart here and skips the
// autostart after 3 within 15 min. A clean exit or 10 min of uptime clears it (persistent: survives the reset).
static const char* const kBootGuardFile = "/var/opt/aa/bootguard";
static void writeUiState(const char* state, int overlay) {
    // One fixed record, written over the previous bytes before the file is shortened. QNX /dev/shmem
    // has no rename(), and opening with O_TRUNC left a moment where the plugin read nothing and then
    // tried to bind the control port itself.
    // "%-11.11s %10d %d\n" is 25 bytes for a one-digit overlay (0 shown, 1 on screen, 2 BACK held, 3 menu key, 4 car
    // control, 5 knob turn / push only).
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
static nbt::SessionOwner g_owner;               // which transport that session came in on (session_owner.hpp)
static pthread_mutex_t g_connectMutex = PTHREAD_MUTEX_INITIALIZER;   // serializes startSession() (TCP and USB)
static bool wirelessSessionActive() { return g_owner.get() == nbt::kTransportTcp; }
static nbt::VideoManager* g_video = NULL;       // for the session callbacks (valid between init and teardown)
// The audio mode (NBT_AUDIO_MODE=bt|browser|a2i, audio/backend.hpp): BtAudio, BrowserAudio or A2iAudio, picked once
// at start-up. Same lifetime rule as g_video.
static nbt::AudioBackend* g_audioBackend = NULL;

// Prints the Wi-Fi clients the head unit currently knows on the AP subnet (ARP table), so a log
// shows whether the phone actually joined the network it was offered: the /24 of the address the daemon listens on
// (CarPlay hotspot 172.16.224.x, Wi-Fi Direct 172.16.222.x, or NBT_AP_IP's). It used to look for 172.16.224. only and
// so reported "none" for every Wi-Fi Direct run.
static void logStations(const char* tag) {
    std::string net = nbt::subnetPrefix24(nbt::wirelessListenIp());
    if (net.empty()) net = "172.16.224.";
    FILE* p = popen("arp -an", "r");
    if (!p) return;
    char line[256];
    int n = 0;
    while (fgets(line, sizeof(line), p)) {
        if (strstr(line, net.c_str())) {
            size_t l = strlen(line);
            while (l && (line[l - 1] == '\n' || line[l - 1] == '\r')) line[--l] = 0;
            printf("[Wireless] AP client (%s): %s\n", tag, line);
            ++n;
        }
    }
    pclose(p);
    if (!n) printf("[Wireless] AP clients (%s): none in ARP table (%sx)\n", tag, net.c_str());
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
static nbt::UsbAccessory* g_usb = NULL;        // set while Android Auto over USB runs (NBT_USB=1)
static const uint32_t kUsbByeWaitMs = 500;     // of the 3 s before the iDrive app sends SIGTERM after 'quit'
// Full yield to CarPlay (v1.9.48, issue #156): when CarPlay takes the screen while an Android Auto session is still open,
// nbt-aad ends the session and stops, instead of handing CarPlay its decoder context with CarPlay's settings. Once
// Android Auto has run, CarPlay plays through OUR context, and CarPlay's video window was created inside our hidden
// window group (vis 0): a black CarPlay until the head unit restarted (#156 log: "mm-renderer's video window joined our
// group"). Stopping takes the session, the decoder context, our windows and the group with it, so CarPlay starts from
// the state it has with no Android Auto. The Android Auto entry starts nbt-aad again.
// Since v1.9.50-cpdetach the factory context is no longer destroyed - only detached while Android Auto has the decoder,
// and re-attached when nbt-aad stops (VideoManager::detachFactoryContext / reattachFactoryContext).
// v1.9.57-cpcompat: all of it only with the "CarPlay compatibility" Settings checkbox (nbt_env.hpp cpCompatEnabled());
// NBT_CP_YIELD=1 / 0 in nbt-aad.env wins for the yield.
static bool cpYieldEnabled() { return nbt::cpYieldOn(); }
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

// The car's Wi-Fi hotspot config: NBT_AP_CFG, else iap2.cfg / iap2_Wifi.cfg. Cars without CarPlay (no Wi-Fi hotspot)
// have none, and then there is nothing a phone could be told over Bluetooth.
static const int kApCfgRecheckSec = 10;

static bool apConfigPresent(const char* cfg) {
    // DIRECT (checkbox or NBT_ALT_WIFI): the Wi-Fi Direct group counts as a usable Wi-Fi even when the car has no CarPlay
    // hotspot config at all (cars without the CarPlay licence). Quiet: this runs every kApCfgRecheckSec.
    nbt::AltWifiMode m = nbt::altWifiMode();
    if (m != nbt::ALT_WIFI_CARPLAY) {
        nbt::ApInfo wfd;
        std::string used;
        if (nbt::loadWfdAp(nbt::wfdPassPaths(), nbt::wfdPassPathCount(), wfd, used, true)) return true;
        if (m == nbt::ALT_WIFI_DIRECT) return false;
    }
    if (cfg) {
        nbt::ApInfo ap;
        return nbt::loadCarPlayApConfig(cfg, ap, true);
    }
    return !nbt::collectApCandidates(nbt::kApCfgPaths, nbt::kApCfgPathCount).empty();
}

static void logNoApConfig(const char* cfg) {
    if (nbt::altWifiMode() == nbt::ALT_WIFI_DIRECT)
        printf("[Wireless] no usable Wi-Fi Direct passphrase (/var/opt/conn/WFDPass.conf, then "
               "/fs/sda1/opt/conn/WFDPass.conf): NBT_ALT_WIFI=DIRECT has no CarPlay fallback, so wireless Android "
               "Auto is unavailable, Bluetooth links are left alone; checking again every %d s\n", kApCfgRecheckSec);
    else
        printf("[Wireless] no Wi-Fi hotspot config (%s): wireless Android Auto is unavailable, Bluetooth links are "
               "left alone; checking again every %d s. A car without the CarPlay hotspot (no CarPlay licence) can use "
               "its Wi-Fi Direct group: tick \"Use WiFi Direct Network\" in Settings\n",
               cfg ? cfg : "iap2.cfg / iap2_Wifi.cfg", kApCfgRecheckSec);
}

// One Wi-Fi the phone may be offered. File-scope (not inside wirelessThread) because GCC 4.4.2 rejects a local
// type as a std::vector argument even under -std=gnu++0x.
struct ApChoice {
    nbt::ApInfo ap;
    std::string path;
    const char* ifn;
    bool direct;
};

// The Wi-Fi Direct passphrase is printed once per value (not per link attempt): it is decoded from the car's own
// WFDPass.conf, so it can be typed into a phone's Wi-Fi settings by hand - that is how a DIRECT join gets checked
// without the daemon getting it right first.
static void logWfdAp(const nbt::ApInfo& ap, const std::string& path, std::string& shown) {
    char tag[512];
    snprintf(tag, sizeof(tag), "%s|%s|%s", path.c_str(), ap.ssid.c_str(), ap.key.c_str());
    if (shown == tag) return;
    shown = tag;
    printf("[Wireless] Wi-Fi Direct AP: ssid='%s', passphrase from %s: '%s' (%u chars, decoded; type it into the "
           "phone's Wi-Fi settings to join the group by hand)\n",
           ap.ssid.c_str(), path.c_str(), ap.key.c_str(), (unsigned)ap.key.size());
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
    const char* wfdIfn = getenv("NBT_WFD_IF");
    if (!wfdIfn || !*wfdIfn) wfdIfn = "wfd0";
    // NBT_ALT_WIFI: which Wi-Fi the phone is offered (see alt_wifi.hpp). CARPLAY unset = the behaviour of every
    // earlier build.
    const nbt::AltWifiMode altMode = nbt::altWifiMode();
    printf("[Wireless] Wi-Fi source: %s (%s; NBT_ALT_WIFI=%s): %s\n", nbt::altWifiName(altMode), nbt::altWifiSource(),
           getenv("NBT_ALT_WIFI") && *getenv("NBT_ALT_WIFI") ? getenv("NBT_ALT_WIFI") : "(unset)",
           altMode == nbt::ALT_WIFI_DIRECT
               ? "the car's Wi-Fi Direct group only, no CarPlay fallback"
               : "the CarPlay hotspot only, no Wi-Fi Direct fallback");
    if (nbt::altWifiRetiredBoth(getenv("NBT_ALT_WIFI")))
        printf("[Wireless] NBT_ALT_WIFI=BOTH is no longer an option (v1.9.37): using the CarPlay hotspot. Tick \"Use "
               "WiFi Direct Network\" (or NBT_ALT_WIFI=DIRECT) for the Wi-Fi Direct group\n");
    // Watch the interface of the source offered (CARPLAY: the hotspot; DIRECT: wfd0).
    const char* pollIfn = altMode == nbt::ALT_WIFI_DIRECT ? wfdIfn : ifn;

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
    int apCfgState = -1;        // Wi-Fi hotspot config: -1 not checked yet, 0 none, 1 present
    uint32_t apCfgChecked = 0;
    bool usbHeld = false;       // a phone is on the USB cable (NBT_USB=1): wireless steps aside
    while (g_running) {
        // Nothing to do while a projection session is running; re-arm after it ends.
        if (g_sessionActive) { idleWait = false; sleep(1); continue; }

        // Watch the access point while idle (every ~2 s): when it comes back after being off, the car was switched
        // off and on again under us -> refresh the firewall and the listener before the phone tries again.
        if ((apPoll++ % 4) == 0) {
            int st = ifaceHasIpv4(pollIfn);
            if (st >= 0 && st != apState) {
                if (st == 0) printf("[Wireless] %s has no IP address: the car's Wi-Fi access point is off\n", pollIfn);
                else if (apState == 0) {
                    recoverWifiPath("the car's Wi-Fi access point came back");
                    idleWait = false;   // the car woke up: start the re-registration schedule from its first step
                }
                apState = st;
            }
        }

        // A phone on the USB cable gets Android Auto over USB: leave the Bluetooth links alone, as without a hotspot
        // config, so it is not also handed the Wi-Fi (its wireless connection would be refused by startSession()).
        nbt::UsbAccessory* usb = g_usb;
        bool usbPhone = usb && usb->phonePresent();
        if (usbPhone != usbHeld) {
            printf("[Wireless] %s\n", usbPhone ? "phone on the USB cable: Bluetooth links are left alone while it is"
                                                 " plugged in"
                                               : "no phone for USB any more (unplugged, or USB gave up on it): "
                                                 "wireless Android Auto available");
            usbHeld = usbPhone;
        }
        if (usbPhone) { idleWait = false; usleep(500000); continue; }

        // No Wi-Fi hotspot config: do not open the phone's Bluetooth links (we would only close them again) and do
        // not re-register the Bluetooth service; look for the config again every kApCfgRecheckSec.
        if (apCfgState != 1) {
            uint32_t now = monoSecs();
            if (apCfgState < 0 || now - apCfgChecked >= (uint32_t)kApCfgRecheckSec) {
                apCfgChecked = now;
                int st = apConfigPresent(cfg) ? 1 : 0;
                if (st == 0 && apCfgState < 0) logNoApConfig(cfg);
                if (st == 1 && apCfgState == 0) printf("[Wireless] Wi-Fi hotspot config found: wireless Android Auto available\n");
                apCfgState = st;
            }
            if (apCfgState != 1) { idleWait = false; usleep(500000); continue; }
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
        // Which Wi-Fi this link is offered (NBT_ALT_WIFI, alt_wifi.hpp):
        //   CARPLAY  the car's hotspot from iap2.cfg (iap2_Wifi.cfg as a second passphrase), no DIRECT fallback
        //   DIRECT   the car's Wi-Fi Direct group only, no hotspot fallback
        std::vector<ApChoice> directChoices;   // the Wi-Fi Direct group (wfd0)
        std::vector<ApChoice> cpChoices;       // the CarPlay hotspot (iap2.cfg / iap2_Wifi.cfg)
        if (altMode != nbt::ALT_WIFI_CARPLAY) {
            nbt::ApInfo wfd;
            std::string used;
            // Quiet here: logWfdAp() prints the passphrase once per value, not on every link attempt.
            bool havePass = nbt::loadWfdAp(nbt::wfdPassPaths(), nbt::wfdPassPathCount(), wfd, used, true);
            std::string wfdSsid;
            if (havePass && nbt::resolveWfdSsid(wfdIfn, wfdSsid)) {
                wfd.ssid = wfdSsid;
                ApChoice c;
                c.ap = wfd;
                c.path = used;
                c.ifn = wfdIfn;
                c.direct = true;
                directChoices.push_back(c);
            } else if (havePass) {
                printf("[Wireless] Wi-Fi Direct: no SSID for %s, skipping the DIRECT group on this link (NBT_WFD_SSID "
                       "pins one)\n", wfdIfn);
            } else {
                printf("[Wireless] Wi-Fi Direct: no usable passphrase file (%s), skipping the DIRECT group on this "
                       "link\n", nbt::wfdPassPaths()[0]);
            }
            if (altMode == nbt::ALT_WIFI_DIRECT) {
                // Logged once, for the car log; a normal Wi-Fi join uses the passphrase above, not the PIN.
                static bool wpsShown = false;
                std::string pin, pinPath;
                if (!wpsShown && nbt::loadWfdWpsPin(nbt::wfdWpsPaths(), nbt::wfdWpsPathCount(), pin, pinPath))
                    wpsShown = true;
            }
        }
        if (altMode != nbt::ALT_WIFI_DIRECT) {
            if (cfg) {
                nbt::ApInfo a;
                if (!nbt::loadCarPlayApConfig(cfg, a)) {
                    if (directChoices.empty()) {
                        close(fd);
                        logNoApConfig(cfg);
                        apCfgState = 0;
                        apCfgChecked = monoSecs();
                        continue;
                    }
                } else {
                    ApChoice c;
                    c.ap = a;
                    c.path = cfg;
                    c.ifn = ifn;
                    c.direct = false;
                    cpChoices.push_back(c);
                }
            } else {
                std::vector<nbt::ApCandidate> cands =
                    nbt::collectApCandidates(nbt::kApCfgPaths, nbt::kApCfgPathCount);
                for (size_t i = 0; i < cands.size(); ++i) {
                    ApChoice c;
                    c.ap = cands[i].ap;
                    c.path = cands[i].path;
                    c.ifn = ifn;
                    c.direct = false;
                    cpChoices.push_back(c);
                }
            }
        }
        // One source per run: the hotspot candidates (CARPLAY) or the DIRECT group (DIRECT), never the other.
        std::vector<ApChoice> choices = nbt::altWifiPrefersCarplay() ? cpChoices : directChoices;
        if (choices.empty()) {
            close(fd);
            logNoApConfig(cfg);
            apCfgState = 0;
            apCfgChecked = monoSecs();
            continue;
        }
        const size_t apCount = choices.size();
        const ApChoice& chosen = choices[(size_t)apIdx % apCount];
        nbt::ApInfo ap = chosen.ap;
        const std::string apPath = chosen.path;
        if (chosen.direct) {
            logWfdAp(ap, apPath, apShown);
        } else {
            char tag[400];
            snprintf(tag, sizeof(tag), "%s|%s|%u", apPath.c_str(), ap.ssid.c_str(), (unsigned)(apIdx % apCount));
            if (apShown != tag) {
                apShown = tag;
                printf("[Wireless] CarPlay AP: ssid='%s', passphrase from %s (%u of %u distinct)\n", ap.ssid.c_str(),
                       apPath.c_str(), (unsigned)(apIdx % apCount) + 1, (unsigned)apCount);
            }
        }
        nbt::fillApNetworkInfo(chosen.ifn, ap);
        if (ap.ip.empty()) {
            printf("[Wireless] no IP on %s, retry\n", chosen.ifn);
            close(fd);
            sleep(5);
            continue;
        }
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
            bool everActive = false;   // the wireless phone connected (a USB session does not count)
            bool toUsb = false;        // the phone was plugged in meanwhile: Android Auto continues over USB
            int tick = 0;
            while (g_running) {
                bool alive = nbt::wppService(fd, 1000, ap, g_aaPort);
                ++tick;
                if (wirelessSessionActive()) everActive = true;
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
                if (!everActive && g_usb && g_usb->phonePresent()) {
                    printf("[Wireless] phone on the USB cable: releasing the Bluetooth link, Android Auto goes over "
                           "USB\n");
                    toUsb = true;
                    break;
                }
                if (!g_sessionActive && (tick % 10) == 5) logStations("waiting for phone TCP connect");
                if (!everActive && tick > 90) { printf("[Wireless] phone did not connect within 90 s\n"); break; }
                if (everActive && !wirelessSessionActive()) { printf("[Wireless] session ended, releasing the Bluetooth link\n"); break; }
            }
            g_btLink = 0;
            close(fd);
            if (!pinned) noteSppDirection(dev, nbt::lastLinkSpokeWpp() || everActive);
            // A phone that joined the Wi-Fi usually drops Bluetooth before its TCP connect arrives: give it a
            // few seconds before counting this as a "never joined" try.
            if (toUsb) continue;       // not a failed wireless try: no recovery, no passphrase rotation
            for (int w = 0; !everActive && g_running && w < 40; ++w) {
                if (wirelessSessionActive()) everActive = true;
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

// ---- Car microphone -> phone (Assistant / voice search), AA channel 14 (audio/mic_uplink.hpp) -------------------
static void onMicRequest(bool open, void* user) {
    (void)user;
    pushSlot(g_micQ, &g_micN, open ? 1 : 2);
}

// Echo-cancelled mic chunk (capture thread): to the AA mic channel, then the level meter / dump while the phone has
// the mic open.
static void onMicData(const uint8_t* pcm_data, size_t size, void* user_data) {
    (void)user_data;
    g_session.postMicData(pcm_data, size);   // echo-cancelled chunk -> AA mic channel (dropped unless open)
    if (!g_session.micOpen()) return;         // capture keeps running between Assistant requests: meter/dump only while open
    nbt::micLevelMeter(pcm_data, size);
}

static void onVideoFrame(uint32_t frame_index, const uint8_t* nal_data, size_t nal_len, uint64_t pts, void* user_data) {
    // [VideoRate] (PR #66): phone video frames counted at the AA callback boundary, before queueing; one average every
    // five seconds, next to what the phone was asked for (NBT_VID_FPS, default 30) and the decoder rate.
    struct VideoRateProbe {
        uint64_t frames;
        struct timespec window_start;
        bool started;
        VideoRateProbe() : frames(0), started(false) {}
    };
    static VideoRateProbe probe;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    ++probe.frames;
    if (!probe.started) {
        probe.window_start = now;
        probe.started = true;
    } else {
        const double elapsed = (now.tv_sec - probe.window_start.tv_sec) +
                               (now.tv_nsec - probe.window_start.tv_nsec) / 1000000000.0;
        if (elapsed >= 5.0) {
            printf("[VideoRate] phone frames=%llu in %.2f s (%.1f fps), requested=%u fps, decoder %u fps\n",
                   (unsigned long long)probe.frames, elapsed, probe.frames / elapsed, nbt::videoRequestedFps(),
                   nbt::videoDecoderFps());
            probe.frames = 0;
            probe.window_start = now;
        }
    }
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
    (void)user_data;
    if (g_audioBackend) g_audioBackend->onFrame(channel, pcm, pcm_len, pts);
}

static void onAudioConsumed(uint8_t channel, size_t bytes, void* user_data) {
    (void)user_data;
    g_session.postAudioConsumed(channel, bytes);
}

static void onAudioUnderflow(uint8_t channel, void* user_data) {
    (void)user_data;
    g_session.postAudioUnderflow(channel);
}

static void onAudioState(uint8_t channel, bool started, uint32_t rate, uint32_t channels, void* user_data) {
    (void)user_data;
    if (g_audioBackend) g_audioBackend->onStreamState(channel, started, rate, channels);
}

static double g_sessionStartS = 0;   // upSec() of the last onClientConnected
static int g_shortSessions = 0;      // sessions in a row that ended within kShortSessionS of connecting
static const double kShortSessionS = 15.0;
static void onSessionEnded(void* user_data) {
    nbt::UsbAccessory* usb = g_usb;
    const nbt::AASession::Outcome outcome = g_session.outcome();
    if (usb && g_owner.get() == nbt::kTransportUsb && outcome != nbt::AASession::kEstablished)
        usb->noteSessionFailed(outcome == nbt::AASession::kStale);
    g_sessionActive = false;
    g_owner.set(nbt::kTransportNone);
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
    // Browser: mixer stopped, source released. Bluetooth: queued audioconn requests dropped, NBT_BT_RELEASE.
    if (g_audioBackend) g_audioBackend->onSessionEnded();
    g_kombiNav.onSessionEnded();   // the worker clears a cluster/HUD arrow
    resetMedia("session ended");   // the iDrive app must not show the last track of a session that is over
}

// UsbAccessory: no new USB session (and no accessory switch) while a phone is already connected, and a USB bridge
// whose session is over (owner no longer USB) is ended.
static nbt::Transport sessionOwner(void*) {
    return g_owner.get();
}

// The one way a session starts, for both transports (the TCP accept / connector threads and the USB watch thread
// may call it at the same time): mayStart() decides whether `t` may take the session from its current owner.
static void startSession(int client_fd, nbt::TcpServer* server, nbt::Transport t) {
    pthread_mutex_lock(&g_connectMutex);
    nbt::Transport cur = g_owner.get();
    if (!nbt::mayStart(cur, t)) {
        printf("[Main] [%9.3f] %s Android Auto session running: refusing the %s connection on fd %d\n", upSec(),
               nbt::transportName(cur), nbt::transportName(t), client_fd);
        close(client_fd);
        pthread_mutex_unlock(&g_connectMutex);
        return;
    }
    // End the session this one replaces (a wireless phone that reconnected before we noticed it left) before
    // starting: its onSessionEnded() runs now, so it cannot clear the new session's flags afterwards.
    g_session.stop();
    g_owner.set(t);
    g_sessionStartS = upSec();
    printf("[Main] [%9.3f] Android Auto client connected over %s on fd %d. Initializing aasdk session...\n",
           g_sessionStartS, nbt::transportName(t), client_fd);
    g_sessionActive = true;
    resetMedia("new session");
    if (server) server->markSessionActive();
    if (g_video && nbt::VideoManager::cpHandoff()) g_video->prepareForSession();   // decoder ready before frames
    if (!g_session.start(client_fd)) {
        // start() closed the fd; make sure the server does not wait for a session that never began
        g_owner.set(nbt::kTransportNone);
        g_sessionActive = false;
        if (server) server->notifySessionEnded();
        pthread_mutex_unlock(&g_connectMutex);
        return;
    }
    if (g_audioBackend) g_audioBackend->onSessionStarted();   // Bluetooth mode: request the car's BT source
    pthread_mutex_unlock(&g_connectMutex);
}

static void onClientConnected(int client_fd, void* user_data) {
    startSession(client_fd, reinterpret_cast<nbt::TcpServer*>(user_data), nbt::kTransportTcp);
}

static void onUsbConnected(int client_fd, void* user_data) {
    startSession(client_fd, reinterpret_cast<nbt::TcpServer*>(user_data), nbt::kTransportUsb);
}

// One-shot diagnostics for "frames reach the decoder but nothing is visible": dump the LayerManager
// state and the QNX Screen window properties (order / above / below / visible) of our window and of
// the HMI's windows. Runs in its own thread so a slow cat can never stall the main loop.
static void* diagThread(void*) {
    char cmd[320];
    int me = (int)getpid();
    printf("[Diag] --- LayerManager ---\n");
    fflush(stdout);
    snprintf(cmd, sizeof(cmd), "/opt/sys/bin/LayerManagerControl get surface %d", nbt::VideoManager::aaSurface());
    system(cmd);                                                        // Android Auto's own surface (1282)
    if (!nbt::VideoManager::sharesCarPlaySurface())
        system("/opt/sys/bin/LayerManagerControl get surface 1280");   // the factory CarPlay's, read only
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

// ---- Volume OSD (NBT_VOL_OSD) ---------------------------------------------------------------------------
// The AA-side gain stage is the only volume we can actually set (no A2I/amp gain call exists on this unit), so
// the overlay and the gain belong together: every change goes through volumeApply().
static nbt::VolumeOsd  g_volumeOsd;
static nbt::BrowserAudio* g_browserAudio = NULL;

// The level the car reports on FBlock 0x20 is the amplifier's own master volume, which already governs our Android Auto
// output -- so by default the overlay only mirrors it. Re-applying the same curve on the AA mixer would attenuate twice.
// NBT_VOL_GAIN=1 also drives the AA mixer (full -40..0 dB curve) for the case where the car's volume turns out not to
// reach our stream; one flag, so that is decided on the car instead of guessed here.
static bool volGainEnabled() {
    static int v = -1;
    if (v < 0) { const char* e = getenv("NBT_VOL_GAIN"); v = (e && *e) ? atoi(e) : 0; }
    return v != 0;
}

static void volumeApply(int pct, bool setGain) {
    const int p = nbt::volumeClampPct(pct);
    if (setGain && volGainEnabled() && g_browserAudio) g_browserAudio->setMediaGain(nbt::volumePctToGain(p));
    g_volumeOsd.setPct(p);
    printf("[Volume] %d%% (%d dB)%s\n", p, nbt::volumePctToDb(p),
           (setGain && volGainEnabled()) ? " [mixer]" : "");
}

// There is no demo sweep. The car's own volume arrives for real (AudioMaster 0x20/0x0C08, car-verified 2026-11-03) and
// drives the pill; a synthetic sweep only ever fought the driver's input. NBT_VOL_DEMO is gone and ignored.

// The pill reads the panel size from the projection window, so it is created once that window exists (lazily, here)
// and again only if the projection window is replaced.
static void* g_volOsdParent = NULL;
static bool g_volOsdOn = false;
// The volume pill is on by default (v1.9.13): NBT_VOL_OSD=0 turns it off. Since v1.9.18 it is LayerManager surface 1281
// of its own (car-verified 2026-11-05, tools/osd1281probe), no longer the Android Auto picture's surface 1280, which is
// what gave the black screen with only the pill, the reverse camera under Android Auto and the grey picture after the
// decoder rebuild (v1.9.08-v1.9.17).
static bool volOsdEnabled() {
    const char* v = getenv("NBT_VOL_OSD");
    if (!v || !*v) return true;
    return !(v[0] == '0' && (v[1] == 0 || v[1] == ' '));
}

static uint32_t monoMs();
// monoMs() of the last car control the driver touched (any controller key or turn, steering-wheel volume / voice, a car
// volume change), from g_carInput / onCarVolume. Overlay 4 tells the iDrive app "iDrive just reacted to the driver";
// overlay 1 when iDrive leaves the blank state means the car opened a screen of its own (camera, safety, warning).
static volatile uint32_t g_carInputMs = 0;
// A knob turn or push alone (overlay 5). iDrive does nothing with either on its blank Android Auto state, so the iDrive
// app no longer takes them for the driver opening a screen: the safety button pressed 1.4 s after a push left Android
// Auto over the safety menu (car 2026-11-05). Where iDrive's focus is elsewhere the app still counts them.
static volatile uint32_t g_knobInputMs = 0;
static bool knobOnlyInput(const char* what) { return what && (!strcmp(what, "ROTARY") || !strcmp(what, "PUSH")); }
static void onCarInput(const char* what, void*) {
    if (knobOnlyInput(what)) g_knobInputMs = monoMs() | 1u;
    else g_carInputMs = monoMs() | 1u;
    static uint32_t s_logged = 0;
    if (monoMs() - s_logged > 2000u) { s_logged = monoMs(); printf("[Input] driver input: %s\n", what); }
}

// Car volume level (AudioMaster FBlock 0x20 fkt 0x0C08, car-verified 2026-11-03, car/volume_level.hpp): mirror it on
// the overlay. This is the value the amplifier actually applied, so the pill always matches what the driver hears.
// From g_carVolume on the DSI callback thread.
static void onCarVolume(int level, int max, void*) {
    g_carInputMs = monoMs() | 1u;   // iDrive draws its own volume bar: that is the driver, not the car
    volumeApply(nbt::car::VolumeLevel::percent(level, max), false);
}

static uint32_t g_idrWaitMs = 0;   // decoder rebuilt on the way back: waiting for the phone's keyframe since
static unsigned g_idrAsks = 0;
// A dropped video frame: ask the phone for a keyframe (keyframe_ask.hpp). On unless NBT_DROP_IDR=0.
static nbt::KeyframeAsk g_keyframeAsk;   // main loop only
static bool dropIdrEnabled() {
    static int s = -1;
    if (s < 0) s = nbt::envFlag("NBT_DROP_IDR") != 0 ? 1 : 0;
    return s == 1;
}
// Take the pill's surface out of the Android Auto layer's order first, then remove its window: the order never names a
// surface whose window is gone.
static void volOsdDown(nbt::VideoManager& vm) {
    if (vm.overlaySurface()) {
        vm.setOverlaySurface(0);
        vm.applyOverlayOrder();
    }
    g_volumeOsd.stop();
    g_volOsdOn = false;
}
// Surface 1281 cannot take the picture's surface, so there is no ordering against mm-renderer's window and no rebuild
// with the decoder any more (v1.9.14-v1.9.17): one pill window per projection window, for the whole run.
static void ensureVolOsd(nbt::VideoManager& vm) {
    if (!volOsdEnabled() || !vm.hasWindow()) return;
    void* w = (void*)vm.screenWindow();
    if (w == g_volOsdParent) return;   // up, or already tried for this projection window
    if (g_volOsdOn) volOsdDown(vm);
    g_volOsdParent = w;
    printf("[VolOsd] projection window up: creating the volume pill (LayerManager surface %d)\n",
           nbt::VolumeOsd::kSurfaceId);
    g_volOsdOn = g_volumeOsd.start(vm.screenContext(), vm.screenWindow());
}
// The pill's surface joins the Android Auto layer's render order (on top of 1280) once LayerManager made it. Only
// VideoManager writes that order, and only from this thread, so a hide can never race a pill update.
static void syncVolOsdSurface(nbt::VideoManager& vm) {
    const int want = (g_volOsdOn && g_volumeOsd.surfaceReady() && !vm.overlayOrderFailed())
                         ? nbt::VolumeOsd::kSurfaceId : 0;
    if (want == vm.overlaySurface()) return;
    vm.setOverlaySurface(want);
    const bool now = vm.applyOverlayOrder();
    printf("[VolOsd] pill surface %d %s the Android Auto layer%s\n", nbt::VolumeOsd::kSurfaceId,
           want ? "added on top of 1280 in" : "taken out of", now ? "" : " (picture hidden: applied when it is shown)");
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
// ---- now playing (audio/media_state.hpp) -----------------------------------------------------------------------
// The phone's media playback channel (12) carries 0x8001 status about once a second and 0x8003 metadata per track. The
// merge lives in media_state.hpp; here it is published for the iDrive app as /dev/shmem/nbt-aad-media (a line record,
// written in place like the menu-entry state) and served on the control port:
//   "media"               -> the record, as a UDP reply (the app polls this while its Now Playing page is up)
//   "media next" / "prev" / "playpause" / "play" / "pause" -> the Android media key to the phone (input channel 7)
// The keys also come from the steering wheel (car/mfl.hpp, NBT_MFL_*). Both are only forwarded when the audio mode is not
// Bluetooth, where the phone already plays over A2DP and the car's own AVRCP handling owns those keys.
static const char* const kMediaFile = "/dev/shmem/nbt-aad-media";
static nbt::MediaState g_media;
static pthread_mutex_t g_mediaMutex = PTHREAD_MUTEX_INITIALIZER;
static bool g_mediaKeysToPhone = false;   // set at start-up from the audio mode
static bool g_wheelMediaKeys = false;     // "Media Controls for LCI Steering Wheels" (nbt_env.hpp lciMediaKeysEnabled())
static uint32_t g_mediaWriteMs = 0;       // monoMs() of the last record write

// Caller holds g_mediaMutex. One fixed record written over the old bytes before the file is shortened (QNX /dev/shmem
// has no rename, and the app must never read a half-written line).
static void writeMediaRecordLocked() {
    const std::string rec = nbt::mediaRecord(g_media);
    int fd = open(kMediaFile, O_WRONLY | O_CREAT, 0666);
    static bool s_warned = false;
    if (fd < 0) {
        if (!s_warned) { printf("[Media] cannot write %s: %s\n", kMediaFile, strerror(errno)); s_warned = true; }
        return;
    }
    fchmod(fd, 0666);
    ssize_t w = write(fd, rec.data(), rec.size());
    if (w == (ssize_t)rec.size()) ftruncate(fd, (off_t)rec.size());
    else if (!s_warned) { printf("[Media] short write to %s\n", kMediaFile); s_warned = true; }
    close(fd);
}

static void resetMedia(const char* why) {
    pthread_mutex_lock(&g_mediaMutex);
    g_media.clear();
    writeMediaRecordLocked();
    pthread_mutex_unlock(&g_mediaMutex);
    g_kombiMedia.update(std::string(), false);
    printf("[Media] cleared (%s)\n", why);
}

// Session thread, never blocks: merge, log a change, rewrite the record (at most 5 times a second).
static void onMediaStatus(uint16_t msg_id, const uint8_t* body, size_t len, void* user_data) {
    (void)user_data;
    pthread_mutex_lock(&g_mediaMutex);
    const bool changed = nbt::mediaMerge(g_media, msg_id, body, len);
    const uint32_t now = monoMs();
    g_media.updatedMs = now;
    g_kombiMedia.update(nbt::mediaClean(g_media.song), g_media.state == 2);   // 2 = playing
    if (changed) {
        printf("[Media] %s\n", nbt::mediaLine(g_media).c_str());
        if (g_mediaWriteMs == 0 || (uint32_t)(now - g_mediaWriteMs) >= 200) {
            g_mediaWriteMs = now;
            writeMediaRecordLocked();
        }
    }
    pthread_mutex_unlock(&g_mediaMutex);
}

// One press: a media key acts on the down edge, and the phone expects the release too.
static void sendMediaKey(uint32_t key, const char* what) {
    if (!g_mediaKeysToPhone) { printf("[Media] %s ignored: audio mode is Bluetooth\n", what); return; }
    if (!g_sessionActive) { printf("[Media] %s ignored: no Android Auto session\n", what); return; }
    printf("[Media] %s -> AA key %u\n", what, key);
    g_session.postKeyEvent(key, true);
    g_session.postKeyEvent(key, false);
}

static bool mediaCtlPrepare(const char* cmd, std::string& reply) {
    while (*cmd == ' ' || *cmd == '\n' || *cmd == '\r') ++cmd;
    char word[16];
    size_t n = 0;
    while (cmd[n] && cmd[n] != ' ' && cmd[n] != '\n' && cmd[n] != '\r' && n + 1 < sizeof(word)) { word[n] = cmd[n]; ++n; }
    word[n] = 0;
    if (!word[0]) {                                   // bare "media": the record as it stands (the app's poll)
        pthread_mutex_lock(&g_mediaMutex);
        reply = nbt::mediaRecord(g_media);
        pthread_mutex_unlock(&g_mediaMutex);
        return true;
    }
    if (!strcmp(word, "next")) sendMediaKey(nbt::AAK_MEDIA_NEXT, "next");
    else if (!strcmp(word, "prev")) sendMediaKey(nbt::AAK_MEDIA_PREVIOUS, "prev");
    else if (!strcmp(word, "playpause")) sendMediaKey(nbt::AAK_MEDIA_PLAY_PAUSE, "play/pause");
    else if (!strcmp(word, "play")) sendMediaKey(nbt::AAK_MEDIA_PLAY, "play");
    else if (!strcmp(word, "pause")) sendMediaKey(nbt::AAK_MEDIA_PAUSE, "pause");
    else printf("[Media] unknown command '%s'\n", word);
    return false;
}

// Shell test path: `echo next > /dev/shmem/nbt-aad-media.cmd` (the daemon removes the file). Same words as the control
// port, so the media keys can be tried from the unit's shell without the iDrive app or a mapped steering wheel.
static void* mediaMailThread(void*) {
    const char* path = "/dev/shmem/nbt-aad-media.cmd";
    while (g_running) {
        if (access(path, F_OK) == 0) {
            char cmd[64] = "";
            FILE* f = fopen(path, "r");
            if (f) { if (!fgets(cmd, sizeof(cmd), f)) cmd[0] = 0; fclose(f); }
            unlink(path);
            printf("[Media] command from %s: '%s'\n", path, cmd);
            std::string reply;
            mediaCtlPrepare(cmd, reply);
        }
        usleep(250000);
    }
    return NULL;
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

// The car's GPS fix (DSI callback thread, about 1 Hz) -> AA LOCATION sensor. Kept between sessions, age-checked at sending.
static void onCarLocation(const nbt::LocationFix& fix, void* user) {
    static_cast<nbt::AASession*>(user)->postCarLocation(fix);
}

// The car's satellite count (DSI callback thread) -> AA GPS_SATELLITE sensor. The session sends it on its own timer.
static void onCarSatellites(const nbt::SatelliteStatus& status, void* user) {
    static_cast<nbt::AASession*>(user)->postCarSatellites(status);
}

// The car's heading (DSI callback thread, about 1 Hz) -> AA COMPASS sensor. Kept between sessions.
static void onCarCompass(int32_t bearing_e6, void* user) {
    static_cast<nbt::AASession*>(user)->postCarCompass(bearing_e6);
}

// The car's wheel speed (DSI callback thread, about 2 Hz while moving) -> AA CAR_SPEED sensor.
static void onCarSpeed(const nbt::CarSpeedSample& speed, void* user) {
    static_cast<nbt::AASession*>(user)->postCarSpeed(speed);
}

// The car's gear when it changes (DSI callback thread, or the control thread from the iDrive app) -> AA GEAR sensor.
static void onCarGear(int aaGear, void* user) {
    static_cast<nbt::AASession*>(user)->postCarGear(aaGear);
}

// The car's parking brake when it changes (DSI callback thread) -> AA PARKING_BRAKE sensor.
static void onCarParkingBrake(bool on, void* user) {
    static_cast<nbt::AASession*>(user)->postCarParkingBrake(on);
}

static void onKnobEvent(const nbt::car::CcsEvent& ev, void* user) {
    nbt::AASession* s = static_cast<nbt::AASession*>(user);
    const bool back_hold = (ev.kind == 1 && ev.keycode == nbt::AAK_BACK && backHoldMs() > 0);
    if (back_hold && !ev.down) {
        int st = g_backState;
        g_backState = 0;
        if (st == 0) return;   // the press went to iDrive (it owned the screen): no hold timer ran, nothing to report
        if (ev.cancel) {       // the MOST link went away with BACK held: not a short press, so the phone gets nothing
            printf("[Knob] BACK hold cancelled: MOST link lost\n");
            return;
        }
        printf("[Knob] BACK released after %u ms (%s)\n", (unsigned)(monoMs() - g_backDownMs),
               st == 2 ? "hold already fired" : g_inputToPhone ? "short press -> phone" : "short press, iDrive owns the screen");
        if (st == 1 && g_inputToPhone) {        // short press: deliver it now
            g_keyToPhoneMs = monoMs() | 1u;       // iDrive popped back a state too: the iDrive app re-holds
            s->postKeyEvent(ev.keycode, true);
            s->postKeyEvent(ev.keycode, false);
        }
        return;                                   // st == 2: the hold already minimized AA, swallow the release
    }
    if (ev.what && (!strcmp(ev.what, "MFL_NEXT") || !strcmp(ev.what, "MFL_PREV") ||
                    !strcmp(ev.what, "MFL_PLAYPAUSE"))) {   // steering-wheel media keys (car/mfl.hpp)
        if (!g_wheelMediaKeys) {
            if (ev.down) printf("[Media] %s ignored: Media Controls for LCI Steering Wheels is off\n", ev.what);
            return;
        }
        if (ev.down) sendMediaKey(ev.keycode, ev.what);
        return;   // iDrive's copy of the key is its own business: nothing here acts on it in a non-Bluetooth mode
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
    if (ev.kind == 2) {
        g_knobInputMs = monoMs() | 1u;            // a turn: overlay 5, iDrive does nothing with it on the blank state
        s->postRelativeEvent(ev.keycode, ev.delta);
    } else {
        // Every key that reaches the phone: iDrive got the same key and may have left the blank state (BACK pops its
        // history, which after a MENU / hard-key re-hold is the iDrive main menu, not our menu: car 2026-10-29 short
        // BACK minimized Android Auto). Overlay 3 makes the iDrive app put it back instead of minimizing. Not the
        // knob's push: on the blank state iDrive does nothing with it (overlay 5, g_knobInputMs).
        if (ev.down) {
            if (ev.keycode == nbt::AAK_DPAD_CENTER) g_knobInputMs = monoMs() | 1u;
            else g_keyToPhoneMs = monoMs() | 1u;
        }
        s->postKeyEvent(ev.keycode, ev.down != 0);
    }
}

#define NBT_AAD_BUILD "v1.9.59-kombimedia"
// ---- log destination ------------------------------------------------------------------------------------------
// Default: no USB file; stdout (which the iDrive app sends to /dev/null) goes to the RAM log /dev/shmem/nbt-aad.log
// (5 MB cap, see stdoutToRamOrNull); from a shell it stays on the terminal / the user's redirect. NBT_LOG_FILE=<path> forces a file. The iDrive app's "Save logs to USB" checkbox (flag
// /var/opt/aa/usblog, usb_log.hpp) makes us append to <stick>/nbt-aad.log on the first writable /mnt/umass* stick;
// checked at start and every 5 s, so a stick plugged in later is picked up and a removed one is let go.
static bool g_logForced = false;
static char g_usbLogPath[192] = "";

static bool logPersistent() { return g_logForced || g_usbLogPath[0] != 0; }

// RAM log (v1.8.67, usb_log.hpp ramlog): with no USB / NBT_LOG_FILE log, stdout goes to /dev/shmem/nbt-aad.log
// (fresh per run, 5 MB cap; NBT_RAM_LOG=0 = off). Only replaces /dev/null: a shell run on a terminal or redirected to
// a file of the user's choosing keeps its stdout.
static bool g_ramLog = false;      // stdout is the RAM log now
static bool g_ramUsed = false;     // the RAM log was opened this run (later opens append)
static bool g_ramFull = false;     // cap reached: no more RAM logging this run

static bool ramLogWanted() { return nbt::envFlag("NBT_RAM_LOG") != 0 && !g_ramFull; }

static bool stdoutIsNull() {
    struct stat a, b;
    if (fstat(1, &a) != 0 || stat("/dev/null", &b) != 0) return false;
    return a.st_dev == b.st_dev && a.st_ino == b.st_ino;
}

static void stdoutToNull() {
    fflush(stdout);
    g_ramLog = false;
    if (!freopen("/dev/null", "a", stdout)) return;
    setvbuf(stdout, NULL, _IOLBF, 0);
    dup2(fileno(stdout), 2);
}

// /dev/null -> RAM log when wanted; otherwise /dev/null.
static void stdoutToRamOrNull() {
    if (!ramLogWanted()) { stdoutToNull(); return; }
    fflush(stdout);
    if (!freopen(nbt::ramlog::kDaemonPath, g_ramUsed ? "a" : "w", stdout)) { stdoutToNull(); return; }
    setvbuf(stdout, NULL, _IOLBF, 0);
    dup2(fileno(stdout), 2);
    g_ramLog = true;
    if (g_ramUsed) printf("[Log] back on the RAM log %s\n", nbt::ramlog::kDaemonPath);
    g_ramUsed = true;
}

// Main loop, every tick: stop the RAM log before it can reach 5 MB.
static void checkRamLogCap() {
    if (!g_ramLog) return;
    struct stat st;
    if (fstat(fileno(stdout), &st) != 0 || !nbt::ramlog::daemonMustStop((long)st.st_size)) return;
    fputs(nbt::ramlog::kFullLine, stdout);
    g_ramFull = true;
    stdoutToNull();
}

static void printHuVersion() {
    char v[96];
    nbt::huVersion(v, sizeof(v));
    printf("  HU software / nav version: %s\n", v);
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
        stdoutToRamOrNull();
        return;
    }
    char stick[128];
    if (!on || !nbt::usblog::findStick(stick, sizeof(stick))) return;
    char path[192];
    nbt::usblog::logPath(path, sizeof(path), stick, nbt::usblog::kDaemonName);
    if (isatty(1)) printf("[Log] Save logs to USB is on: logging to %s\n", path);
    fflush(stdout);
    if (g_ramLog) printf("[Log] USB stick found: logging to %s (the RAM log %s stops here)\n", path, nbt::ramlog::kDaemonPath);
    fflush(stdout);
    if (!freopen(path, "a", stdout)) { stdoutToRamOrNull(); return; }
    setvbuf(stdout, NULL, _IOLBF, 0);
    dup2(fileno(stdout), 2);
    g_ramLog = false;
    snprintf(g_usbLogPath, sizeof(g_usbLogPath), "%s", path);
    printf("---- nbt-aad log appended to %s (Save logs to USB, fsync every 2 s) ----\n", path);
    if (!atStart) {
        printf("  Build: " NBT_AAD_BUILD " (%s %s): USB stick found while running, earlier lines were not saved%s\n",
               __DATE__, __TIME__, g_ramUsed ? " here (see the RAM log)" : "");
        printHuVersion();
    }
}

int main(int argc, char* argv[]) {
    // pf_aa built in (src/pf_aa_embed.cpp): nbt-aad --pf-install | --pf-check | --pf-uninstall [--pf/--env/--backup f].
    // Before any of the daemon's start-up (log redirection, control port), so it never starts a second daemon.
    if (argc > 1 && !strncmp(argv[1], "--pf-", 5)) {
        static char mode[32];
        snprintf(mode, sizeof(mode), "--%s", argv[1] + 5);
        argv[1] = mode;
        int rc = nbt::pfaa::run(argc, argv);
        // Last line for the iDrive app: it cannot wait for us (the HMI ignores SIGCHLD), so it reads the result here.
        printf("pf_aa-exit: %d\n", rc);
        fflush(stdout);
        return rc;
    }
    if (argc > 1 && (!strcmp(argv[1], "--license") || !strcmp(argv[1], "--licence"))) {
        nbt::printNbtAadLicense();
        return 0;
    }
    // NBT_LOG_FILE=/mnt/umass01200t7/nbt-aad-live.log: log straight to that file (fsync'd every 2 s by the main
    // loop). Otherwise the "Save logs to USB" checkbox decides (serviceUsbLog).
    const char* log_file = getenv("NBT_LOG_FILE");
    if (log_file && *log_file) g_logForced = (freopen(log_file, "a", stdout) != NULL);
    else serviceUsbLog(true);
    if (!logPersistent() && stdoutIsNull()) stdoutToRamOrNull();   // started by the iDrive app with no USB log
    nbt::installProcessGuards();   // first: line-buffered log, SIGPIPE ignored, fatal-signal report (setvbuf must precede any output)
    if (g_logForced) printf("---- nbt-aad log appended to %s (fsync every 2 s) ----\n", log_file);
    printf("====================================================\n");
    printf("  NBT-AAD: Native Wireless Android Auto Daemon      \n");
    printf("  Platform: BMW NBT Evo ID6 / Toyota GR Supra (J29) \n");
    printf("  OS: QNX Neutrino RTOS (ARMv7LE)                   \n");
    printf("  Build: " NBT_AAD_BUILD " (%s %s)\n", __DATE__, __TIME__);
    printHuVersion();
    if (g_ramLog) printf("  Log: RAM %s (fresh per run, 5 MB cap; NBT_RAM_LOG=0 = off)\n", nbt::ramlog::kDaemonPath);
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

    // Vehicle + panel profile (NBT_VEHICLE / NBT_SCREEN, else the car's carplay.ini, else the display's own size for
    // the screen, else Toyota 1280x480).
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
        printf("[Car] phone canvas %dx%d shown 1:1 (NBT_VID_NATIVE): top-left %dx%d visible, MMR dst %dx%d, touch scale 1.000\n",
               nbt::videoCanvasWidth(), nbt::videoCanvasHeight(), nbt::canvasColsShown(), nbt::canvasRowsShown(), nbt::mmrDstWidth(), nbt::mmrDstHeight());
    else
        printf("[Car] phone canvas %dx%d scaled to fill the panel: top-left %dx%d visible, MMR dst %dx%d, touch scale %.3f\n",
               nbt::videoCanvasWidth(), nbt::videoCanvasHeight(), nbt::canvasColsShown(), nbt::canvasRowsShown(), nbt::mmrDstWidth(), nbt::mmrDstHeight(), nbt::touchScale());

    // 1. Initialize Video Pipeline (libscreen.so window at the panel size)
    nbt::VideoManager video;
    if (!video.init((uint32_t)car.screenW,
                    (uint32_t)car.screenH,
                    nbt::videoDecoderFps())) {
        printf("[Main] Warning: Video initialization encountered an issue.\n");
    }
    g_video = &video;

    // 2. Initialize the audio mode (audio/backend.hpp): the only place main depends on which one it is.
    nbt::A2iAudio a2i;
    nbt::BrowserAudio browser;
    nbt::BtAudio bt;
    const nbt::AudioMode audio_mode = nbt::audioMode();
    {
        const char* am = getenv("NBT_AUDIO_MODE");
        const bool set = (am && *am) || nbt::envFlag("NBT_AUDIO_BT") >= 0;
        printf("[Main] audio mode: %s (NBT_AUDIO_MODE=bt|browser|a2i%s)\n", nbt::audioModeName(audio_mode),
               set ? " from nbt-aad.env" : nbt::nativeAudioFlagSet() ? "; Use Native Audio instead of Bluetooth is on"
                                                                     : "; Use Native Audio instead of Bluetooth is off");
    }
    // Steering-wheel / control-port media keys go to the phone only when its media does not come over A2DP: in Bluetooth
    // mode the car's own AVRCP handling already owns those keys (audio/media_state.hpp, car/mfl.hpp).
    g_mediaKeysToPhone = audio_mode != nbt::AUDIO_MODE_BT;
    g_wheelMediaKeys = nbt::lciMediaKeysEnabled();
    printf("[Media] steering-wheel media keys: %s\n",
           g_wheelMediaKeys ? "on (Media Controls for LCI Steering Wheels)"
                            : "off (tick Media Controls for LCI Steering Wheels, or NBT_LCI_MEDIA=1)");
    printf("[CarPlay] CarPlay compatibility: %s - yield when CarPlay takes the screen: %s, factory CarPlay decoder "
           "context: %s, CarPlay window after a hand-over: %s\n",
           nbt::cpCompatEnabled() ? "on" : "off (tick CarPlay compatibility, or NBT_CP_COMPAT=1)",
           nbt::cpYieldOn() ? "on" : "off", nbt::cpPurgeOn() ? "destroyed (as before)" : "detached, re-attached afterwards",
           nbt::cpWinGroupOn() ? "in our window group (as before)" : "top-level");
    printf("[Media] now playing on %s, media keys to the phone: %s\n", kMediaFile,
           g_mediaKeysToPhone ? "on (audio mode is not Bluetooth)" : "off (audio mode is Bluetooth)");
    if (audio_mode == nbt::AUDIO_MODE_BROWSER) {
        // Media sink (ch 9) and GUIDANCE sink (ch 8) stay in the ServiceDiscovery and a SYSTEM_AUDIO sink (ch 16) is
        // added, so the phone sends all three streams here instead of to A2DP. The A2I pipeline is not opened; the
        // mic runs standalone like in Bluetooth mode.
        const nbt::BrowserConfig browser_cfg = nbt::readBrowserConfig();   // audio/config.hpp
        if (!browser.init(browser_cfg)) printf("[Browser] WARNING: the libasound shim is missing: AA audio will be silent\n");
        // Deferred acks (NBT_AA_DEFER_ACKS, default on): an AA audio frame is acked once the mixer has taken it, so
        // the phone is paced by the car's clock instead of running ahead on its own.
        const bool defer = browser_cfg.defer_acks;
        if (defer) {
            browser.setConsumedCallback(&onAudioConsumed, NULL);
            browser.setAckWindow(nbt::AudioAcks::window());   // the drift loop's target must cover it
        }
        g_session.setDeferredAudioAcks(defer);
        printf("[Browser] audio acks: %s (NBT_AA_DEFER_ACKS)\n", defer ? "once played (deferred)" : "on arrival");
        // Underflow reports (NBT_AA_UNDERFLOW_REPORT, default on): tell the phone when a stream ran dry (0x800b).
        const bool underflow = browser_cfg.underflow_reports;
        if (underflow) browser.setUnderflowCallback(&onAudioUnderflow, NULL);
        g_session.setUnderflowReports(underflow);
        printf("[Browser] underflow reports to the phone: %s (NBT_AA_UNDERFLOW_REPORT)\n", underflow ? "on" : "off");
        g_browserAudio = &browser;
        g_audioBackend = &browser;
    } else if (audio_mode == nbt::AUDIO_MODE_BT) {
        bt.init(nbt::readBtConfig());   // the car's Bluetooth source, NBT_BT_PLAY, NBT_SPEECH
        g_audioBackend = &bt;
    } else {
        if (!a2i.init()) {
            printf("[Main] Warning: Audio initialization encountered an issue.\n");
        }
        g_audioBackend = &a2i;
    }

    // 3. Input: the iDrive controller (ZBE) is read observe-only over MOSTSubSystem.DSYSMOSTServer by
    //    g_most and g_ccs (started below); iDrive keeps receiving every knob event itself.

    if (!dropIdrEnabled())
        printf("[Main] NBT_DROP_IDR=0: no keyframe request after a dropped video frame\n");
    // 3b. Volume overlay (NBT_VOL_OSD=1). Off by default: it creates an extra Screen window, so it is opt-in until the
    //     compositing is confirmed on the car. It mirrors the car's own volume (AudioMaster 0x20/0x0C08); there is no
    //     synthetic sweep and no env var for one.
    if (volOsdEnabled()) {
        printf("[VolOsd] will attach to the Android Auto window as a child when a phone connects\n");
    } else {
        printf("[VolOsd] off (NBT_VOL_OSD not set): the car's volume is decoded but nothing is drawn\n");
    }

    // Microphone upload is not wired into AA yet; leave callback unset so the
    // A2I uplink reader does not flood slog while debugging video bring-up.

    // Start all subsystems
    video.start();
    g_audioBackend->start();   // A2I mode: the playback workers

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
    g_session.setNavStatusCallback(&nbt::kombinav::KombiNav::navStatusCallback, &g_kombiNav);
    g_session.setMediaStatusCallback(&onMediaStatus, NULL);

    if (!tcp_server.start()) {
        printf("[Main] Fatal: Failed to start TCP server on port %u\n", (unsigned)g_aaPort);
        return 1;
    }
    g_tcp = &tcp_server;
    g_kombiNav.start(&g_most);     // idle until "Turn arrows in cluster/HUD" (or NBT_KOMBI_NAV=1) is on; before
                                   // g_most starts: NBT_CLUSTER=6wa attaches to it
    g_kombiMedia.start(audio_mode != nbt::AUDIO_MODE_BT);   // idle until the phone plays something
    {
        pthread_t t;
        if (pthread_create(&t, NULL, &mediaMailThread, NULL) == 0) pthread_detach(t);
    }
    nbt::g_fatal_hook = &nbt::kombinav::KombiNav::onFatalSignal;

    // 4b. Android Auto over USB (NBT_USB=1, experimental). Same user data as the TCP server, so its "session
    // active" state also covers a USB phone and the auto-connector stays quiet; startSession() picks the owner.
    nbt::UsbAccessory usb(&onUsbConnected, &tcp_server, &sessionOwner, NULL);
    if (nbt::usbEnabled()) {
        const char* usbEnv = getenv("NBT_USB");
        printf("[Main] Android Auto over USB: on (%s), through %s\n", usbEnv && *usbEnv ? "NBT_USB" : "Settings checkbox",
               nbt::usbDirectEnabled() ? "io-usb" : "/dev/oap* (NBT_USB_DIRECT=0)");
        if (usb.start()) g_usb = &usb;
        else printf("[Main] Android Auto over USB failed to start.\n");
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
        if (!g_ccs.attach(g_most, nbt::car::readCcsConfig(), &onKnobEvent, &g_session))
            printf("[Main] iDrive knob input failed to start.\n");
    } else {
        printf("[Main] iDrive knob input off (NBT_KNOB=0).\n");
    }
    // Driver input for the screen hand-over (overlay 4): attached whether or not the knob drives the phone.
    if (!g_carInput.attach(g_most, nbt::car::readCcsConfig(), &onCarInput, NULL))
        printf("[Main] driver input tracking failed to start: the iDrive app treats every iDrive screen as the car's.\n");
    // The car's volume is followed whether or not the knob is: it drives the overlay, not the phone.
    if (!g_carVolume.attach(g_most, &onCarVolume, NULL))
        printf("[Main] car volume input failed to start.\n");
    if (nbt::phoneGpsEnabled()) {
        const char* pg = getenv("NBT_PHONE_GPS");
        printf("[Main] Use Phone GPS on (%s): the car's GPS and heading are not read; the phone uses its own GPS, "
               "compass and motion sensors.\n", pg && *pg ? "NBT_PHONE_GPS" : "Settings checkbox");
    } else if (!nbt::carGpsEnabled())
        printf("[Main] car GPS -> Android Auto off (NBT_CAR_GPS=0).\n");
    else if (!g_gps.attach(g_most, nbt::car::readGpsAccuracyOverrideE3(), &onCarLocation, &onCarSatellites, &onCarCompass,
                           &g_session))
        printf("[Main] car GPS failed to start.\n");
    nbt::car::CarVehicleCallbacks vehicle;
    vehicle.user = &g_session;
    if (nbt::carSpeedEnabled()) vehicle.speed = &onCarSpeed;
    else printf("[Main] car speed -> Android Auto off (NBT_CAR_SPEED=0).\n");
    if (nbt::carGearEnabled()) vehicle.gear = &onCarGear;
    else printf("[Main] car gear -> Android Auto off (NBT_CAR_GEAR=0).\n");
    if (nbt::carParkingBrakeEnabled()) vehicle.parkingBrake = &onCarParkingBrake;
    else printf("[Main] car parking brake -> Android Auto off (NBT_CAR_PARKING_BRAKE=0).\n");
    if (!g_vehicle.attach(g_most, vehicle))
        printf("[Main] car speed, gear and parking brake failed to start.\n");
    // NBT_CCS_DEBUG / NBT_CCS_ALL were the control-message loggers of the old ccs_input.cpp; the MOST client has one
    // logger for everything it sees now, so they turn that on.
    const bool ccsLogCompat = nbt::envFlag("NBT_CCS_DEBUG") == 1 || nbt::envFlag("NBT_CCS_ALL") == 1;
    if (ccsLogCompat && nbt::envFlag("NBT_MOST_LOG") != 1)
        printf("[Main] NBT_CCS_DEBUG / NBT_CCS_ALL are replaced by NBT_MOST_LOG=1: turning that on.\n");
    if ((nbt::envFlag("NBT_MOST_LOG") == 1 || ccsLogCompat) && !g_mostTrace.attach(g_most))
        printf("[Main] NBT_MOST_LOG: could not attach to the MOST client.\n");
    g_most.setRawLog(getenv("NBT_KNOB_RAW") != NULL);
    if (!g_most.start("NBTAAD_MOST"))
        printf("[Main] MOST client failed to start: knob/keys, car GPS, car speed and parking brake disabled.\n");

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
    bool was_camera = false;
    bool camera_dismissed = false; // iDrive Android Auto entry chosen over a camera view: AA until that view closes
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
    const bool mic_on = nbt::micEnabled();
    nbt::MicUplink mic(&onMicData, NULL);   // the standalone uplink (every mode but A2I, which has its own)
    bool mic_live = false;
    unsigned log_sync_ticks = 0;
    printf("[Mic] car microphone for the Assistant: %s\n", mic_on ? "on" : "off (NBT_MIC=0 or /opt/aa/mic.disable)");
    g_audioBackend->setMicCallback(&onMicData, NULL);
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
    int cp_1280_last = -1;       // surface 1280's visibility at the last CarPlay check while Android Auto was minimized
    uint32_t most_health_ms = monoMs();
    while (g_running) {
        for (int w = 0; w < 5 && !g_mainWake && g_running && !driveModePeekDue(); ++w)
            usleep(50000);   // 250 ms tick, cut short on a wake or when a driving-mode step-aside is due
        g_mainWake = 0;
        if (monoMs() - most_health_ms >= 60000u) {
            most_health_ms = monoMs();
            g_most.logHealth();
            g_kombiNav.logHealth();
        }
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
            uint32_t ci = g_carInputMs;
            bool inputRecent = ci && (uint32_t)(monoMs() - ci) < 3000u;   // the driver touched a car control
            if (ci && !inputRecent) g_carInputMs = 0;
            uint32_t ki = g_knobInputMs;
            bool knobRecent = ki && (uint32_t)(monoMs() - ki) < 3000u;    // only the knob turned / pushed
            if (ki && !knobRecent) g_knobInputMs = 0;
            int ov = g_inputToPhone
                         ? (g_backState == 1 ? 2 : (menuRecent ? 3 : (inputRecent ? 4 : (knobRecent ? 5 : 1)))) : 0;
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
        // Bluetooth: re-request the car's BT source for the reply (NBT_BT_REFOCUS) and snapshot the car's audio
        // connections. Browser: ask for the browser source again in case the voice scenario moved the car off it.
        if (mr == 1 && act) g_audioBackend->onMicOpened();
        if (mr == 2 && act) g_audioBackend->onMicClosed();
        if (!act && voice.active()) voice.deactivate("session ended");
        if (mr == 1 && mic_on && act && !mic_live) {
            bool ok = g_audioBackend->startMic(mic);
            printf("[Mic] phone opened the mic: capture %s (kept open until the session ends)\n", ok ? "started" : "FAILED");
            mic_live = ok;
        } else if (mic_ev) {
            printf("[Mic] phone %s the mic (capture %s)\n", mr == 1 ? "opened" : "closed", mic_live ? "running" : "not running");
        }
        if (mic_live && !act) {
            g_audioBackend->stopMic(mic);
            printf("[Mic] capture stopped (session ended)\n");
            mic_live = false;
        }
        ++log_sync_ticks;
        if (log_sync_ticks == 2400 && unlink(kBootGuardFile) == 0)   // 10 min up: not part of a bootloop
            printf("[Main] running for 10 min: autostart bootloop guard cleared\n");
        if ((log_sync_ticks % 20) == 0) serviceUsbLog(false);   // every 5 s: stick plugged in / removed, checkbox
        checkRamLogCap();                                         // every tick: RAM log 5 MB cap
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
            if (was_camera) { camera_dismissed = true; printf("[Camera] Android Auto chosen over the camera view: kept until it closes\n"); }
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
        const bool camera_raw = g_camera > 0;
        if (!camera_raw) camera_dismissed = false;
        const bool camera = camera_raw && !camera_dismissed;
        if (camera != was_camera) {
            printf("[Camera] %s\n", camera ? "iDrive camera view up: overlay down for the camera"
                                            : "camera view closed: restoring");
            if (reverse_focus && act && !g_userExit) g_session.postVideoFocus(!camera && !park && !reverse);
            was_camera = camera;
        }
        // CarPlay taking the screen while an Android Auto session is still open (iPhone joins while the Android phone
        // stays connected, MINI tester 2026-10-29): CarPlay plays through our decoder context, so without its factory
        // settings it opens zoomed (the hand-off retune used to run only after the AA session ended). While Android
        // Auto is minimized we keep surface 1280 hidden; when it turns visible anyway, iDrive is showing CarPlay on it.
        // On Android Auto's own surface (v1.9.20) we never hide 1280, and iDrive may leave it visible after a CarPlay
        // session: only a change from hidden to visible while Android Auto is minimized counts there.
        // NBT_CP_LIVE_RETUNE=0 disables. Not car-verified.
        bool cp_shows = false;
        if (overlay_on || !act) cp_1280_last = -1;
        else if (cpLiveRetune() && video.hasWindow() && (++cp_live_ticks % 8) == 0 && !video.decoderInCarPlayMode()) {
            int v = nbt::VideoManager::surface1280Visible();
            cp_shows = v == 1 && (nbt::VideoManager::sharesCarPlaySurface() || cp_1280_last == 0);
            cp_1280_last = v;
        }
        if (cp_shows && cpYieldEnabled()) {
            printf("[Main] CarPlay took the screen while Android Auto is still connected: Android Auto yields to CarPlay "
                   "completely (session ended, decoder context and windows released, nbt-aad stops; the Android Auto "
                   "entry starts it again; CarPlay compatibility / NBT_CP_YIELD)\n");
            g_userExit = 1;
            g_carExit = 1;                               // nothing may cover CarPlay on the way out
            g_session.postVideoFocus(false);             // phone stops streaming at once
            g_cpYielded = 1;
            g_running = 0;                               // the clean shutdown below releases everything
        } else if (cp_shows) {
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
        ensureVolOsd(video);   // the pill reads the panel size from the projection window
        syncVolOsdSurface(video);
        video.pollScreenEvents();   // mm-renderer's window joining our group (logged; used by NBT_MMR_WIN_HIDE=1)
        video.serviceAaSurface();   // Android Auto's own LayerManager surface: shown once LayerManager has made it
        // Decoder rebuilt on the way back: frames are held until the phone's keyframe. None within 2 s: cycle the video
        // focus (UNFOCUSED, FOCUSED a tick later), twice; after 6 s pass frames on rather than stay black.
        if (g_idrWaitMs) {
            uint32_t w = monoMs() - g_idrWaitMs;
            if (!video.waitingForKeyframe()) {
                printf("[Main] picture back after the decoder rebuild (%u ms)\n", w);
                g_idrWaitMs = 0;
            } else if (!act || w >= 6000u) {
                video.releaseKeyframeWait();
                g_idrWaitMs = 0;
            } else if (g_idrAsks < 4 && w >= (uint32_t)(2000u + 2000u * (g_idrAsks / 2) + 500u * (g_idrAsks & 1))) {
                // off 2.0 s, on 2.5 s, off 4.0 s, on 4.5 s: always back on FOCUSED before the 6 s release
                bool focus = (g_idrAsks & 1) != 0;
                if (!focus) printf("[Main] no keyframe %u ms after the decoder rebuild: asking the phone again (video focus off/on)\n", w);
                g_session.postVideoFocus(focus);
                ++g_idrAsks;
            }
        }
        bool want = (hide_idle ? act : true) && !g_userExit && !reverse && !park && !camera && !dm_peek;
        if (want && act && !overlay_on && (video.decoderInCarPlayMode() || video.decoderParked())) {
            printf("[Main] back to Android Auto after %s: rebuilding the decoder context\n",
                   video.decoderParked() ? "iDrive (overlay had surface 1280)" : "CarPlay");
            if (video.decoderParked()) { g_idrWaitMs = monoMs() | 1u; g_idrAsks = 0; }
            video.prepareForSession();
            g_session.postVideoFocus(true);
        }
        // A video frame was lost (queue full, or mdev did not take it): everything up to the phone's next keyframe
        // decodes against the missing one. Ask for a keyframe the way the decoder rebuild above does (video focus off,
        // on again 500 ms later): at most once in 10 s, not in a session's first 5 s, not while iDrive has the screen.
        {
            const bool settled = act && overlay_on && !g_idrWaitMs && upSec() - g_sessionStartS >= 5.0;
            const nbt::KeyframeAsk::Action ka =
                g_keyframeAsk.step(monoMs(), dropIdrEnabled() && video.keyframeWanted(), want && settled, want && act, act);
            if (ka == nbt::KeyframeAsk::kFocusOff) {
                printf("[Main] [%9.3f] a video frame was dropped: asking the phone for a keyframe (video focus off/on)\n",
                       upSec());
                video.clearKeyframeWanted();
                g_session.postVideoFocus(false);
            } else if (ka == nbt::KeyframeAsk::kFocusOn) {
                g_session.postVideoFocus(true);
            }
        }
        g_inputToPhone = (want && act) ? 1 : 0;
        nbt::setTouchOwned(g_inputToPhone != 0);      // UDP 30567 goes back to the HMI whenever iDrive has the screen
        if (want != overlay_on || (force_show && want)) {
            // The pill's surface 1281 leaves with the picture (the hide order holds neither), so the decoder stays
            // open: no close/rebuild, no keyframe wait, no grey picture (v1.9.15-v1.9.17 needed those only because the
            // pill was surface 1280). setShown: no pill drawn while iDrive has the screen.
            video.setVisible(want);
            g_volumeOsd.setShown(want);
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
    g_audioBackend->cancelPending();   // no queued audioconn starts while we tear down
    video.setVisible(false);   // give iDrive back first: any of the stop() calls below may block
    g_volumeOsd.setShown(false);
    printf("[Main] [+%.2f s] overlay hidden\n", upSec() - t0);
    if (mic_live) g_audioBackend->stopMic(mic);
    voice.deactivate("shutdown");   // never leave the car mic scenario active behind us
    g_kombiMedia.stop();
    g_kombiNav.stop();              // clears a cluster/HUD arrow (one quick attempt)
    printf("[Main] [+%.2f s] cluster/HUD arrows stopped\n", upSec() - t0);
    tcp_server.stop();         // no more onClientConnected -> no session can start behind our back
    printf("[Main] [+%.2f s] listener stopped\n", upSec() - t0);
    // Before usb.stop(), which takes the link away. A phone that isn't told keeps streaming into the void and ignores
    // the next daemon's session. Wireless phones just reconnect, so they are not told to quit - except when yielding to
    // CarPlay: then the Android phone is told the session is over, so it does not keep trying for the screen.
    if (g_owner.get() == nbt::kTransportUsb || (g_cpYielded && g_session.isActive())) {
        const bool answered = g_session.byeAndWait(kUsbByeWaitMs);
        printf("[Main] [+%.2f s] %s session: %s\n", upSec() - t0, g_owner.get() == nbt::kTransportUsb ? "USB" : "wireless",
               answered ? "ended by the phone" : "no answer to the ByeByeRequest");
    }
    g_usb = NULL;              // the wireless thread stops asking it
    usb.stop();                // no new USB session either; ends the USB bridge (and with it a USB session)
    printf("[Main] [+%.2f s] USB accessory stopped\n", upSec() - t0);
    g_session.stop();
    printf("[Main] [+%.2f s] session stopped\n", upSec() - t0);
    g_video = NULL;
    nbt::AudioBackend* audio_backend = g_audioBackend;
    g_audioBackend = NULL;
    audio_backend->stop();
    video.stop();
    printf("[Main] [+%.2f s] video stopped\n", upSec() - t0);

    if (g_ctlFd >= 0) close(g_ctlFd);
    unlink(kUiStateFile);   // plugin falls back to the plain "Android Auto" entry
    unlink(kBootGuardFile); // clean exit: the iDrive app's autostart bootloop guard starts from zero
    printf("[Main] [%9.3f] nbt-aad shutdown complete (%.2f s)%s.\n", upSec(), upSec() - t0,
           g_cpYielded ? ": yielded to CarPlay (decoder context, windows and window group released)" : "");
    fflush(stdout);
    // Leave without running static destructors: detached threads (DSI sender loops, wireless bring-up) may still
    // be running, and tearing globals down under them crashed at exit (nbt-aad-21: SIGSEGV after "shutdown complete").
    _exit(0);
}
