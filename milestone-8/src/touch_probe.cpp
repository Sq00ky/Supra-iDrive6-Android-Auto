// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "touch_probe.hpp"
#include "touch_frame.hpp"
#include "car_config.hpp"
#include "gesture_pdu.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>

namespace nbt {
namespace {
struct Args { uint16_t port; int seconds; };

void* probeMain(void* p) {
    Args a = *static_cast<Args*>(p);
    delete static_cast<Args*>(p);
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) { printf("[TouchProbe] socket failed: %s\n", strerror(errno)); fflush(stdout); return NULL; }
    int one = 1;
    if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) != 0)
        printf("[TouchProbe] SO_REUSEADDR failed: %s\n", strerror(errno));
#ifdef SO_REUSEPORT
    if (setsockopt(s, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one)) != 0)
        printf("[TouchProbe] SO_REUSEPORT failed: %s\n", strerror(errno));
#endif
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(a.port);
    if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        printf("[TouchProbe] bind UDP %u FAILED: %s (HMI owns it without SO_REUSEADDR?)\n", a.port, strerror(errno));
        fflush(stdout);
        close(s);
        return NULL;
    }
    printf("[TouchProbe] bound UDP %u, listening %d s. Touch the screen now.\n", a.port, a.seconds);
    fflush(stdout);
    struct timeval t0;
    gettimeofday(&t0, NULL);
    int frames = 0, decoded = 0;
    TouchReassembler reasm;
    for (;;) {
        fd_set rf; FD_ZERO(&rf); FD_SET(s, &rf);
        struct timeval tv; tv.tv_sec = 1; tv.tv_usec = 0;
        int r = select(s + 1, &rf, NULL, NULL, &tv);
        struct timeval now; gettimeofday(&now, NULL);
        if ((long)(now.tv_sec - t0.tv_sec) >= (long)a.seconds) break;
        if (r <= 0) continue;
        unsigned char buf[2048];
        sockaddr_in from; socklen_t fl = sizeof(from);
        ssize_t n = recvfrom(s, buf, sizeof(buf), 0, reinterpret_cast<sockaddr*>(&from), &fl);
        if (n <= 0) continue;
        ++frames;
        if (frames <= 200 || (frames % 100) == 0) {
            char ip[32];
            inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));
            printf("[TouchProbe] #%d from %s:%u len=%d:", frames, ip, (unsigned)ntohs(from.sin_port), (int)n);
            for (ssize_t i = 0; i < n && i < 96; ++i) printf(" %02x", buf[i]);
            printf("\n");
            fflush(stdout);
        }
        TouchMsg msgs[kTouchMaxMsgs];
        const int nm = reasm.feed(buf, (size_t)n, msgs, kTouchMaxMsgs);
        for (int mi = 0; mi < nm && decoded < 400; ++mi) {
            TouchContact c[kTouchMaxContacts];
            int declared = 0;
            const int nc = parseTouchStream(msgs[mi], c, kTouchMaxContacts, &declared);
            if (nc <= 0) continue;
            ++decoded;
            printf("[TouchProbe] decoded (%d records):", msgs[mi].recs);
            for (int k = 0; k < nc; ++k) printf(" [id %d st %d %d,%d sz %d]", c[k].id, c[k].state, c[k].x, c[k].y, c[k].size);
            printf("\n");
        }
    }
    printf("[TouchProbe] done, %d frames\n", frames);
    fflush(stdout);
    close(s);
    return NULL;
}
}

namespace {
struct FwdArgs { uint16_t port; TouchEventCb cb; void* user; };

double envNum(const char* name, double def) {
    const char* v = getenv(name);
    return (v && *v) ? atof(v) : def;
}

int clampi(double v, int lo, int hi) {
    int i = (int)(v < 0 ? v - 0.5 : v + 0.5);
    return i < lo ? lo : (i > hi ? hi : i);
}

// e = every active finger; e.index = position of finger 'which' (or 0).
void fillActive(TouchEventOut& e, const bool* active, const int* cx, const int* cy, int ns, int which) {
    e.n = 0; e.index = 0; e.action = 2;
    for (int j = 0; j < ns && e.n < kTouchMaxContacts; ++j) if (active[j]) {
        if (j == which) e.index = e.n;
        e.p[e.n].id = j; e.p[e.n].x = cx[j]; e.p[e.n].y = cy[j]; ++e.n;
    }
}

// RELEASE (last finger) or POINTER_UP (others stay) for finger k at (x,y); clears active[k].
void emitUp(TouchEventOut& e, bool* active, int* cx, int* cy, int ns, int k, int x, int y) {
    cx[k] = x; cy[k] = y;
    int cnt = 0;
    for (int j = 0; j < ns; ++j) if (active[j]) ++cnt;
    fillActive(e, active, cx, cy, ns, k);
    e.action = (cnt <= 1) ? 1 : 6;
    active[k] = false;
}

volatile int g_touchOwned = 0;

// ---- gesture camera PDUs on the touch port --------------------------------------------------------------------------
// UDP 30567 is not touch-only. HmiMain3D's CTouchDataClient (processSocketStream FUN_028dbe30) dispatches on the u32
// ident of every PDU: 0xB0048002 = touch (ours), 0xB5098004 / 0xB5308005 = gesture camera gesture id (FUN_028dba84 ->
// forwardGestureCameraGestureSync -> CFreespaceGestureHandler::handleGesture, ids in gesture_pdu.hpp), 0xB50A8003 / 0xB5098003 / 0xB5308003 / 0xB5308006 = hand
// coordinates, 0xB509800x / 0xB530800x 1-2 = camera status. While the forwarder holds the port, io-pkt delivers every
// unicast datagram to our socket only, so up to v1.8.42 iDrive never saw a gesture while Android Auto was on screen
// (user report 2026-10-29: circle-for-volume and swipe-to-skip dead under Android Auto).
// Fix (default since v1.8.47, NBT_TOUCH_CONNECT=0 turns it off; car-verified on a G11, 2026-10-29: gestures work
// under Android Auto): connect() our socket to the touch controller (source of the first touch PDU, or
// NBT_TOUCH_PEER=ip:port). io-pkt then matches only that peer's datagrams to our socket (exact 4-tuple lookup) and
// hands everything else to the HMI's wildcard socket: the camera's gestures and status PDUs, and its answers to the
// HMI's configureGestures requests (sent from that socket to the camera's UDP 30501). The HMI's own gates
// (iva_3DGESTURES coding, power state on, input manager type 5, which configureGestures itself sets) have nothing
// that depends on Android Auto, so nothing else is needed.
bool isTouchPdu(const unsigned char* b, size_t n) {
    return n >= 4 && b[0] == 0xB0 && b[1] == 0x04 && b[2] == 0x80 && b[3] == 0x02;
}

bool parsePeer(const char* e, sockaddr_in* out) {
    if (!e || !*e) return false;
    char ip[32];
    const char* c = strchr(e, ':');
    size_t l = c ? (size_t)(c - e) : strlen(e);
    if (l == 0 || l >= sizeof(ip)) return false;
    memcpy(ip, e, l);
    ip[l] = 0;
    memset(out, 0, sizeof(*out));
    out->sin_family = AF_INET;
    if (inet_aton(ip, &out->sin_addr) == 0) return false;
    out->sin_port = htons(c ? (uint16_t)strtoul(c + 1, NULL, 10) : (uint16_t)30501);
    return true;
}

// The touch controller's address, learned from a real touch PDU, is kept here so the next daemon start connects at
// once (before it, gesture camera PDUs are still swallowed until the screen is touched).
const char* const kTouchPeerFile = "/var/opt/aa/touch-peer";

bool loadPeerFile(sockaddr_in* out) {
    FILE* f = fopen(kTouchPeerFile, "r");
    if (!f) return false;
    char line[64] = "";
    bool ok = fgets(line, sizeof(line), f) != NULL;
    fclose(f);
    if (!ok) return false;
    size_t l = strlen(line);
    while (l && (line[l - 1] == '\n' || line[l - 1] == '\r' || line[l - 1] == ' ')) line[--l] = 0;
    return parsePeer(line, out);
}

void savePeerFile(const sockaddr_in& p) {
    mkdir("/var/opt/aa", 0755);
    FILE* f = fopen(kTouchPeerFile, "w");
    if (!f) { printf("[TouchFwd] cannot write %s (%s)\n", kTouchPeerFile, strerror(errno)); return; }
    fprintf(f, "%s:%u\n", inet_ntoa(p.sin_addr), (unsigned)ntohs(p.sin_port));
    fclose(f);
}

bool connectToPeer(int s, const sockaddr_in& peer, const char* why) {
    char ip[32];
    snprintf(ip, sizeof(ip), "%s", inet_ntoa(peer.sin_addr));
    if (connect(s, reinterpret_cast<const sockaddr*>(&peer), sizeof(peer)) != 0) {
        printf("[TouchFwd] connect to the touch controller %s:%u FAILED (%s): iDrive keeps missing gesture camera "
               "PDUs\n", ip, (unsigned)ntohs(peer.sin_port), strerror(errno));
        return false;
    }
    sockaddr_in me;
    socklen_t ml = sizeof(me);
    memset(&me, 0, sizeof(me));
    getsockname(s, reinterpret_cast<sockaddr*>(&me), &ml);
    printf("[TouchFwd] socket connected to the touch controller %s:%u (%s; local %s:%u): only its touch PDUs come "
           "to us now, the gesture camera's go to iDrive\n", ip, (unsigned)ntohs(peer.sin_port), why,
           inet_ntoa(me.sin_addr), (unsigned)ntohs(me.sin_port));
    return true;
}

// Binds the touch port (after the HMI's socket, so the kernel delivers to us). -1 on failure.
int bindTouchPort(uint16_t port) {
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) { printf("[TouchFwd] socket failed: %s\n", strerror(errno)); return -1; }
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
#ifdef SO_REUSEPORT
    setsockopt(s, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one));
#endif
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        printf("[TouchFwd] bind UDP %u FAILED: %s\n", port, strerror(errno));
        close(s);
        return -1;
    }
    return s;
}

void* forwardMain(void* p) {
    FwdArgs a = *static_cast<FwdArgs*>(p);
    delete static_cast<FwdArgs*>(p);
    int s = -1;
    int bindFails = 0;
    // Panel pixels -> the phone's 1280x720 canvas: the canvas is shown scaled to the panel width
    // (1.0 on the 1280x480 panel, 0.889 on a 1440x540 one; 1.0 with NBT_VID_NATIVE=1). NBT_TOUCH_SX/SY override.
    const double panel = touchScale();
    const double sx = envNum("NBT_TOUCH_SX", panel), sy = envNum("NBT_TOUCH_SY", panel);
    const double oxDef = defaultTouchOffsetX(carConfig());
    const double ox = envNum("NBT_TOUCH_OX", oxDef), oy = envNum("NBT_TOUCH_OY", 0.0);
    if (oxDef != 0.0 && !getenv("NBT_TOUCH_OX"))
        printf("[TouchFwd] MINI round display: touch x offset %.0f by default (NBT_TOUCH_OX overrides)\n", oxDef);
    const int maxX = (int)envNum("NBT_TOUCH_W", 1280) - 1, maxY = (int)envNum("NBT_TOUCH_H", 720) - 1;
    printf("[TouchFwd] UDP %u, forwarding decoded multi-touch while Android Auto is on screen (scale %.3f/%.3f offset %.0f/%.0f range 0..%d x 0..%d)\n",
           a.port, sx, sy, ox, oy, maxX, maxY);

    // Slots are indexed by the HMI's finger id (0..kTouchMaxContacts-1); the id is also the AA pointer_id.
    enum { NS = kTouchMaxContacts, MAXEV = 16 };
    bool active[NS];
    int cx[NS], cy[NS];        // last calibrated position per finger id
    for (int j = 0; j < NS; ++j) { active[j] = false; cx[j] = cy[j] = 0; }
    int logged = 0, loggedMoves = 0, rawLogged = 0, multiFrames = 0;
    TouchReassembler reasm;
    bool layoutLogged = false;
    const bool connectPeer = envNum("NBT_TOUCH_CONNECT", 1) != 0;
    sockaddr_in peer;
    bool havePeer = parsePeer(getenv("NBT_TOUCH_PEER"), &peer);
    if (havePeer) printf("[TouchFwd] touch controller from NBT_TOUCH_PEER: %s:%u\n", inet_ntoa(peer.sin_addr), (unsigned)ntohs(peer.sin_port));
    else if (connectPeer && loadPeerFile(&peer)) {
        havePeer = true;
        printf("[TouchFwd] touch controller from %s: %s:%u (learned from an earlier touch)\n", kTouchPeerFile,
               inet_ntoa(peer.sin_addr), (unsigned)ntohs(peer.sin_port));
    }
    // Gestures that reached us (iDrive did not get them), logged decoded: one line per run of the same gesture.
    int gLast = -1;
    unsigned gRun = 0, gLines = 0;
    struct timeval gAt;
    gettimeofday(&gAt, NULL);
    unsigned otherPdus = 0;   // non-touch PDUs that reached our socket (iDrive did not get them)
    printf("[TouchFwd] gesture camera PDUs on UDP %u: %s\n", a.port,
           connectPeer ? (havePeer ? "left to iDrive (socket connected to the touch controller; NBT_TOUCH_CONNECT=0 turns this off)"
                                   : "left to iDrive once the screen has been touched (the touch controller's address is "
                                     "learned from it; NBT_TOUCH_CONNECT=0 turns this off)")
                       : "reach only us while Android Auto is on screen and are dropped (NBT_TOUCH_CONNECT=0)");
    const int idleMs = (int)envNum("NBT_TOUCH_IDLE_MS", 400);   // finger "up" frames can be lost: release after this silence
    struct timeval lastFrame;
    gettimeofday(&lastFrame, NULL);

    for (;;) {
        TouchEventOut evs[MAXEV];
        int ne = 0;
        unsigned char buf[2048];

        // Port ownership follows the screen: hold it only while Android Auto shows (see setTouchOwned()).
        const bool want = g_touchOwned != 0;
        if (!want && s >= 0) {
            close(s);
            s = -1;
            for (int j = 0; j < NS; ++j) active[j] = false;   // the phone got no release: its own timeout handles it
            reasm = TouchReassembler();
            printf("[TouchFwd] released UDP %u: iDrive gets touch\n", a.port);
        }
        if (want && s < 0) {
            s = bindTouchPort(a.port);
            if (s < 0) { if (++bindFails > 50) return NULL; usleep(200000); continue; }
            bindFails = 0;
            gettimeofday(&lastFrame, NULL);
            printf("[TouchFwd] took UDP %u: touch goes to the phone\n", a.port);
            if (connectPeer && havePeer) connectToPeer(s, peer, "known peer");
        }
        if (s < 0) { usleep(100000); continue; }

        fd_set rf;
        FD_ZERO(&rf);
        FD_SET(s, &rf);
        struct timeval tv;
        tv.tv_sec = 0; tv.tv_usec = 100000;
        int sr = select(s + 1, &rf, NULL, NULL, &tv);
        if (sr < 0) { if (errno == EINTR) continue; usleep(100000); continue; }

        struct timeval now;
        gettimeofday(&now, NULL);
        if (sr == 0) {
            long gap = (now.tv_sec - lastFrame.tv_sec) * 1000 + (now.tv_usec - lastFrame.tv_usec) / 1000;
            int cnt = 0;
            for (int j = 0; j < NS; ++j) if (active[j]) ++cnt;
            if (cnt && gap >= idleMs) {
                printf("[TouchFwd] no touch frames for %ld ms: releasing %d stuck finger(s)\n", gap, cnt);
                for (int k = 0; k < NS && ne < MAXEV; ++k)
                    if (active[k]) emitUp(evs[ne++], active, cx, cy, NS, k, cx[k], cy[k]);
            }
        } else {
            sockaddr_in src;
            socklen_t sl = sizeof(src);
            memset(&src, 0, sizeof(src));
            ssize_t n = recvfrom(s, buf, sizeof(buf), 0, reinterpret_cast<sockaddr*>(&src), &sl);
            if (n < 0) { if (errno == EINTR) continue; usleep(100000); continue; }
            if (!isTouchPdu(buf, (size_t)n)) {
                ++otherPdus;
                if (otherPdus <= 20 || (otherPdus % 100) == 0) {
                    unsigned id = n >= 4 ? ((unsigned)buf[0] << 24 | (unsigned)buf[1] << 16 | (unsigned)buf[2] << 8 | buf[3]) : 0;
                    printf("[TouchFwd] non-touch PDU #%u ident 0x%08X (%d bytes) from %s:%u%s: iDrive does not get it\n",
                           otherPdus, id, (int)n, inet_ntoa(src.sin_addr), (unsigned)ntohs(src.sin_port),
                           (buf[0] == 0xB5 && (buf[1] == 0x09 || buf[1] == 0x30 || buf[1] == 0x0A)) ? " (gesture camera)" : "");
                }
                // Recognised gestures (gesture_pdu.hpp), also when packed behind a hand-coordinate PDU.
                nbt::CamPdu cp[4];
                const int np = nbt::splitCameraPdus(buf, (size_t)n, cp, 4);
                for (int q = 0; q < np; ++q) {
                    if (!nbt::isGesturePdu(cp[q])) continue;
                    struct timeval gn;
                    gettimeofday(&gn, NULL);
                    long gap = (gn.tv_sec - gAt.tv_sec) * 1000 + (gn.tv_usec - gAt.tv_usec) / 1000;
                    gAt = gn;
                    const int gid = cp[q].body[0];
                    if (gid == gLast && gap < 800) { ++gRun; continue; }
                    if (gRun > 1 && gLines < 200) printf("[TouchFwd]   (previous gesture: %u PDUs)\n", gRun);
                    gLast = gid;
                    gRun = 1;
                    if (++gLines <= 200)
                        printf("[TouchFwd] gesture camera: %s (id %d) from %s: %s\n", nbt::gestureName((unsigned)gid), gid,
                               inet_ntoa(src.sin_addr), connectPeer ? "reached us anyway (connect did not route it to iDrive)"
                                                                     : "iDrive does not get it (NBT_TOUCH_CONNECT=0)");
                }
                continue;
            }
            if (connectPeer && !havePeer && src.sin_family == AF_INET && src.sin_addr.s_addr != 0) {
                peer = src;
                havePeer = true;
                if (connectToPeer(s, peer, "learned from the first touch PDU")) savePeerFile(peer);
            }
            // One touch message = 12 records: one 112-byte datagram on the Supra, 2-4 shorter ones on 1440x540
            // units. Reassemble first, then handle every completed message.
            TouchMsg msgs[kTouchMaxMsgs];
            const int nm = reasm.feed(buf, (size_t)n, msgs, kTouchMaxMsgs);
            if (nm < 0) continue;
            lastFrame = now;
            if (!layoutLogged) {
                layoutLogged = true;
                printf("[TouchFwd] first touch datagram: %d bytes (%s)\n", (int)n,
                       n == 112 ? "whole message per datagram" : "message split over several datagrams");
            }
            for (int mi = 0; mi < nm; ++mi) {
                TouchContact c[kTouchMaxContacts];
                int declared = 0;
                const int nc = parseTouchStream(msgs[mi], c, kTouchMaxContacts, &declared);
                if (nc < 0) continue;

                if (declared >= 2) {
                    ++multiFrames;
                    if (rawLogged < 20) {
                        ++rawLogged;
                        printf("[TouchFwd] MULTI frame (%d contacts):", declared);
                        for (int k = 0; k < nc; ++k) printf(" [id %d st %d %d,%d sz %d]", c[k].id, c[k].state, c[k].x, c[k].y, c[k].size);
                        printf("\n");
                    } else if ((multiFrames % 200) == 0) {
                        printf("[TouchFwd] %d multi-finger frames so far\n", multiFrames);
                    }
                }

                int nx[NS], ny[NS], st[NS];
                bool seen[NS];
                for (int j = 0; j < NS; ++j) { st[j] = 0; seen[j] = false; nx[j] = cx[j]; ny[j] = cy[j]; }
                for (int k = 0; k < nc; ++k) {
                    const int id = c[k].id;
                    seen[id] = true;
                    st[id] = c[k].state;
                    nx[id] = clampi(c[k].x * sx + ox, 0, maxX);
                    ny[id] = clampi(c[k].y * sy + oy, 0, maxY);
                }

                // A finger that is active but no longer listed has been lifted; a "down" on an active finger means we
                // missed its "up": release it first.
                for (int k = 0; k < NS && ne < MAXEV; ++k) {
                    if (active[k] && (!seen[k] || st[k] == 1)) {
                        emitUp(evs[ne++], active, cx, cy, NS, k, cx[k], cy[k]);
                        printf("[TouchFwd] finger %d: %s -> synthesized release\n", k, !seen[k] ? "gone from frame" : "new down while still active");
                    }
                }
                // downs, then one move for all fingers, then ups
                for (int k = 0; k < NS && ne < MAXEV; ++k) {
                    if (seen[k] && (st[k] == 1 || (st[k] == 2 && !active[k]))) {
                        int before = 0;
                        for (int j = 0; j < NS; ++j) if (active[j]) ++before;
                        active[k] = true; cx[k] = nx[k]; cy[k] = ny[k];
                        TouchEventOut& e = evs[ne++];
                        fillActive(e, active, cx, cy, NS, k);
                        e.action = before == 0 ? 0 : 5;     // PRESS / POINTER_DOWN
                    }
                }
                bool moved = false;
                for (int k = 0; k < NS; ++k)
                    if (seen[k] && st[k] == 2 && active[k] && (nx[k] != cx[k] || ny[k] != cy[k])) { cx[k] = nx[k]; cy[k] = ny[k]; moved = true; }
                if (moved && ne < MAXEV) {
                    TouchEventOut& e = evs[ne++];
                    fillActive(e, active, cx, cy, NS, -1);
                    e.action = 2;                            // DRAG (all pointers)
                }
                for (int k = 0; k < NS && ne < MAXEV; ++k)
                    if (seen[k] && st[k] == 3 && active[k]) emitUp(evs[ne++], active, cx, cy, NS, k, nx[k], ny[k]);
            }   // messages
        }
        for (int i = 0; i < ne; ++i) {
            bool isMove = (evs[i].action == 2);
            if (isMove ? (loggedMoves < 40) : (logged < 600)) {
                if (isMove) ++loggedMoves; else ++logged;
                printf("[TouchFwd] action=%d idx=%d n=%d:", evs[i].action, evs[i].index, evs[i].n);
                for (int j = 0; j < evs[i].n; ++j) printf(" (%d: %d,%d)", evs[i].p[j].id, evs[i].p[j].x, evs[i].p[j].y);
                printf("\n");
            }
            if (a.cb) a.cb(evs[i], a.user);
        }
    }
    return NULL;
}
}

void setTouchOwned(bool owned) { g_touchOwned = owned ? 1 : 0; }

bool startTouchForward(uint16_t port, TouchEventCb cb, void* user) {
    FwdArgs* a = new FwdArgs;
    a->port = port; a->cb = cb; a->user = user;
    pthread_attr_t at; pthread_attr_init(&at);
    pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    pthread_t t;
    bool ok = pthread_create(&t, &at, &forwardMain, a) == 0;
    pthread_attr_destroy(&at);
    if (!ok) delete a;
    return ok;
}

bool startTouchProbe(uint16_t port, int seconds) {
    Args* a = new Args;
    a->port = port; a->seconds = seconds;
    pthread_attr_t at; pthread_attr_init(&at);
    pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    pthread_t t;
    bool ok = pthread_create(&t, &at, &probeMain, a) == 0;
    pthread_attr_destroy(&at);
    if (!ok) delete a;
    return ok;
}
}
