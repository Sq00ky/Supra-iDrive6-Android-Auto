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

#include <stddef.h>
#include <stdint.h>
#include <string.h>

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

} // namespace aoap
} // namespace nbt

#endif // NBT_USB_AOAP_HPP
