// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#pragma once
// Decoders for tuner (FBlocks 0x43 DAB, 0x40 FM) and connection master (0x03) MOST payloads. Pure, no I/O.
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <string>

namespace nbt {

enum { kMostFBlockConnectionMaster = 0x03, kMostFBlockDabTuner = 0x43 };
enum {
    kMostFktConnectionTable = 0xC03,    // connection master
    kMostFktDynamicLabel = 0x420,       // DAB tuner, from here on
    kMostFktDlPlus = 0xC2B,
    kMostFktLogo = 0xC0D,
    kMostFktSlide = 0xC43,
    kMostFktSlideAnnounce = 0xC44
};

inline void mostAppendUtf8(std::string& s, unsigned cp) {
    if (cp < 0x80) {
        s += (char)cp;
    } else if (cp < 0x800) {
        s += (char)(0xC0 | (cp >> 6));
        s += (char)(0x80 | (cp & 0x3F));
    } else {
        s += (char)(0xE0 | (cp >> 12));
        s += (char)(0x80 | ((cp >> 6) & 0x3F));
        s += (char)(0x80 | (cp & 0x3F));
    }
}

inline unsigned mostLatin9(uint8_t b) {
    switch (b) {
    case 0xA4: return 0x20AC;
    case 0xA6: return 0x160;
    case 0xA8: return 0x161;
    case 0xB4: return 0x17D;
    case 0xB8: return 0x17E;
    case 0xBC: return 0x152;
    case 0xBD: return 0x153;
    case 0xBE: return 0x178;
    default: return b;
    }
}

// charset 0x02: UTF-8 unchanged; 0x01: ISO 8859-15; else printable ASCII, rest '?'.
inline std::string mostString(uint8_t charset, const uint8_t* p, size_t n) {
    std::string s;
    if (!p) return s;
    if (charset == 0x02) return std::string((const char*)p, n);
    s.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        if (charset == 0x01 && p[i] >= 0x80) mostAppendUtf8(s, mostLatin9(p[i]));
        else s += (p[i] >= 0x20 && p[i] < 0x7F) ? (char)p[i] : '?';
    }
    return s;
}

// Index of the 0x00 that ends the text at `from`; false when there is none before n.
inline bool mostTextEnd(const uint8_t* p, size_t from, size_t n, size_t& end) {
    const void* z = from < n ? memchr(p + from, 0, n - from) : NULL;
    if (!z) return false;
    end = (size_t)((const uint8_t*)z - p);
    return true;
}

// fkt C2B: after 2 bytes, entries (type, then three fields charset, text, 0x00; the content is the third field);
// 1 = title, 4 = artist. The DAB tuner leaves the first two fields empty, the FM tuner fills them (car log 2026-11-18).
inline bool decodeDlPlus(const uint8_t* p, size_t n, std::string& title, std::string& artist) {
    title.clear();
    artist.clear();
    if (!p || n < 2) return false;
    size_t i = 2;
    while (i < n) {
        const uint8_t type = p[i++];
        uint8_t cs = 0;
        size_t s = 0, e = 0;
        for (int f = 0; f < 3; ++f) {
            if (i >= n) return false;
            cs = p[i];
            s = i + 1;
            if (!mostTextEnd(p, s, n, e)) return false;
            i = e + 1;
        }
        if (type == 1) title = mostString(cs, p + s, e - s);
        else if (type == 4) artist = mostString(cs, p + s, e - s);
    }
    return true;
}

// fkt 420: charset at byte 7, text from byte 8 up to 0x00.
inline bool decodeDynamicLabel(const uint8_t* p, size_t n, std::string& text) {
    text.clear();
    if (!p || n < 8) return false;
    size_t e;
    if (!mostTextEnd(p, 8, n, e)) return false;
    text = mostString(p[7], p + 8, e - 8);
    return true;
}

// fkt C44: 01 <id> 01.
inline int decodeSlideAnnounce(const uint8_t* p, size_t n) {
    if (!p || n < 3 || p[0] != 0x01 || p[2] != 0x01) return -1;
    return p[1];
}

// fkt C44 00 00 00: no slideshow (car log 2026-11-18: sent on a station change). Exactly that, so an ErrorAck (00 80 ..)
// is not taken for it.
inline bool mostNoSlideshow(const uint8_t* p, size_t n) {
    return p && n >= 3 && p[0] == 0x00 && p[1] == 0x00 && p[2] == 0x00;
}

// fkt C43: id at byte 2, JPEG from byte 4.
inline bool decodeSlide(const uint8_t* p, size_t n, int& id, const uint8_t*& img, size_t& len) {
    if (!p || n < 6 || p[4] != 0xFF || p[5] != 0xD8) return false;
    id = p[2];
    img = p + 4;
    len = n - 4;
    return true;
}

// fkt C0D: PNG from byte 1.
inline bool decodeLogo(const uint8_t* p, size_t n, const uint8_t*& img, size_t& len) {
    if (!p || n < 5 || p[1] != 0x89 || p[2] != 0x50 || p[3] != 0x4E || p[4] != 0x47) return false;
    img = p + 1;
    len = n - 1;
    return true;
}

// fkt C0D 01 without a PNG: no logo (car log 2026-11-18: sent on a station change).
inline bool mostNoLogo(const uint8_t* p, size_t n) { return p && n >= 1 && n < 5 && p[0] == 0x01; }

// String field at i (charset, text, 0x00); advances i past the 0x00.
inline bool mostReadString(const uint8_t* p, size_t n, size_t& i, std::string& out) {
    size_t e;
    if (i >= n || !mostTextEnd(p, i + 1, n, e)) return false;
    out = mostString(p[i], p + i + 1, e - i - 1);
    i = e + 1;
    return true;
}

enum { kMostFBlockFmTuner = 0x40 };
enum { kMostFktDabStation = 0xC13, kMostFktFmStation = 0xC0F, kMostFktRadioText = 0xC2D };

struct DabStation {
    std::string shortName, longName;
    uint32_t sid;
    bool tuning;
};

// fkt C13: 2 bytes, 2 ensemble strings, EId and ECC (3), short name, long name, SId (4), 4 unknown, state (0x90 = tuning).
inline bool decodeDabStation(const uint8_t* p, size_t n, DabStation& out) {
    out.shortName.clear();
    out.longName.clear();
    out.sid = 0;
    out.tuning = false;
    if (!p || n < 2) return false;
    size_t i = 2;
    std::string skip;
    if (!mostReadString(p, n, i, skip) || !mostReadString(p, n, i, skip)) return false;
    if (n - i < 3) return false;
    i += 3;
    std::string shortName, longName;
    if (!mostReadString(p, n, i, shortName) || !mostReadString(p, n, i, longName)) return false;
    if (n - i < 9) return false;
    const uint32_t sid = ((uint32_t)p[i] << 24) | ((uint32_t)p[i + 1] << 16) | ((uint32_t)p[i + 2] << 8) | p[i + 3];
    const uint8_t state = p[i + 8];
    out.shortName = shortName;
    out.longName = longName;
    out.sid = sid;
    out.tuning = state == 0x90 || (shortName.empty() && longName.empty());
    return true;
}

struct FmStation {
    std::string ps;
    uint32_t freqKHz;
    int pi;
};

// fkt C0F: PS string, 1 byte, frequency (4, kHz), flag (0x01 = PI valid), PI (2).
inline bool decodeFmStation(const uint8_t* p, size_t n, FmStation& out) {
    out.ps.clear();
    out.freqKHz = 0;
    out.pi = -1;
    if (!p) return false;
    size_t i = 0;
    std::string ps;
    if (!mostReadString(p, n, i, ps)) return false;
    if (n - i < 8) return false;
    out.ps = ps;
    out.freqKHz = ((uint32_t)p[i + 1] << 24) | ((uint32_t)p[i + 2] << 16) | ((uint32_t)p[i + 3] << 8) | p[i + 4];
    const int pi = (p[i + 6] << 8) | p[i + 7];
    out.pi = (p[i + 5] == 0x01 && pi != 0) ? pi : -1;
    return true;
}

// fkt C2D: charset, text, 0x00.
inline bool decodeRadioText(const uint8_t* p, size_t n, std::string& text) {
    text.clear();
    if (!p) return false;
    size_t i = 0;
    return mostReadString(p, n, i, text);
}

// DAB 0x68-0x6A and 0x6E-0x70 (0x6B-0x6D is SDARS), DAB announcement 0x3B-0x3D.
inline bool mostIsDabSource(uint8_t s) {
    return (s >= 0x68 && s <= 0x6A) || (s >= 0x6E && s <= 0x70) || (s >= 0x3B && s <= 0x3D);
}

// FM/AM 0x01-0x10 without 0x08-0x0A ("No Audio"), FM traffic 0x38, DAB.
inline bool mostIsTunerSource(uint8_t s) {
    return (s >= 0x01 && s <= 0x10 && !(s >= 0x08 && s <= 0x0A)) || s == 0x38 || mostIsDabSource(s);
}

inline bool connectionTableValid(const uint8_t* p, size_t n) {
    (void)p;
    return n >= 2 && (n - 2) % 3 == 0;
}

// fkt C03: after 2 bytes, entries (zone, source, state); first audible tuner in zone 0, else 0.
inline uint8_t audibleTuner(const uint8_t* p, size_t n) {
    if (!p) return 0;
    for (size_t i = 2; i + 3 <= n; i += 3)
        if (p[i] == 0 && p[i + 2] == 0x00 && mostIsTunerSource(p[i + 1])) return p[i + 1];
    return 0;
}

}  // namespace nbt
