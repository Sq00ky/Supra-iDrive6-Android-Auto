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
// With lanes (start() given KombiLanes): maneuver 0 gets ONE junction lane model 0x1B000000 (method 0x0010
// setLaneGuidingData) right before each of its 0x0F nodes, which then list it; the template's own lane models are
// dropped (93 frames instead of 97). position() is the 0x11 destination-info update the KOMBI re-checks lanes on.
// Only maneuver 0 (the next one) is patched. Frame body layout: [0:4] handle, [4:8] model id (<type><index24>),
// [8] model type, [9:13] payload length (= body length - 17), [13:17] tag, [17:] payload.
#ifndef KOMBI_GRAPH_HPP
#define KOMBI_GRAPH_HPP

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <string>
#include <vector>

#include "kombi_graph_template.hpp"
#include "kombi_nav_msg.hpp"

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


inline void putF64(Bytes& b, size_t off, double v) {
    uint64_t u;
    memcpy(&u, &v, 8);
    for (int i = 0; i < 8; ++i) b[off + i] = (uint8_t)(u >> (56 - 8 * i));
}
inline double getF64(const uint8_t* p) {
    uint64_t u = 0;
    for (int i = 0; i < 8; ++i) u = (u << 8) | p[i];
    double v;
    memcpy(&v, &u, 8);
    return v;
}
inline void appendU32(Bytes& b, uint32_t v) {
    b.push_back((uint8_t)(v >> 24)); b.push_back((uint8_t)(v >> 16)); b.push_back((uint8_t)(v >> 8)); b.push_back((uint8_t)v);
}
inline void appendF64(Bytes& b, double v) {
    size_t at = b.size();
    b.resize(at + 8);
    putF64(b, at, v);
}

// ---- the KOMBI lane model 0x1B (method 0x0010), decoded from 126 factory frames (every one re-encodes byte-identically) -
//   body    = handle u32 | model id u32 (0x1B000000 | idx) | 0x1B | u32 len(body) - 17 | u32 1 | model id u32 | 0x1B
//             | u32 n | n bytes of lanes | u32 m | m boundary bytes (one per lane + a 0) | double d0 | double d1
//             | double d2 | 0x00
//   lane    = 1 (valid) | kind (1 take, 2 other) | 1 | u32 k + k arrows (factory: always 1) | u32 h + h bytes (factory:
//             only far from the junction; we send none) | u32 0
//   d0 = d1 = route position (metres to the destination, the 0x1D double's frame) where the section starts, d2 = its
//   length: the junction section ends exactly at the maneuver's own position (d0 - d2).
// The KOMBI (HmiAggrNaviLaneGuiding::SetRicData) shows the lane row while the car's route position from the last 0x11
// destination info lies inside the current maneuver's sections, re-checked on every 0x11 (so one must follow the model).
const uint32_t kLaneModel0 = 0x1B000000;
const uint8_t kLaneType = 0x1B;
const double kLaneSectionM = 120.0;   // fallback span when the template's car position isn't before maneuver 0
const double kLaneMarginM = 100.0;    // the section also starts this far before the car position we claim

// Boundary byte left of each lane (+ the 0 terminator): 0 at the road edge and between lanes with different arrows, 2
// between lanes with the same arrow (factory junction sections; AA has no lane-line data).
inline Bytes laneMarks(const KombiLanes& l) {
    Bytes o;
    for (size_t k = 0; k < l.size(); ++k) o.push_back(k == 0 || l[k].arrow != l[k - 1].arrow ? 0 : 2);
    o.push_back(0);
    return o;
}

inline Bytes encodeLaneModel(uint32_t handle, uint32_t mid, const KombiLanes& l, double d0, double d1, double d2) {
    Bytes arr;
    for (size_t i = 0; i < l.size(); ++i) {
        arr.push_back(1); arr.push_back(l[i].kind); arr.push_back(1);
        appendU32(arr, 1); arr.push_back(l[i].arrow);
        appendU32(arr, 0);
        appendU32(arr, 0);
    }
    Bytes marks = laneMarks(l), body;
    appendU32(body, mid); body.push_back(kLaneType);
    appendU32(body, (uint32_t)arr.size()); body.insert(body.end(), arr.begin(), arr.end());
    appendU32(body, (uint32_t)marks.size()); body.insert(body.end(), marks.begin(), marks.end());
    appendF64(body, d0); appendF64(body, d1); appendF64(body, d2);
    body.push_back(0);
    Bytes out;
    appendU32(out, handle); appendU32(out, mid); out.push_back(kLaneType);
    appendU32(out, (uint32_t)body.size()); appendU32(out, 1);
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

// A 0x0F maneuver node with its lane-model id list (body[54:58] = byte length, ids follow) replaced. False = layout.
inline bool withLaneRefs(Bytes& b, const std::vector<uint32_t>& refs) {
    if (modelType(b) != 15 || b.size() < 62 || b[21] != 15) return false;
    uint32_t n = getU32(b, 54);
    if (n % 4 || b.size() < 58 + (size_t)n + 4) return false;
    Bytes o(b.begin(), b.begin() + 54);
    appendU32(o, (uint32_t)(4 * refs.size()));
    for (size_t i = 0; i < refs.size(); ++i) appendU32(o, refs[i]);
    o.insert(o.end(), b.begin() + 58 + n, b.end());
    putU32(o, 9, (uint32_t)(o.size() - 17));
    b.swap(o);
    return true;
}

class Graph {
public:
    // bar_pct: red distance bar in every maneuver-0 distance frame we send; 255 = hidden, -1 = the template's value.
    explicit Graph(uint32_t handle = 0xC8, int bar_pct = 255)
        : handle_(handle), bar_pct_(bar_pct), pos_frame_(-1), template_pos_(0), has_anchor_(false), anchor_m0_(0),
          anchor_span_(0) {
        // the template's car position: its last 0x11 destination info with a non-zero route position (4132 m)
        for (size_t i = 0; i < tmpl::phase_b_count; ++i) {
            const tmpl::Frame& t = tmpl::phase_b[i];
            if (t.method != 0x0009 || t.len < 30 || t.body[8] != 17) continue;
            double p = getF64(t.body + 22);
            if (p > 0) { pos_frame_ = (int)i; template_pos_ = p; }
        }
    }

    uint32_t handle() const { return handle_; }

    // Full route-start graph (97 frames, factory order) with maneuver 0 patched. dist "" = keep the template's text;
    // unit -1 = keep the template's unit byte. False = the template doesn't have the expected layout (never send).
    // lanes (optional, non-empty): maneuver 0's lane row (see the lane model above).
    bool start(int direction, const std::string& road, const std::string& dist, int unit,
               std::vector<Frame>& out, const KombiLanes* lanes = NULL) const {
        out.clear();
        out.reserve(tmpl::phase_b_count);
        for (size_t i = 0; i < tmpl::phase_b_count; ++i) {
            Frame f;
            f.method = tmpl::phase_b[i].method;
            f.body.assign(tmpl::phase_b[i].body, tmpl::phase_b[i].body + tmpl::phase_b[i].len);
            if (!patch(f, direction, road, dist, unit)) return false;
            out.push_back(f);
        }
        return lanes && !lanes->empty() ? withLanes(out, *lanes) : true;
    }

    // One 0x11 destination-info update (method 0x0009, the template's frame) carrying the car's route position.
    bool position(double pos_m, std::vector<Frame>& out) const {
        out.clear();
        if (pos_frame_ < 0) return false;
        Frame f = copy(tmpl::phase_b[pos_frame_]);
        putF64(f.body, 22, pos_m);
        out.push_back(f);
        return true;
    }

    // Route position for the lane feed: maneuver 0 + the metres to it, clamped into the last lane graph's section (so
    // it never leaves it, and d = position - maneuver stays <= the template's 180 m band). False before any lane graph.
    bool feedPosition(bool has_step, double step_m, double& pos) const {
        if (!has_anchor_) return false;
        double lo = 0.0;
        if (!has_step) { pos = anchor_m0_ + anchor_span_; return true; }
        double d = step_m < lo ? lo : step_m;
        if (d > anchor_span_) d = anchor_span_;
        pos = anchor_m0_ + d;
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
    bool withLanes(std::vector<Frame>& frames, const KombiLanes& lanes) const {
        std::vector<Frame> out;
        out.reserve(frames.size() + 4);
        bool has_pos = false;
        double pos = 0;
        std::vector<uint32_t> ours(1, kLaneModel0), none;
        for (size_t i = 0; i < frames.size(); ++i) {
            Frame& f = frames[i];
            if (f.method == 0x0010) continue;                          // the template's own lane models
            if (f.method == 0x000A && modelType(f.body) == 29 && modelIndex(f.body) == 0 && f.body.size() >= 31) {
                pos = getF64(&f.body[23]);
                has_pos = true;
            }
            if (f.method == 0x000A && modelType(f.body) == 15) {
                if (modelIndex(f.body) == 0) {
                    if (!has_pos) return false;
                    double d0, d2;
                    laneSection(pos, d0, d2);
                    Frame lm;
                    lm.method = 0x0010;
                    lm.body = encodeLaneModel(handle_, kLaneModel0, lanes, d0, d0, d2);
                    out.push_back(lm);
                    if (!withLaneRefs(f.body, ours)) return false;
                } else if (f.body.size() >= 58 && getU32(f.body, 54)) {
                    if (!withLaneRefs(f.body, none)) return false;
                }
            }
            out.push_back(f);
        }
        frames.swap(out);
        return true;
    }

    // (d0, d2) of our junction section for maneuver 0 at route position m0: it ends at the maneuver (d0 - d2 = m0, as
    // every factory junction section) and starts kLaneMarginM before the car position we claim (m0 + span).
    void laneSection(double m0, double& d0, double& d2) const {
        double span = template_pos_ > m0 ? template_pos_ - m0 : kLaneSectionM;
        has_anchor_ = true;
        anchor_m0_ = m0;
        anchor_span_ = span;
        d0 = m0 + span + kLaneMarginM;
        d2 = span + kLaneMarginM;
    }

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
    int pos_frame_;               // index of the template's 0x11 with the car position, -1 = none
    double template_pos_;
    mutable bool has_anchor_;     // (maneuver-0 position, span) of the last graph built with lanes: the feed stays inside
    mutable double anchor_m0_;
    mutable double anchor_span_;
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
