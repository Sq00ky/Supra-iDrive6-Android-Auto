// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef NBT_USB_AOAP_HPP
#define NBT_USB_AOAP_HPP

// Android Auto over USB through the head unit's own Android Open Accessory (AOA) drivers (header-only, no QNX
// calls, so the host tests can use it). Reverse-engineered from the firmware dump, not car-verified:
//
// 1. vdev-medialaunch asks every new USB device for its AOA protocol and, for a phone that has one, starts
//    `/opt/mm/bin/aoap-switch -c /opt/mm/etc/aoap.cfg -t` (umass-enum.cfg). With -t it only creates
//    /dev/android<bus><dev> and waits for an app to trigger the switch through the devctls below.
// 2. After the switch the phone re-enumerates as 18d1:2d00/2d01 and medialauncher starts
//    `devc-serusb-aoap -l 16384`, which creates /dev/oap<bus><dev>: read() = bulk IN, write() = bulk OUT.
//    Each read() is answered with one whole USB transfer and the rest of it is dropped when the caller's
//    buffer is smaller, so it must always be read with a buffer of at least the -l size.

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

namespace nbt {
namespace aoap {

// devctl commands of aoap-switch (decoded from its io_devctl, class 5).
static const unsigned kDcmdGetMode      = 0x40040501u;   // __DIOF(5, 1, int): 0 = not switched yet
static const unsigned kDcmdSwitch       = 0xC0040502u;   // __DIOTF(5, 2, int): switch with the aoap.cfg identity
static const unsigned kDcmdConfigSwitch = 0xC6040503u;   // __DIOTF(5, 3, Identity): switch with our identity
static const unsigned kDcmdGetProtocol  = 0x40040504u;   // __DIOF(5, 4, int): the phone's AOA protocol version

// Payload of kDcmdConfigSwitch. aoap-switch strcpy()s each string into a 128-byte buffer, so keep them under 128.
// protocol 1 = accessory only (2d00/2d01); 2 also sends SET_AUDIO_MODE and makes medialauncher start io-audio.
struct Identity {
    int32_t protocol;
    char manufacturer[256];
    char model[256];
    char description[256];
    char version[256];
    char url[256];
    char serial[256];
};
typedef char IdentitySizeCheck[sizeof(Identity) == 1540 ? 1 : -1];   // the size encoded in kDcmdConfigSwitch

static const size_t kMaxField = 127;   // aoap-switch's copy buffers are 128 bytes

inline void copyField(char* dst, size_t n, const char* src) {
    memset(dst, 0, n);
    if (!src) return;
    size_t len = strlen(src);
    if (len > kMaxField) len = kMaxField;
    memcpy(dst, src, len);
}

// The identity Android Auto answers (the one open-source head units use). NULL = default.
inline void fillIdentity(Identity& id, const char* manufacturer = NULL, const char* model = NULL) {
    memset(&id, 0, sizeof(id));
    id.protocol = 1;
    copyField(id.manufacturer, sizeof(id.manufacturer), (manufacturer && *manufacturer) ? manufacturer : "Android");
    copyField(id.model, sizeof(id.model), (model && *model) ? model : "Android Auto");
    copyField(id.description, sizeof(id.description), "Android Auto");
    copyField(id.version, sizeof(id.version), "2.0.1");
    copyField(id.url, sizeof(id.url), "https://github.com/Sq00ky/Supra-iDrive6-Android-Auto");
    copyField(id.serial, sizeof(id.serial), "HU-NBTAAD0001");
}

// "android012" / "oap13": prefix followed by one or more digits only (the drivers name their nodes "%s%d%d").
inline bool isNode(const char* name, const char* prefix) {
    if (!name || !prefix) return false;
    size_t pl = strlen(prefix);
    if (strncmp(name, prefix, pl) != 0) return false;
    const char* p = name + pl;
    if (!*p) return false;
    for (; *p; ++p) {
        if (*p < '0' || *p > '9') return false;
    }
    return true;
}

// Length of the complete AA frame at p (4-byte header, +4 total size on a FIRST-only frame, then the payload),
// or 0 while the header itself is incomplete. Used to hand the phone one frame per bulk transfer.
inline size_t frameLength(const uint8_t* p, size_t avail) {
    if (avail < 4) return 0;
    size_t hdr = ((p[1] & 0x03) == 0x01) ? 8 : 4;
    return hdr + ((static_cast<size_t>(p[2]) << 8) | p[3]);
}

// How many USB resets a phone gets. Not per accessory node: a reset can bring the phone back under a new key, which
// is a new node, so a per-node count would never run out on a phone that keeps failing.
class ResetBudget {
public:
    enum { kMax = 2 };
    ResetBudget() : used_(0), last_s_(-1e9) {}
    // True (and counted) when one more reset is allowed. The count starts over kWindowS after the last reset.
    bool take(double now) {
        if (now - last_s_ >= kWindowS()) used_ = 0;
        if (used_ >= kMax) return false;
        ++used_;
        last_s_ = now;
        return true;
    }
    // Within s of the last reset: the phone may be off the bus because of it.
    bool within(double now, double s) const { return now - last_s_ < s; }

private:
    static double kWindowS() { return 120.0; }
    int used_;
    double last_s_;
};

// Why an accessory (a phone already in accessory mode) is or is not reset before its first session.
enum StartState {
    kAccNew = 0,              // plugged in (or switched by someone else) while nbt-aad ran: no reset
    kAccAtConnect,            // reported during usbd_connect: in accessory mode before nbt-aad ran -> reset
    kAccSoonAfterConnect,     // appeared within the start window and nobody here switched it -> reset
    kAccSwitchedByUs          // appeared after nbt-aad sent a switch itself: it is ours, fresh -> no reset
};
// connected_s: when usbd_connect returned (0 = still in it); seen_s: when io-usb reported the accessory; switch_s:
// when nbt-aad last sent an accessory switch (either path), 0 = never. A Samsung S25 came back 0.5 s after our own
// switch, inside the 1 s start window, and was reset: it then attached twice and its session failed (2026-11-17).
inline StartState accessoryStartState(double connected_s, double seen_s, double switch_s, double window_s) {
    if (connected_s == 0) return kAccAtConnect;
    if (switch_s > 0 && seen_s >= switch_s) return kAccSwitchedByUs;
    if (seen_s - connected_s < window_s) return kAccSoonAfterConnect;
    return kAccNew;
}
inline bool resetAtStart(StartState s) { return s == kAccAtConnect || s == kAccSoonAfterConnect; }

// Gap before the next session on the same plug-in, after `started` sessions. A phone needs a few seconds to give up
// on a failed session (its own start-up timeout is 5 s); a retry 3 s later met a phone still tearing down
// ("No USB state changes occurred within 2000ms", 2026-11-17).
inline double sessionRetryGapS(int started) {
    return started <= 1 ? 5.0 : started == 2 ? 10.0 : 20.0;
}

// Length of a leading "[Tag] " in a log format, 0 if there is none.
inline size_t logTagEnd(const char* fmt) {
    if (!fmt || fmt[0] != '[') return 0;
    const char* e = strchr(fmt, ']');
    return (e && e[1] == ' ') ? static_cast<size_t>(e - fmt) + 2 : 0;
}

inline double upS() {   // seconds since boot: the clock of main.cpp's [%9.3f] lines and of the iDrive app log
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

// printf for the [UsbAccessory] / [UsbDirect] lines: the seconds since boot go in after the tag, so a USB log can be
// lined up with the other daemon lines and a phone's logcat. One write per line (threads log concurrently).
inline void usbLog(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
inline void usbLog(const char* fmt, ...) {
    char line[1024];
    size_t t = logTagEnd(fmt);
    int n = snprintf(line, sizeof(line), "%.*s[%9.3f] ", (int)t, fmt, upS());
    if (n < 0 || n >= (int)sizeof(line)) n = 0;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line + n, sizeof(line) - n, fmt + t, ap);
    va_end(ap);
    fputs(line, stdout);
}

} // namespace aoap
} // namespace nbt

#endif // NBT_USB_AOAP_HPP
