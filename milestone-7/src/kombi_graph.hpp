// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// kombi_nav: the KOMBI route-guidance graph (BMW.IC.Guidance, SOME/IP service 0xB006), header-only, no I/O.
//
// Template-patch design: every frame is a byte copy of the captured factory route start (kombi_graph_template.hpp)
// and only the fields proven on the car are changed, so the KOMBI always sees a self-consistent graph:
//   - instance handle         body[0:4]   (0xC8 on every boot seen; NBT_KOMBI_NAV_HANDLE overrides)
//   - ManeuverDescription 0x0F direction enum at body[33]
//   - ManeuverDistance    0x1D UTF-16BE display text at the FEFF BOM, the unit byte after its NUL (0x01 mi,
//                              0x05 ft), the red distance bar percentage at body[22] (255 = hidden)
//   - TextResource        0x03 UTF-16BE road name at the FEFF BOM
// Only maneuver 0 (the next one) is patched. Frame body layout: [0:4] handle, [4:8] model id (<type><index24>),
// [8] model type, [9:13] payload length (= body length - 17), [13:17] tag, [17:] payload.
#ifndef KOMBI_GRAPH_HPP
#define KOMBI_GRAPH_HPP

#include <stdint.h>
#include <stddef.h>
#include <string>
#include <vector>

#include "kombi_graph_template.hpp"

namespace nbt {
namespace kombinav {

typedef std::vector<uint8_t> Bytes;

struct Frame {
    uint16_t method;
    Bytes body;
};

// SOME/IP methods kombi_nav may send. Never 0x0001 (create / dispose): the factory owns the instance.
inline bool methodAllowed(uint16_t m) {
    switch (m) {
        case 0x0002: case 0x0003: case 0x0004: case 0x0009: case 0x000A:
        case 0x000B: case 0x000C: case 0x000E: case 0x0010: return true;
        default: return false;
    }
}

inline void putU32(Bytes& b, size_t off, uint32_t v) {
    b[off] = (uint8_t)(v >> 24); b[off + 1] = (uint8_t)(v >> 16);
    b[off + 2] = (uint8_t)(v >> 8); b[off + 3] = (uint8_t)v;
}
inline uint32_t getU32(const Bytes& b, size_t off) {
    return ((uint32_t)b[off] << 24) | ((uint32_t)b[off + 1] << 16) | ((uint32_t)b[off + 2] << 8) | b[off + 3];
}
inline uint8_t modelType(const Bytes& b) { return b.size() > 8 ? b[8] : 0; }
inline uint32_t modelIndex(const Bytes& b) { return b.size() >= 8 ? (getU32(b, 4) & 0xFFFFFF) : 0; }

// UTF-8 -> UTF-16BE (surrogate pairs above U+FFFF, U+FFFD for invalid bytes).
inline Bytes utf16be(const std::string& s) {
    Bytes o;
    size_t i = 0;
    while (i < s.size()) {
        uint8_t c = (uint8_t)s[i];
        uint32_t cp = 0xFFFD;
        size_t n = 1;
        if (c < 0x80) cp = c;
        else if ((c & 0xE0) == 0xC0) { n = 2; cp = c & 0x1F; }
        else if ((c & 0xF0) == 0xE0) { n = 3; cp = c & 0x0F; }
        else if ((c & 0xF8) == 0xF0) { n = 4; cp = c & 0x07; }
        if (n > 1) {
            if (i + n > s.size()) { cp = 0xFFFD; n = 1; }
            else for (size_t k = 1; k < n; ++k) {
                uint8_t d = (uint8_t)s[i + k];
                if ((d & 0xC0) != 0x80) { cp = 0xFFFD; n = 1; break; }
                cp = (cp << 6) | (d & 0x3F);
            }
        }
        i += n;
        if (cp >= 0x10000) {
            cp -= 0x10000;
            uint16_t hi = (uint16_t)(0xD800 | (cp >> 10)), lo = (uint16_t)(0xDC00 | (cp & 0x3FF));
            o.push_back((uint8_t)(hi >> 8)); o.push_back((uint8_t)hi);
            o.push_back((uint8_t)(lo >> 8)); o.push_back((uint8_t)lo);
        } else {
            o.push_back((uint8_t)(cp >> 8)); o.push_back((uint8_t)cp);
        }
    }
    return o;
}

// Replace the UTF-16BE string at the first FEFF BOM (searched from body[18]). The length byte sits just before the
// BOM: 2 (BOM) + 2 * chars + 2 (NUL). *end = offset right after the new string's NUL. False if there is no string or
// the text doesn't fit the one-byte length.
inline bool patchText(Bytes& b, const std::string& text, size_t* end) {
    for (size_t i = 18; i + 1 < b.size(); ++i) {
        if (b[i] != 0xFE || b[i + 1] != 0xFF) continue;
        size_t slen_off = i - 1, tail_off = i + b[slen_off];
        if (tail_off > b.size()) return false;
        Bytes enc = utf16be(text);
        if (2 + enc.size() + 2 > 255) return false;
        Bytes nb(b.begin(), b.begin() + slen_off);
        nb.push_back((uint8_t)(2 + enc.size() + 2));
        nb.push_back(0xFE); nb.push_back(0xFF);
        nb.insert(nb.end(), enc.begin(), enc.end());
        nb.push_back(0); nb.push_back(0);
        nb.insert(nb.end(), b.begin() + tail_off, b.end());
        b.swap(nb);
        putU32(b, 9, (uint32_t)(b.size() - 17));
        if (end) *end = i + 2 + enc.size() + 2;
        return true;
    }
    return false;
}

class Graph {
public:
    // bar_pct: red distance bar in every maneuver-0 distance frame we send; 255 = hidden, -1 = the template's value.
    explicit Graph(uint32_t handle = 0xC8, int bar_pct = 255) : handle_(handle), bar_pct_(bar_pct) {}

    uint32_t handle() const { return handle_; }

    // Full route-start graph (97 frames, factory order) with maneuver 0 patched. dist "" = keep the template's text;
    // unit -1 = keep the template's unit byte. False = the template doesn't have the expected layout (never send).
    bool start(int direction, const std::string& road, const std::string& dist, int unit,
               std::vector<Frame>& out) const {
        out.clear();
        out.reserve(tmpl::phase_b_count);
        for (size_t i = 0; i < tmpl::phase_b_count; ++i) {
            Frame f;
            f.method = tmpl::phase_b[i].method;
            f.body.assign(tmpl::phase_b[i].body, tmpl::phase_b[i].body + tmpl::phase_b[i].len);
            if (!patch(f, direction, road, dist, unit)) return false;
            out.push_back(f);
        }
        return true;
    }

    // One in-place distance update: the last maneuver-0 ManeuverDistance frame of the template, patched.
    bool distance(const std::string& dist, int unit, std::vector<Frame>& out) const {
        out.clear();
        for (size_t i = tmpl::phase_b_count; i-- > 0;) {
            if (tmpl::phase_b[i].method != 0x000A) continue;
            Frame f;
            f.method = 0x000A;
            f.body.assign(tmpl::phase_b[i].body, tmpl::phase_b[i].body + tmpl::phase_b[i].len);
            if (modelType(f.body) != 29 || modelIndex(f.body) != 0) continue;
            if (!patch(f, 0, std::string(), dist, unit)) return false;
            out.push_back(f);
            return true;
        }
        return false;
    }

    // The factory cancel: guidance state 7 -> 5 -> 0, empty maneuver list, init, destination clear.
    void cancel(std::vector<Frame>& out) const {
        out.clear();
        for (size_t i = 0; i < tmpl::cancel_count; ++i) out.push_back(copy(tmpl::cancel[i]));
    }

    // Fallback when the full cancel is refused: only the "guidance state 0" frame.
    void cancelState0(std::vector<Frame>& out) const {
        out.clear();
        for (size_t i = 0; i < tmpl::cancel_count; ++i) {
            Frame f = copy(tmpl::cancel[i]);
            if (f.method == 0x0009 && f.body.size() > 30 && f.body[30] == 0) out.push_back(f);
        }
    }

private:
    Frame copy(const tmpl::Frame& t) const {
        Frame f;
        f.method = t.method;
        f.body.assign(t.body, t.body + t.len);
        putU32(f.body, 0, handle_);
        return f;
    }

    bool patch(Frame& f, int direction, const std::string& road, const std::string& dist, int unit) const {
        Bytes& b = f.body;
        putU32(b, 0, handle_);
        if ((f.method != 0x000A && f.method != 0x0003) || modelIndex(b) != 0) return true;
        uint8_t t = modelType(b);
        if (f.method == 0x000A && t == 15) {                      // ManeuverDescription: direction
            if (b.size() <= 33 || b[21] != 15) return false;
            b[33] = (uint8_t)direction;
        } else if (f.method == 0x000A && t == 29) {               // ManeuverDistance: bar, text, unit
            if (bar_pct_ >= 0) {
                if (b.size() <= 22 || b[21] != 29 || (b[22] != 0 && b[22] != 100 && b[22] != bar_pct_)) return false;
                b[22] = (uint8_t)bar_pct_;
            }
            if (!dist.empty()) {
                size_t end = 0;
                if (!patchText(b, dist, &end)) return false;
                if (unit >= 0) {
                    if (end >= b.size() || (b[end] != 0x01 && b[end] != 0x05)) return false;
                    b[end] = (uint8_t)unit;
                }
            }
        } else if (f.method == 0x0003 && t == 3 && !road.empty()) {  // TextResource: road name
            if (!patchText(b, road, NULL)) return false;
        }
        return true;
    }

    uint32_t handle_;
    int bar_pct_;
};

// SOME/IP request: service 0xB006, method, length = 8 + body, client, session, protocol 1, interface 1, REQUEST, E_OK.
inline Bytes someipRequest(uint16_t method, const Bytes& body, uint16_t client, uint16_t session) {
    Bytes h(16);
    uint32_t len = (uint32_t)(8 + body.size());
    h[0] = 0xB0; h[1] = 0x06; h[2] = (uint8_t)(method >> 8); h[3] = (uint8_t)method;
    putU32(h, 4, len);
    h[8] = (uint8_t)(client >> 8); h[9] = (uint8_t)client; h[10] = (uint8_t)(session >> 8); h[11] = (uint8_t)session;
    h[12] = 1; h[13] = 1; h[14] = 0; h[15] = 0;
    h.insert(h.end(), body.begin(), body.end());
    return h;
}

} // namespace kombinav
} // namespace nbt

#endif
