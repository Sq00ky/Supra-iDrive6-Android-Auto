// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef NBT_NATIVE_MEDIA_RECORD_HPP
#define NBT_NATIVE_MEDIA_RECORD_HPP

#include "audio/media_state.hpp"

namespace nbt {

struct NativeMediaSnapshot {
    bool active;
    uint32_t updatedMs, writerPid, sequence;
    char song[kMediaValueMax + 1], artist[kMediaValueMax + 1];
    NativeMediaSnapshot() : active(false), updatedMs(0), writerPid(0), sequence(0) { song[0] = artist[0] = 0; }
};

inline size_t nativeMediaUtf16(const char* text, uint16_t* out) {
    size_t n = 0, i = 0;
    while (i < kMediaValueMax && text[i]) {
        const unsigned char c = (unsigned char)text[i];
        size_t count = c < 0x80 ? 1 : c >= 0xC2 && c <= 0xDF ? 2 :
                       c >= 0xE0 && c <= 0xEF ? 3 : c >= 0xF0 && c <= 0xF4 ? 4 : 0;
        uint32_t cp = count == 1 ? c : c & (count == 2 ? 0x1F : count == 3 ? 0x0F : 7);
        bool valid = count && i + count <= kMediaValueMax;
        for (size_t j = 1; valid && j < count; ++j) {
            const unsigned char next = (unsigned char)text[i + j];
            if ((next & 0xC0) != 0x80) valid = false;
            else cp = (cp << 6) | (next & 0x3F);
        }
        if (!valid || (count == 2 && cp < 0x80) || (count == 3 && cp < 0x800) ||
            (count == 4 && cp < 0x10000) || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
            cp = '?'; count = 1;
        }
        if (cp < 0x20 || cp == 0x7F) cp = ' ';
        if (cp > 0xFFFF) {
            out[n++] = (uint16_t)(0xD800 + ((cp - 0x10000) >> 10));
            out[n++] = (uint16_t)(0xDC00 + ((cp - 0x10000) & 0x3FF));
        } else out[n++] = (uint16_t)cp;
        i += count;
    }
    out[n] = 0;
    return n;
}

inline size_t nativeMediaPrefix(const uint16_t* text, size_t size, size_t limit) {
    size_t n = size < limit ? size : limit;
    if (n && text[n - 1] >= 0xD800 && text[n - 1] <= 0xDBFF) --n;
    while (n && text[n - 1] == ' ') --n;
    return n;
}

// Each of the high window's two labels allows 25 UTF-16 units.
inline size_t nativeMediaText(const char* utf8, uint16_t out[26]) {
    uint16_t text[kMediaValueMax + 1];
    const size_t size = nativeMediaUtf16(utf8, text);
    const size_t n = nativeMediaPrefix(text, size, 25);
    memcpy(out, text, n * sizeof(*out));
    out[n] = 0;
    return n;
}

inline bool nativeMediaUint(const char* p, size_t n, uint32_t& out) {
    if (!n || n > 10) return false;
    uint32_t v = 0;
    for (size_t i = 0; i < n; ++i) {
        if (p[i] < '0' || p[i] > '9') return false;
        const unsigned d = (unsigned)(p[i] - '0');
        if (v > (UINT32_MAX - d) / 10u) return false;
        v = v * 10u + d;
    }
    out = v;
    return true;
}

inline bool nativeMediaIdentity(const char* value, size_t n, uint32_t& pid, uint32_t& sequence) {
    const char* colon = (const char*)memchr(value, ':', n);
    return colon && nativeMediaUint(value, (size_t)(colon - value), pid) &&
           nativeMediaUint(colon + 1, n - (size_t)(colon + 1 - value), sequence);
}

inline bool nativeMediaGeneration(const char* head, size_t n, uint32_t& pid, uint32_t& sequence) {
    if (!head || n < 16 || memcmp(head, "native_begin=", 13)) return false;
    const char* nl = (const char*)memchr(head + 13, '\n', n - 13);
    return nl && nativeMediaIdentity(head + 13, (size_t)(nl - head - 13), pid, sequence);
}

// The file reader must also reread the first generation AFTER reading the body and compare pid/sequence.
inline bool parseNativeMediaRecord(const char* p, size_t n, uint32_t nowMs, NativeMediaSnapshot& out) {
    out = NativeMediaSnapshot();
    if (!p || !n || n > kMediaRecordMax || p[n - 1] != '\n' || memchr(p, 0, n)) return false;
    unsigned seen = 0;
    uint32_t begin = 0, end = 0, pid = 0, endPid = 0, native = 0, have = 0;
    bool playing = false;
    size_t at = 0;
    while (at < n) {
        const char* line = p + at;
        const char* nl = (const char*)memchr(line, '\n', n - at);
        if (!nl) return false;
        const char* eq = (const char*)memchr(line, '=', (size_t)(nl - line));
        if (!eq) return false;
        const size_t key = (size_t)(eq - line), len = (size_t)(nl - eq - 1);
        const char* value = eq + 1;
        unsigned bit = 0;
        if (key == 12 && !memcmp(line, "native_begin", key)) {
            bit = 1;
            if (at != 0 || !nativeMediaIdentity(value, len, pid, begin)) return false;
        } else if (key == 10 && !memcmp(line, "native_end", key)) {
            bit = 2;
            if (nl != p + n - 1 || !nativeMediaIdentity(value, len, endPid, end)) return false;
        } else if (key == 6 && !memcmp(line, "native", key)) {
            bit = 4;
            if (!nativeMediaUint(value, len, native) || native > 1) return false;
        } else if (key == 10 && !memcmp(line, "updated_ms", key)) {
            bit = 8;
            if (!nativeMediaUint(value, len, out.updatedMs)) return false;
        } else if (key == 4 && !memcmp(line, "have", key)) {
            bit = 16;
            if (!nativeMediaUint(value, len, have) || have > 3) return false;
        } else if (key == 5 && !memcmp(line, "state", key)) {
            bit = 32;
            playing = len == 7 && !memcmp(value, "playing", len);
            if (!playing && !(len == 6 && !memcmp(value, "paused", len)) &&
                !(len == 7 && (!memcmp(value, "stopped", len) || !memcmp(value, "unknown", len)))) return false;
        } else if ((key == 4 && !memcmp(line, "song", key)) || (key == 6 && !memcmp(line, "artist", key))) {
            const bool song = key == 4;
            bit = song ? 64 : 128;
            if (len > kMediaValueMax) return false;
            char* text = song ? out.song : out.artist;
            for (size_t i = 0; i < len; ++i)
                if ((unsigned char)value[i] < 0x20 || (unsigned char)value[i] == 0x7F) return false;
            memcpy(text, value, len);
            text[len] = 0;
        }
        if (seen & bit) return false;
        seen |= bit;
        at = (size_t)(nl - p) + 1;
    }
    if (seen != 255 || begin != end || pid != endPid) { out = NativeMediaSnapshot(); return false; }
    out.writerPid = pid;
    out.sequence = begin;
    out.active = native && playing && have == 3 && out.song[0] &&
                 (uint32_t)(nowMs - out.updatedMs) < kMediaStaleMs;
    return true;
}

inline std::string nativeMediaRecord(const MediaState& st, bool native, uint32_t sequence, uint32_t pid) {
    char head[48], tail[96];
    snprintf(head, sizeof(head), "native_begin=%u:%u\n", (unsigned)pid, (unsigned)sequence);
    snprintf(tail, sizeof(tail), "native=%u\nupdated_ms=%u\nnative_end=%u:%u\n", native ? 1u : 0u,
             (unsigned)st.updatedMs, (unsigned)pid, (unsigned)sequence);
    return std::string(head) + mediaRecord(st) + tail;
}

} // namespace nbt
#endif
