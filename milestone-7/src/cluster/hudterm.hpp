// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef CLUSTER_HUDTERM_HPP
#define CLUSTER_HUDTERM_HPP

// The MOST HUDTerminal protocol (FBlock 82, instance 00), which F-series instrument clusters host. The factory navigation
// draws its guidance there as vector items. Pure: the function list, payload builders and shape checks, the call a
// request is built from, and the filter for the cluster's copies of our answers. Host-tested in tests/test_hudterm.cpp.
// The transport is cluster/hud_terminal.hpp; what to draw on a given cluster is its driver's profile.
//
// From the head unit's NavNBTEvoController (Y24) and the MOST library, checked by drawing on a 6WA (F20, S19):
//   - every call is a MOST method sent as StartResultAck (op 6). The first parameter is a u16 handle, the instance
//     id; the rest are the method's parameters, big-endian. The cluster answers with ResultAck (op D), echoing the
//     payload.
//   - the cluster sends that answer to the caller and a copy to the factory navigation's address. The copy is not the
//     navigation drawing: RecentCalls below tells them apart.
//   - TextareaSetText: handle, byte 00 (encoding), UTF-16BE characters, then 00 00 (CHBParaString::writeToStream 0x11c238
//     and write2UnicodeString 0x11b994 in libimp_sys_most_base_nbt). The cluster's answer leaves out the 00 00.
//   - ImageAddPolygon: handle, u16 item index, u8 type (2 polyline, 3 filled), u8 count (1..99), then count x (s16 x,
//     s16 y). The cluster's interpreter refuses more than 99 points (F010 KOMBI-L6 firmware).
//   - circles (21E, and C09, which the head unit's builder 0x93d18c uses): handle, u16 index, u8 type, s16 centre x, s16
//     centre y, u16 size, s32 start and s32 end angle in tenths of a degree: 19 bytes.
//   - line colour, line width, fill colour and fill mode: handle and one byte. They are held per image and copied into
//     each item as it is added, so they stay set after the call. Fill colour is a colour index, not RGB.
//   - IsVisible (205) got no answer on the car, so it is not in the send list.
//
// Hard limits, checked when a call is built rather than trusted to the caller: FBlock 82 instance 00, op
// StartResultAck, to the registry's host of 82/00, which must be a ring node (0x0100..0x0FFF); only the functions in
// payloadShapeOk(), each with its exact payload shape; only the handles the cluster's profile allows.

#include "most/ids.hpp"
#include "most/msg.hpp"
#include "most/registry.hpp"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

namespace nbt {
namespace hudterm {

const uint8_t kFBlock = 0x82;
const uint8_t kInst = 0x00;

enum Fkt {
    kSetVisibility = 0x204, kIsVisible = 0x205, kSwitchVisibility = 0x20E, kTextareaSetText = 0x210,
    kSetLineColor = 0x218, kSetLineWidth = 0x219, kSetFillColor = 0x21B, kSetFillMode = 0x21C, kAddPolygon = 0x21D,
    kAddCircle = 0x21E, kRemoveAllItems = 0x220, kGetSize = 0xC04, kAddCircleDiameter = 0xC09
};
enum PolyType { kPolyline = 2, kPolygon = 3 };
enum { kMaxPoints = 99, kMaxTextChars = 32, kMaxHandle = 255 };
const int32_t kMaxAngle = 7200;   // tenths of a degree; the cluster wraps modulo 3600

inline bool isRingNode(uint16_t a) { return a >= 0x0100 && a <= 0x0FFF; }
inline bool isAppAddress(uint16_t a) { return a >= 0x2000 && a <= 0x2FFF; }

inline const char* fktName(uint16_t f) {
    switch (f) {
        case 0x200: return "CreateInstance";
        case 0x203: return "ReleaseInstance";
        case kSetVisibility: return "SetVisibility";
        case kIsVisible: return "IsVisible";
        case kSwitchVisibility: return "SwitchVisibility";
        case kTextareaSetText: return "TextareaSetText";
        case 0x211: return "KOMBITextareaSetFormat";
        case 0x215: return "ImageSetRotation";
        case kSetLineColor: return "ImageSetLineColor";
        case kSetLineWidth: return "ImageSetLineWidth";
        case kSetFillColor: return "ImageSetFillColor";
        case kSetFillMode: return "ImageSetFillMode";
        case kAddPolygon: return "ImageAddPolygon";
        case kAddCircle: return "ImageAddCircle";
        case 0x21F: return "ImageRemoveItem";
        case kRemoveAllItems: return "ImageRemoveAllItems";
        case 0x226: return "PotentiometerSetType";
        case 0x227: return "PotentiometerSetValue";
        case 0x229: return "PotentiometerSetRange";
        case 0x22B: return "FrameAddItem";
        case 0x300: return "TerminalInterface";
        case 0xC03: return "ImageSetTextareaFormat";
        case kGetSize: return "ImageGetSize";
        case 0xC05: return "ImageAddBitmap";
        case 0xC06: return "ImageSetPositionRotation";
        case 0xC07: return "AnimationSetGuidingStep";
        case 0xC08: return "CurrentGuidingStep";
        case kAddCircleDiameter: return "ImageAddCircleDiameter";
    }
    return "?";
}

inline uint16_t rdBe16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }
inline int32_t rdBe32(const uint8_t* p) {
    return (int32_t)(((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]);
}
inline void putBe16(std::vector<uint8_t>& v, uint16_t x) { v.push_back((uint8_t)(x >> 8)); v.push_back((uint8_t)x); }
inline void putBe32(std::vector<uint8_t>& v, int32_t x) {
    uint32_t u = (uint32_t)x;
    v.push_back((uint8_t)(u >> 24)); v.push_back((uint8_t)(u >> 16)); v.push_back((uint8_t)(u >> 8)); v.push_back((uint8_t)u);
}

// The host of 82/00 in a registry, or 0 if there is none or it is not a ring node.
inline uint16_t terminalHost(const std::vector<most::RegistryEntry>& reg) {
    const most::RegistryEntry* e = most::findRegistryEntry(reg, most::registryKey(kFBlock, kInst));
    return (e && isRingNode(e->address)) ? e->address : 0;
}

// The handles (instances) a cluster's profile lets us use. Handle 0 is never allowed.
class HandleSet {
public:
    HandleSet() { memset(on_, 0, sizeof(on_)); }
    void allow(int h) { if (h > 0 && h <= kMaxHandle) on_[h] = true; }
    bool has(int h) const { return h > 0 && h <= kMaxHandle && on_[h]; }
private:
    bool on_[kMaxHandle + 1];
};

// ---- payloads ----

struct Point { int16_t x, y; };

inline std::vector<uint8_t> handleAnd(uint16_t h, uint8_t b) {
    std::vector<uint8_t> p; putBe16(p, h); p.push_back(b); return p;
}
inline std::vector<uint8_t> setVisibility(uint16_t h, bool on) { return handleAnd(h, on ? 1 : 0); }
inline std::vector<uint8_t> switchVisibility(uint16_t hide, uint16_t show) {
    std::vector<uint8_t> p; putBe16(p, hide); putBe16(p, show); return p;
}
inline std::vector<uint8_t> removeAllItems(uint16_t h) { std::vector<uint8_t> p; putBe16(p, h); return p; }
inline std::vector<uint8_t> getSize(uint16_t h) { std::vector<uint8_t> p; putBe16(p, h); return p; }
inline std::vector<uint8_t> setFillColor(uint16_t h, uint8_t colour) { return handleAnd(h, colour); }
inline std::vector<uint8_t> setLineColor(uint16_t h, uint8_t colour) { return handleAnd(h, colour); }
inline std::vector<uint8_t> setLineWidth(uint16_t h, uint8_t width) { return handleAnd(h, width); }
inline std::vector<uint8_t> setFillMode(uint16_t h, uint8_t mode) { return handleAnd(h, mode); }

// Empty when there are no points or more than kMaxPoints.
inline std::vector<uint8_t> addPolygon(uint16_t h, uint16_t index, PolyType type, const std::vector<Point>& pts) {
    std::vector<uint8_t> p;
    if (pts.empty() || pts.size() > kMaxPoints) return p;
    putBe16(p, h); putBe16(p, index); p.push_back((uint8_t)type); p.push_back((uint8_t)pts.size());
    for (size_t i = 0; i < pts.size(); ++i) { putBe16(p, (uint16_t)pts[i].x); putBe16(p, (uint16_t)pts[i].y); }
    return p;
}

// For 21E and C09 alike. Empty when an angle is out of range.
inline std::vector<uint8_t> addCircle(uint16_t h, uint16_t index, PolyType type, int16_t cx, int16_t cy, uint16_t size,
                                      int32_t a0, int32_t a1) {
    std::vector<uint8_t> p;
    if (a0 < -kMaxAngle || a0 > kMaxAngle || a1 < -kMaxAngle || a1 > kMaxAngle) return p;
    putBe16(p, h); putBe16(p, index); p.push_back((uint8_t)type);
    putBe16(p, (uint16_t)cx); putBe16(p, (uint16_t)cy); putBe16(p, size);
    putBe32(p, a0); putBe32(p, a1);
    return p;
}

// The firmware's text format: handle, 00, UTF-16BE characters, 00 00. Empty when the text is longer than kMaxTextChars
// or not plain printable ASCII; which characters a cluster's font can show is its driver's business. An empty string is
// a valid call: it clears the text.
inline std::vector<uint8_t> setText(uint16_t h, const char* s) {
    std::vector<uint8_t> p;
    size_t n = strlen(s);
    if (n > kMaxTextChars) return p;
    for (size_t i = 0; i < n; ++i) if (s[i] < 0x20 || s[i] >= 0x7f) return p;
    putBe16(p, h);
    p.push_back(0x00);
    for (size_t i = 0; i < n; ++i) { p.push_back(0); p.push_back((uint8_t)s[i]); }
    p.push_back(0); p.push_back(0);
    return p;
}

// UTF-8 -> text setText() takes and the cluster's font can show. Letters, digits and spaces are kept; Latin-1 and
// Latin Extended-A letters become their plain letters (German umlauts and sharp s the German way: ae, oe, ue, ss);
// the characters in `punct` are kept; a comma becomes a dot when commaToDot (a decimal comma in a distance); anything
// else becomes a space. Runs of spaces become one, the ends are trimmed, and the result is cut to maxChars.
// Which characters a cluster's font has is its profile's business: a comma made the 6WA show nothing at all.
inline std::string toClusterText(const std::string& utf8, const char* punct, size_t maxChars, bool commaToDot) {
    // U+00C0..U+017F, one entry per code point.
    static const char* const kLatin =
        "A|A|A|A|Ae|A|AE|C|E|E|E|E|I|I|I|I|D|N|O|O|O|O|Oe| |O|U|U|U|Ue|Y|Th|ss|"
        "a|a|a|a|ae|a|ae|c|e|e|e|e|i|i|i|i|d|n|o|o|o|o|oe| |o|u|u|u|ue|y|th|y|"
        "A|a|A|a|A|a|C|c|C|c|C|c|C|c|D|d|D|d|E|e|E|e|E|e|E|e|E|e|G|g|G|g|"
        "G|g|G|g|H|h|H|h|I|i|I|i|I|i|I|i|I|i|IJ|ij|J|j|K|k|k|L|l|L|l|L|l|L|"
        "l|L|l|N|n|N|n|N|n|n|N|n|O|o|O|o|O|o|OE|oe|R|r|R|r|R|r|S|s|S|s|S|s|"
        "S|s|T|t|T|t|T|t|U|u|U|u|U|u|U|u|U|u|U|u|W|w|Y|y|Y|Z|z|Z|z|Z|z|s";
    std::string raw;
    size_t i = 0;
    while (i < utf8.size()) {
        unsigned char c = (unsigned char)utf8[i];
        uint32_t cp;
        size_t k = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 1;
        if (i + k > utf8.size()) k = 1;
        cp = k == 1 ? c : k == 2 ? (c & 0x1Fu) : k == 3 ? (c & 0x0Fu) : (c & 0x07u);
        for (size_t j = 1; j < k; ++j) cp = (cp << 6) | ((unsigned char)utf8[i + j] & 0x3Fu);
        if (k == 1 && c >= 0x80) cp = 0xFFFD;                         // a stray continuation or bad lead byte
        i += k;
        if ((cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') || (cp >= '0' && cp <= '9') || cp == ' ') {
            raw.push_back((char)cp);
        } else if (cp == ',' && commaToDot) {
            raw.push_back('.');
        } else if (cp > ' ' && cp < 0x7F && punct && strchr(punct, (int)cp)) {
            raw.push_back((char)cp);
        } else if (cp >= 0xC0 && cp <= 0x17F) {
            const char* p = kLatin;
            for (uint32_t n = cp - 0xC0; n; --n) p = strchr(p, '|') + 1;
            while (*p && *p != '|') raw.push_back(*p++);
        } else {
            raw.push_back(' ');
        }
    }
    std::string out;
    for (size_t j = 0; j < raw.size(); ++j) {
        if (raw[j] == ' ' && (out.empty() || out[out.size() - 1] == ' ')) continue;
        out.push_back(raw[j]);
    }
    if (out.size() > maxChars) out.resize(maxChars);
    while (!out.empty() && out[out.size() - 1] == ' ') out.resize(out.size() - 1);
    return out;
}

// The ASCII text of a TextareaSetText payload (others as '?'), with or without the terminator; "" if it is not one.
inline std::string decodeText(const uint8_t* p, size_t n) {
    std::string s;
    if (n < 3 || p[2] != 0x00 || (n - 3) % 2) return s;
    for (size_t i = 3; i + 1 < n; i += 2) {
        uint16_t ch = (uint16_t)((p[i] << 8) | p[i + 1]);
        if (ch == 0) break;
        s.push_back(ch >= 0x20 && ch < 0x7f ? (char)ch : '?');
    }
    return s;
}

// The shape each function we send must have. Anything else, IsVisible included, is refused.
inline bool payloadShapeOk(uint16_t fkt, const std::vector<uint8_t>& p) {
    size_t n = p.size();
    switch (fkt) {
        case kSetVisibility: return n == 3 && p[2] <= 1;
        case kRemoveAllItems: case kGetSize: return n == 2;
        case kSwitchVisibility: return n == 4;
        case kSetFillColor: case kSetLineColor: case kSetLineWidth: case kSetFillMode: return n == 3;
        case kAddCircle: case kAddCircleDiameter: {
            if (n != 19 || (p[4] != kPolyline && p[4] != kPolygon)) return false;
            int32_t a0 = rdBe32(&p[11]), a1 = rdBe32(&p[15]);
            return a0 >= -kMaxAngle && a0 <= kMaxAngle && a1 >= -kMaxAngle && a1 <= kMaxAngle;
        }
        case kTextareaSetText:
            return n >= 5 && (n - 3) % 2 == 0 && n <= 3 + 2 * (kMaxTextChars + 1) && p[2] == 0x00 && p[n - 1] == 0 &&
                   p[n - 2] == 0;
        case kAddPolygon:
            return n >= 10 && (p[4] == kPolyline || p[4] == kPolygon) && p[5] >= 1 && p[5] <= kMaxPoints &&
                   n == 6 + 4 * (size_t)p[5];
    }
    return false;
}

// ---- calls ----

struct Call {
    uint16_t fkt;
    std::vector<uint8_t> payload;
    Call() : fkt(0) {}
    Call(uint16_t f, const std::vector<uint8_t>& p) : fkt(f), payload(p) {}
    uint16_t handle() const { return payload.size() >= 2 ? rdBe16(&payload[0]) : 0; }
};

// Whether a call keeps every limit: the shape, its handle, and for SwitchVisibility the second handle too.
inline bool callOk(const Call& c, const HandleSet& allowed) {
    if (!payloadShapeOk(c.fkt, c.payload) || !allowed.has(c.handle())) return false;
    if (c.fkt == kSwitchVisibility && !allowed.has(rdBe16(&c.payload[2]))) return false;
    return true;
}

// The MOST message for one call, without the sender address: most::Client::send fills that in from its registration.
// False, with out untouched, when a limit is broken.
inline bool buildCall(uint16_t host, const Call& c, uint32_t tag, const HandleSet& allowed, most::Msg& out) {
    if (!isRingNode(host) || !callOk(c, allowed)) return false;
    out.txType = 0;
    out.varTag = 1;
    out.varVal = 0;
    out.dst = host;
    out.src = 0;
    out.fblock = kFBlock;
    out.inst = kInst;
    out.fkt = c.fkt;
    out.opType = most::kOpStartResultAck;
    out.hdrTag = tag;
    out.data = c.payload;
    return true;
}

// One call for the log: "21D ImageAddPolygon h5 #0 polygon 7 pts x 70..130 y 20..120", "210 TextareaSetText h3 '250'".
inline std::string describeCall(const Call& c) {
    char b[160];
    const std::vector<uint8_t>& p = c.payload;
    size_t n = p.size();
    int k = snprintf(b, sizeof(b), "%03X %s h%u", c.fkt, fktName(c.fkt), c.handle());
    if (k < 0 || k >= (int)sizeof(b)) return b;
    char* e = b + k;
    size_t room = sizeof(b) - (size_t)k;
    if (c.fkt == kSetVisibility && n == 3) {
        snprintf(e, room, " %s", p[2] ? "on" : "off");
    } else if (c.fkt == kSwitchVisibility && n == 4) {
        snprintf(e, room, " hidden, h%u shown", rdBe16(&p[2]));
    } else if (c.fkt == kTextareaSetText) {
        snprintf(e, room, " '%s'", decodeText(&p[0], n).c_str());
    } else if ((c.fkt == kSetFillColor || c.fkt == kSetLineColor || c.fkt == kSetLineWidth || c.fkt == kSetFillMode) &&
               n == 3) {
        snprintf(e, room, " %u", p[2]);
    } else if (c.fkt == kAddPolygon && n >= 6 && n == 6 + 4 * (size_t)p[5] && p[5]) {
        int x0 = 32767, y0 = 32767, x1 = -32768, y1 = -32768;
        for (size_t i = 6; i + 3 < n; i += 4) {
            int x = (int16_t)rdBe16(&p[i]), y = (int16_t)rdBe16(&p[i + 2]);
            if (x < x0) x0 = x;
            if (x > x1) x1 = x;
            if (y < y0) y0 = y;
            if (y > y1) y1 = y;
        }
        snprintf(e, room, " #%u %s %u pts x %d..%d y %d..%d", rdBe16(&p[2]), p[4] == kPolygon ? "polygon" : "polyline",
                 p[5], x0, x1, y0, y1);
    } else if ((c.fkt == kAddCircle || c.fkt == kAddCircleDiameter) && n == 19) {
        snprintf(e, room, " #%u centre %d,%d size %u, %d..%d", rdBe16(&p[2]), (int16_t)rdBe16(&p[5]),
                 (int16_t)rdBe16(&p[7]), rdBe16(&p[9]), (int)rdBe32(&p[11]), (int)rdBe32(&p[15]));
    } else if (c.fkt != kRemoveAllItems && c.fkt != kGetSize) {
        snprintf(e, room, " %u bytes", (unsigned)n);
    }
    return b;
}

// ---- the cluster's copies of our answers ----

// The cluster answers each of our calls twice: to us, and with the same function and handle to the factory
// navigation's address, in the same millisecond (car, S19). The copy is matched on function and handle within the
// reply timeout, not on the whole payload: the answer to a text leaves out the 00 00 we send. A real navigation answer
// that happens to share a function and handle with one of our calls in that window is taken for a copy; the navigation
// sends many calls at the start of a route, so the next one is seen.
class RecentCalls {
public:
    enum { kSlots = 64 };
    explicit RecentCalls(uint32_t windowMs = 1500) : windowMs_(windowMs), next_(0) {
        for (int i = 0; i < kSlots; ++i) used_[i] = false;
    }
    void add(uint16_t fkt, uint16_t handle, uint32_t now) {
        fkt_[next_] = fkt; handle_[next_] = handle; t_[next_] = now; used_[next_] = true;
        next_ = (next_ + 1) % kSlots;
    }
    bool isCopy(uint16_t fkt, const uint8_t* data, size_t len, uint32_t now) const {
        if (len < 2) return false;
        uint16_t h = rdBe16(data);
        for (int i = 0; i < kSlots; ++i)
            if (used_[i] && fkt_[i] == fkt && handle_[i] == h && now - t_[i] <= windowMs_) return true;
        return false;
    }
    void clear() { for (int i = 0; i < kSlots; ++i) used_[i] = false; }
private:
    uint32_t windowMs_;
    uint16_t fkt_[kSlots];
    uint16_t handle_[kSlots];
    uint32_t t_[kSlots];
    bool used_[kSlots];
    int next_;
};

} // namespace hudterm
} // namespace nbt

#endif
