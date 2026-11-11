// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef CAR_MFL_HPP
#define CAR_MFL_HPP

// Steering-wheel (MFL) buttons: MOST FBlock 0x15, instance 0x02, Status messages. Two jobs:
//
//   1. Recon: log every function the block carries when it is first seen and whenever its value CHANGES, so a press is
//      never missed by sampling (a J29 sends periodic 401 = 00000001 and 511 = 00, which a time-based sampler hides):
//      "[MFL] 15/02 fkt=511 op=C len=1: 02 (changed, #11)".
//   2. Media keys. A function and - for the roller, which carries its DIRECTION in the payload - an optional value, map
//      to Android's next (87) / previous (88) / play-pause (85) keys:
//        NBT_MFL_NEXT=511:2      fkt 511, value 02 is a press
//        NBT_MFL_PREV=511:1      fkt 511, value 01 is a press
//        NBT_MFL_PLAYPAUSE=C31   any change away from 0 is a press
//      Each setting is a comma separated list ("511:2,C31"), value in hex like the fkt, no ":value" = any non-zero.
//      Values are hex as the log prints them.
//
// The J29's track roller sent 00 -> 02 -> 00 in pairs while the button was held (auto-repeat, ~3/s) and 01 the other
// way, which is why the value has to take part in the mapping: one function, two directions.
//
// Host-tested (tests/test_mfl.cpp). Plain C++98: the car builds with GCC 4.4.2.

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aa_keys.hpp"
#include "nbt_env.hpp"

namespace nbt {
namespace car {

enum { kMflBindings = 6 };

// The thumb-wheel press is not a key of its own: the daemon asks the cluster list which row the cursor is on
// (cluster/ric_media.hpp). Its events carry this value, which is never sent to the phone.
static const uint32_t kMflWheelKey = 0x7E000031u;

// One mapping: a function (and optionally a value) -> a key. value < 0: any change away from 0 is the press.
struct MflBinding {
    int fkt;
    int value;
    uint32_t key;
    const char* what;
    bool pressed;       // the binding is down (its value was seen, no 0 since)
    MflBinding() : fkt(-1), value(-1), key(0), what(""), pressed(false) {}
};

struct MflConfig {
    MflBinding b[kMflBindings];
    int n;
    bool log;           // NBT_MFL_LOG=0 silences the recon lines
    MflConfig() : n(0), log(true) {}
};

// "FKT" or "FKT:VALUE", hex; several separated by commas. Appends what it understands, returns how many.
inline int mflParseBindings(const char* s, MflBinding* out, int max, int* n, uint32_t key, const char* what) {
    if (!s || !*s) return 0;
    int added = 0;
    while (*s && *n < max) {
        while (*s == ' ' || *s == ',') ++s;
        if (!*s) break;
        char* end = NULL;
        long fkt = strtol(s, &end, 16);
        if (end == s) { ++s; continue; }          // junk: skip a character rather than the rest
        int value = -1;
        s = end;
        if (*s == ':') {
            s++;
            long v = strtol(s, &end, 16);
            if (end != s) { value = (int)v; s = end; }
        }
        if (fkt <= 0 || fkt > 0x0FFF || value > 0xFF) continue;
        MflBinding& b = out[(*n)++];
        b.fkt = (int)fkt;
        b.value = value;
        b.key = key;
        b.what = what;
        b.pressed = false;
        ++added;
    }
    return added;
}

// Removes the bindings of one key, so an NBT_MFL_* setting replaces that key's defaults and leaves the others alone.
inline void mflDropKey(MflConfig& c, uint32_t key) {
    int k = 0;
    for (int i = 0; i < c.n; ++i)
        if (c.b[i].key != key) c.b[k++] = c.b[i];
    c.n = k;
}

inline MflConfig readMflConfig() {
    MflConfig c;
    // Defaults from the J29 logs (2026-11-08): the track roller on 15/02 fkt 511, value 02 one way and 01 the other.
    // Which is "next" is the guess; swap the two settings if the wheel skips backwards.
    mflParseBindings("511:2", c.b, kMflBindings, &c.n, AAK_MEDIA_NEXT, "MFL_NEXT");
    mflParseBindings("511:1", c.b, kMflBindings, &c.n, AAK_MEDIA_PREVIOUS, "MFL_PREV");
    if (getenv("NBT_MFL_NEXT")) {
        mflDropKey(c, AAK_MEDIA_NEXT);
        mflParseBindings(getenv("NBT_MFL_NEXT"), c.b, kMflBindings, &c.n, AAK_MEDIA_NEXT, "MFL_NEXT");
    }
    if (getenv("NBT_MFL_PREV")) {
        mflDropKey(c, AAK_MEDIA_PREVIOUS);
        mflParseBindings(getenv("NBT_MFL_PREV"), c.b, kMflBindings, &c.n, AAK_MEDIA_PREVIOUS, "MFL_PREV");
    }
    if (getenv("NBT_MFL_PLAYPAUSE")) {
        mflDropKey(c, AAK_MEDIA_PLAY_PAUSE);
        mflParseBindings(getenv("NBT_MFL_PLAYPAUSE"), c.b, kMflBindings, &c.n, AAK_MEDIA_PLAY_PAUSE, "MFL_PLAYPAUSE");
    }
    // Thumb-wheel press (J29: fkt C31, 01 down / 00 up), only with the cluster track title on. The wheel's rotation
    // (C30) is deliberately not here. NBT_KOMBI_MEDIA_PRESS=<fkt[:value]> names another function, =0 turns it off.
    const char* km = getenv("NBT_KOMBI_MEDIA");
    if (km && !strcmp(km, "1")) {
        const char* p = getenv("NBT_KOMBI_MEDIA_PRESS");
        mflParseBindings(p ? p : "C31", c.b, kMflBindings, &c.n, kMflWheelKey, "MFL_WHEEL");
    }
    c.log = envFlag("NBT_MFL_LOG") != 0;
    return c;
}

struct MflEvent {
    int down;               // 1 press, 0 release
    uint32_t keycode;       // AA (Android) keycode
    const char* what;       // "MFL_NEXT" / "MFL_PREV" / "MFL_PLAYPAUSE"
};

class MflDecoder {
public:
    enum { kFkts = 16 };

    MflDecoder() : n_(0) {}

    void setConfig(const MflConfig& cfg) { cfg_ = cfg; }

    // One message on 15/02. Returns the number of events written to `out` (at most kMflBindings * 2).
    int feed(int fkt, int op, const uint8_t* data, size_t len, MflEvent* out, int max) {
        if (!data || len == 0) return 0;
        const uint8_t v = data[0];
        int n = 0;

        // Recon: one line per change of a function's first byte.
        Slot* s = slot(fkt);
        if (s) {
            const bool first = s->count == 0;
            ++s->count;
            const bool changed = first ? true : (v != s->last);
            s->last = v;
            if (cfg_.log && changed && fkt != 0x512) {   // the voice button's own tracker prints itself
                printf("[MFL] 15/02 fkt=%03X op=%X len=%u:", fkt, op, (unsigned)len);
                for (size_t i = 0; i < len && i < 8; ++i) printf(" %02x", data[i]);
                printf(" (%s, #%u)\n", first ? "new" : "changed", s->count);
            }
        }

        // Keys: a binding is down while its value is seen, up on 0. A press is a change into that state.
        for (int i = 0; i < cfg_.n && n < max; ++i) {
            MflBinding& b = cfg_.b[i];
            if (b.fkt != fkt) continue;
            const bool active = v != 0 && (b.value < 0 || v == b.value);
            if (active && !b.pressed) {
                b.pressed = true;
                out[n].down = 1;
                out[n].keycode = b.key;
                out[n].what = b.what;
                ++n;
            } else if (v == 0 && b.pressed) {
                b.pressed = false;
                out[n].down = 0;
                out[n].keycode = b.key;
                out[n].what = b.what;
                ++n;
            }
        }
        return n;
    }

    const MflConfig& config() const { return cfg_; }

private:
    struct Slot { int fkt; uint8_t last; unsigned count; };

    Slot* slot(int fkt) {
        for (int i = 0; i < n_; ++i) if (seen_[i].fkt == fkt) return &seen_[i];
        if (n_ >= kFkts) return NULL;
        Slot& s = seen_[n_++];
        s.fkt = fkt;
        s.last = 0;
        s.count = 0;
        return &s;
    }

    MflConfig cfg_;
    Slot seen_[kFkts];
    int n_;
};

} // namespace car
} // namespace nbt

#endif // CAR_MFL_HPP
