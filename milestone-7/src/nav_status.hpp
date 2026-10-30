// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef NAV_STATUS_HPP
#define NAV_STATUS_HPP

// Android Auto navigation status channel (ch 10): readable log + state file for the HUD / cluster work.
//
// During route guidance the phone sends on ch 10 (car-seen, v1.8.47 log 2026-10-30, Google Maps):
//   0x8003  NavigationStatus {1: status}            1 = guidance active, 2 = inactive
//   0x8006  turn-by-turn state, 63..111 bytes, about once per second while guiding
//   0x8007  (and 0x8004 / 0x8005 on older phones) next turn / distance
// Until now the daemon only logged the size. The exact .proto layout of 0x8006/0x8007 is not public and
// differs between phone versions, so this decoder makes no assumptions: it walks the protobuf generically and
// prints every field as  <field>:<value>  (strings in quotes, nested messages in { }), e.g.
//   [Nav] 0x8006 {1:{1:{1:"Via Roma" 2:{1:3 2:2}} 3:{1:250 2:"250" 3:1}}}
// One drive with guidance gives the field map (maneuver type, road name, distance, units), which is what a
// later step needs to feed the HUD / instrument cluster.
//
// Log volume: a message is printed only when its content differs from the previous one of the same id, and at
// most once every NBT_NAV_LOG_MS (default 2000 ms) per id; road-name changes are always printed.
// State file: /dev/shmem/nbt-aad-nav.txt (rewritten on every change; read it with cat while driving):
//   guidance=1|0  updated_ms=<monotonic>  then one line per message id: 0x8006=<decoded>
// NBT_NAV_DECODE=0 turns all of this off (back to the old size-only line).

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <string>
#include <vector>

namespace nbt {

namespace navpb {

inline bool readVarint(const uint8_t* p, size_t n, size_t& i, uint64_t& v) {
    v = 0;
    int sh = 0;
    while (i < n && sh < 64) {
        uint8_t b = p[i++];
        v |= (uint64_t)(b & 0x7F) << sh;
        if (!(b & 0x80)) return true;
        sh += 7;
    }
    return false;
}

// Printable UTF-8 text (road names have accents: "Viale Italia", "Straße", "Città")
inline bool looksLikeText(const uint8_t* p, size_t n) {
    if (n == 0) return false;
    size_t i = 0;
    while (i < n) {
        uint8_t c = p[i];
        if (c < 0x20 && c != '\t') return false;
        if (c == 0x7F) return false;
        if (c < 0x80) { ++i; continue; }
        size_t len = (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 0;
        if (len == 0 || i + len > n) return false;
        for (size_t k = 1; k < len; ++k) if ((p[i + k] & 0xC0) != 0x80) return false;
        i += len;
    }
    return true;
}

// Does the buffer parse completely as protobuf fields? (used to tell nested messages from opaque bytes)
inline bool parsesAsMessage(const uint8_t* p, size_t n, int depth) {
    if (n == 0 || depth > 8) return false;
    size_t i = 0;
    while (i < n) {
        uint64_t tag;
        if (!readVarint(p, n, i, tag)) return false;
        uint32_t f = (uint32_t)(tag >> 3), w = (uint32_t)(tag & 7);
        if (f == 0) return false;
        if (w == 0) { uint64_t v; if (!readVarint(p, n, i, v)) return false; }
        else if (w == 1) { if (i + 8 > n) return false; i += 8; }
        else if (w == 5) { if (i + 4 > n) return false; i += 4; }
        else if (w == 2) {
            uint64_t len;
            if (!readVarint(p, n, i, len) || len > n - i) return false;
            i += (size_t)len;
        } else return false;
    }
    return true;
}

inline void appendf(std::string& out, const char* fmt, uint64_t a) {
    char b[48];
    snprintf(b, sizeof(b), fmt, (unsigned long long)a);
    out += b;
}

// Generic dump. strings collects every text field (road names, cues) so callers can spot changes cheaply.
inline void dump(const uint8_t* p, size_t n, std::string& out, std::vector<std::string>& strings, int depth) {
    size_t i = 0;
    bool first = true;
    out += '{';
    while (i < n) {
        uint64_t tag;
        if (!readVarint(p, n, i, tag)) { out += " ?"; break; }
        uint32_t f = (uint32_t)(tag >> 3), w = (uint32_t)(tag & 7);
        if (!first) out += ' ';
        first = false;
        appendf(out, "%llu:", f);
        if (w == 0) {
            uint64_t v;
            if (!readVarint(p, n, i, v)) { out += '?'; break; }
            appendf(out, "%llu", v);
        } else if (w == 1) {
            if (i + 8 > n) { out += '?'; break; }
            double d; memcpy(&d, p + i, 8); i += 8;
            char b[48]; snprintf(b, sizeof(b), "%gd", d); out += b;
        } else if (w == 5) {
            if (i + 4 > n) { out += '?'; break; }
            float fl; memcpy(&fl, p + i, 4); i += 4;
            char b[48]; snprintf(b, sizeof(b), "%gf", (double)fl); out += b;
        } else if (w == 2) {
            uint64_t len;
            if (!readVarint(p, n, i, len) || len > n - i) { out += '?'; break; }
            const uint8_t* q = p + i;
            size_t ln = (size_t)len;
            i += ln;
            // text first: a nested message starts with a tag byte below 0x20 for fields 1..3, so it never
            // passes as text; a road name practically never parses as a complete protobuf message
            if (looksLikeText(q, ln)) {
                std::string s((const char*)q, ln);
                strings.push_back(s);
                out += '"'; out += s; out += '"';
            } else if (parsesAsMessage(q, ln, depth + 1)) {
                dump(q, ln, out, strings, depth + 1);
            } else {
                appendf(out, "<%llu bytes>", ln);          // images (turn icons) and other opaque data
            }
        } else {
            out += '?';
            break;
        }
    }
    out += '}';
}

}  // namespace navpb

class NavStatusLog {
public:
    NavStatusLog() : enabled_(true), intervalMs_(2000), guidance_(-1) {
        const char* e = getenv("NBT_NAV_DECODE");
        if (e && (!strcmp(e, "0") || !strcmp(e, "off") || !strcmp(e, "no") || !strcmp(e, "false"))) enabled_ = false;
        const char* ms = getenv("NBT_NAV_LOG_MS");
        if (ms && *ms) intervalMs_ = atol(ms);
        for (int k = 0; k < kSlots; ++k) lastPrintMs_[k] = -1000000;
    }

    bool enabled() const { return enabled_; }

    // frame = decrypted message as aa_session sees it: 2-byte message id, then the protobuf body.
    void onMessage(uint16_t msgId, const std::vector<uint8_t>& frame) {
        if (!enabled_ || frame.size() < 2) return;
        const uint8_t* body = &frame[0] + 2;
        size_t n = frame.size() - 2;
        int slot = msgId - 0x8000;
        if (slot < 0 || slot >= kSlots) return;

        std::string text;
        std::vector<std::string> strings;
        navpb::dump(body, n, text, strings, 0);

        if (msgId == 0x8003) {
            uint64_t st = 0;
            size_t i = 0;
            uint64_t tag;
            if (n > 0 && navpb::readVarint(body, n, i, tag) && (tag >> 3) == 1 && (tag & 7) == 0) navpb::readVarint(body, n, i, st);
            int g = (st == 1) ? 1 : 0;
            if (g != guidance_) {
                guidance_ = g;
                if (!g) for (int k = 0; k < kSlots; ++k) if (k != 3) last_[k].clear();
            }
        }

        long now = nowMs();
        bool changed = (text != last_[slot]);
        bool newRoad = (strings != lastStrings_[slot]);
        if (changed && (newRoad || now - lastPrintMs_[slot] >= intervalMs_ || msgId == 0x8003)) {
            printf("[Nav] 0x%04X %s\n", msgId, text.c_str());
            lastPrintMs_[slot] = now;
        }
        if (changed) {
            last_[slot] = text;
            lastStrings_[slot] = strings;
            writeState(now);
        }
    }

private:
    enum { kSlots = 16 };   // message ids 0x8000..0x800F

    static long nowMs() {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (long)(ts.tv_sec * 1000L + ts.tv_nsec / 1000000L);
    }

    void writeState(long now) {
        // write to a temp file and rename, so a reader never sees half a file
        FILE* f = fopen("/dev/shmem/nbt-aad-nav.tmp", "w");
        if (!f) return;
        fprintf(f, "guidance=%d\nupdated_ms=%ld\n", guidance_ < 0 ? 0 : guidance_, now);
        for (int k = 0; k < kSlots; ++k)
            if (!last_[k].empty()) fprintf(f, "0x%04X=%s\n", 0x8000 + k, last_[k].c_str());
        fclose(f);
        rename("/dev/shmem/nbt-aad-nav.tmp", "/dev/shmem/nbt-aad-nav.txt");
    }

    bool enabled_;
    long intervalMs_;
    int guidance_;
    std::string last_[kSlots];
    std::vector<std::string> lastStrings_[kSlots];
    long lastPrintMs_[kSlots];
};

}  // namespace nbt

#endif
