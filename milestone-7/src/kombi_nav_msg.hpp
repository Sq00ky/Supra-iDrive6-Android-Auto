// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// kombi_nav: Android Auto navigation status (ch 10) -> what the cluster / HUD arrow needs. Pure, no I/O.
//
// Messages (field numbers as in opencardev/aasdk aap_protobuf/service/navigationstatus; maneuver types are
// androidx.car.app.navigation.model.Maneuver TYPE_*):
//   0x8003 NavigationStatus          {1 status: 1 ACTIVE, 2 INACTIVE, 3 REROUTING}
//   0x8006 NavigationState           {1 steps[] {1 maneuver {1 type, 2 exit_number, 3 exit_angle}, 2 road {1 name},
//                                     3 lanes[], 4 cue {1 text[]}}, 2 destinations[]}
//   0x8007 NavigationCurrentPosition {1 step_distance {1 distance {1 meters, 2 display_value, 3 display_units},
//                                     2 seconds}, 2 destination_distances[] {1 distance {...}, 2 eta, 3 seconds},
//                                     3 current_road {1 name}}
//   0x8004 / 0x8005: the older NextTurnEvent / NextTurnDistanceEvent (phones that don't send 0x8006 / 0x8007).
#ifndef KOMBI_NAV_MSG_HPP
#define KOMBI_NAV_MSG_HPP

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

namespace nbt {
namespace kombinav {

// ---- protobuf reading ------------------------------------------------------------------------------------------

struct PbField {
    uint32_t num;
    uint32_t wire;          // 0 varint, 1 fixed64, 2 length-delimited, 5 fixed32
    uint64_t value;         // wire 0
    const uint8_t* data;    // wire 2
    size_t len;             // wire 2
};

inline bool pbReadVarint(const uint8_t*& p, const uint8_t* end, uint64_t& v) {
    v = 0;
    for (int shift = 0; p < end && shift < 64; shift += 7) {
        uint8_t b = *p++;
        v |= (uint64_t)(b & 0x7F) << shift;
        if (!(b & 0x80)) return true;
    }
    return false;
}

// Walks the fields of one message. next() returns false at the end or on malformed input (then ok() is false).
class PbReader {
public:
    PbReader(const uint8_t* p, size_t n) : p_(p), end_(p + n), ok_(true) {}
    bool next(PbField& f) {
        if (p_ >= end_) return false;
        uint64_t tag;
        if (!pbReadVarint(p_, end_, tag) || (tag >> 3) == 0) return fail();
        f.num = (uint32_t)(tag >> 3);
        f.wire = (uint32_t)(tag & 7);
        f.value = 0; f.data = NULL; f.len = 0;
        if (f.wire == 0) {
            if (!pbReadVarint(p_, end_, f.value)) return fail();
        } else if (f.wire == 1) {
            if (end_ - p_ < 8) return fail();
            p_ += 8;
        } else if (f.wire == 5) {
            if (end_ - p_ < 4) return fail();
            p_ += 4;
        } else if (f.wire == 2) {
            uint64_t n;
            if (!pbReadVarint(p_, end_, n) || n > (uint64_t)(end_ - p_)) return fail();
            f.data = p_; f.len = (size_t)n; p_ += n;
        } else {
            return fail();
        }
        return true;
    }
    bool ok() const { return ok_; }
private:
    bool fail() { ok_ = false; p_ = end_; return false; }
    const uint8_t* p_;
    const uint8_t* end_;
    bool ok_;
};

// A sub-message or string: [data, data + len). present = the field was there with that wire type.
struct PbSpan {
    const uint8_t* data;
    size_t len;
    bool present;
    PbSpan() : data(NULL), len(0), present(false) {}
    PbSpan(const uint8_t* d, size_t n) : data(d), len(n), present(true) {}
};

// First occurrence of field num (any wire type decides, like a reader that takes the first value it sees).
inline bool pbFirst(const PbSpan& m, uint32_t num, PbField& out) {
    if (!m.present) return false;
    PbReader r(m.data, m.len);
    PbField f;
    while (r.next(f)) {
        if (f.num == num) { out = f; return true; }
    }
    return false;
}

// Text check for a length-delimited field: valid UTF-8, no control characters except tab.
inline bool pbLooksLikeText(const uint8_t* p, size_t n) {
    if (!n) return false;
    size_t i = 0;
    while (i < n) {
        uint8_t c = p[i];
        if ((c < 0x20 && c != 9) || c == 0x7F) return false;
        if (c < 0x80) { ++i; continue; }
        size_t k = (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 0;
        if (!k || i + k > n) return false;
        for (size_t j = 1; j < k; ++j)
            if ((p[i + j] & 0xC0) != 0x80) return false;
        i += k;
    }
    return true;
}

// Sub-message at field num (length-delimited and not text), or an empty span.
inline PbSpan pbMsg(const PbSpan& m, uint32_t num) {
    PbField f;
    if (!pbFirst(m, num, f) || f.wire != 2 || pbLooksLikeText(f.data, f.len)) return PbSpan();
    return PbSpan(f.data, f.len);
}

// String at field num; "" when absent or not text.
inline std::string pbText(const PbSpan& m, uint32_t num) {
    PbField f;
    if (!pbFirst(m, num, f) || f.wire != 2 || !pbLooksLikeText(f.data, f.len)) return std::string();
    return std::string((const char*)f.data, f.len);
}

// Varint at field num.
inline bool pbInt(const PbSpan& m, uint32_t num, int64_t& v) {
    PbField f;
    if (!pbFirst(m, num, f) || f.wire != 0) return false;
    v = (int64_t)f.value;
    return true;
}

// ---- the latest ch 10 messages ------------------------------------------------------------------------------------

// The newest body of each message we use (bytes after the 2-byte message id).
struct NavMessages {
    std::vector<uint8_t> status;     // 0x8003
    std::vector<uint8_t> state;      // 0x8006
    std::vector<uint8_t> position;   // 0x8007
    std::vector<uint8_t> turn;       // 0x8004 (legacy)
    std::vector<uint8_t> turn_dist;  // 0x8005 (legacy)
    bool has_status, has_state, has_position, has_turn, has_turn_dist;
    NavMessages() : has_status(false), has_state(false), has_position(false), has_turn(false), has_turn_dist(false) {}

    // Returns false for messages kombi_nav doesn't use.
    bool store(uint16_t msg_id, const uint8_t* body, size_t len) {
        std::vector<uint8_t>* slot = NULL;
        bool* has = NULL;
        switch (msg_id) {
            case 0x8003: slot = &status; has = &has_status; break;
            case 0x8006: slot = &state; has = &has_state; break;
            case 0x8007: slot = &position; has = &has_position; break;
            case 0x8004: slot = &turn; has = &has_turn; break;
            case 0x8005: slot = &turn_dist; has = &has_turn_dist; break;
            default: return false;
        }
        slot->assign(body, body + len);
        *has = true;
        return true;
    }
    void clear() { *this = NavMessages(); }

    // Guidance is on only while the last NavigationStatus says ACTIVE (1). REROUTING (3) counts as off: the caller's
    // grace period keeps the arrow through it.
    bool guidanceActive() const {
        int64_t st;
        return has_status && pbInt(span(status), 1, st) && st == 1;
    }
    static PbSpan span(const std::vector<uint8_t>& v) {
        return v.empty() ? PbSpan(NULL, 0) : PbSpan(&v[0], v.size());
    }
};

// ---- view: the next maneuver in Android Auto terms ----------------------------------------------------------------

struct NavView {
    int aa_type;             // Maneuver TYPE_*
    std::string road;
    std::string dist_text;   // the phone's display value, e.g. "0.3" or "600"
    int aa_unit;             // 1 m, 2/3 km, 4/5 mi, 6 ft, 7 yd, 0 unknown
    bool has_step_m;
    int64_t step_m;          // metres to the maneuver
    bool has_dest_m;
    int64_t dest_m;
    bool has_exit_angle;
    int64_t exit_angle;
    NavView() : aa_type(0), aa_unit(0), has_step_m(false), step_m(0), has_dest_m(false), dest_m(0),
                has_exit_angle(false), exit_angle(0) {}

    bool sameManeuver(const NavView& o) const {
        return aa_type == o.aa_type && road == o.road && has_exit_angle == o.has_exit_angle &&
               (!has_exit_angle || exit_angle == o.exit_angle);
    }
    bool sameDistance(const NavView& o) const { return dist_text == o.dist_text && aa_unit == o.aa_unit; }
};

// e3 / 1000 rounded half to even (2500 -> 2, 3500 -> 4), in integers: the daemon doesn't link libm.
inline int64_t roundE3HalfEven(int64_t e3) {
    bool neg = e3 < 0;
    uint64_t a = neg ? (uint64_t)(-(e3 + 1)) + 1 : (uint64_t)e3;
    uint64_t q = a / 1000, r = a % 1000;
    if (r > 500 || (r == 500 && (q & 1))) ++q;
    return neg ? -(int64_t)q : (int64_t)q;
}

inline std::string fmtInt(int64_t v) {
    char b[32];
    snprintf(b, sizeof(b), "%lld", (long long)v);
    return b;
}

inline std::string fmt1(double v) {
    char b[32];
    snprintf(b, sizeof(b), "%.1f", v);
    return b;
}

// Next-step view, or false when there is nothing to show (guidance off is the caller's decision).
inline bool viewFromMessages(const NavMessages& m, NavView& v) {
    v = NavView();
    PbSpan ns = NavMessages::span(m.state), ev = NavMessages::span(m.turn);
    if (m.has_state && ns.len) {
        PbSpan step = pbMsg(ns, 1);
        if (!step.present) return false;
        PbSpan man = pbMsg(step, 1);
        int64_t x;
        if (pbInt(man, 1, x)) v.aa_type = (int)x;
        if (pbInt(man, 3, x)) { v.has_exit_angle = true; v.exit_angle = x; }
        v.road = pbText(pbMsg(step, 2), 1);
    } else if (m.has_turn && ev.len) {
        // NextTurnEvent {1 road, 2 turn_side (1 L / 2 R), 3 event, 5 exit number, 6 exit angle}: event -> type pair
        static const int kLegacy[][3] = { {1, 1, 1}, {2, 2, 2}, {3, 5, 6}, {4, 7, 8}, {5, 9, 10}, {6, 11, 12},
            {7, 15, 16}, {8, 23, 24}, {9, 25, 26}, {10, 27, 28}, {11, 34, 32}, {12, 34, 32}, {13, 34, 32},
            {14, 36, 36}, {16, 37, 37}, {17, 38, 38}, {19, 39, 39} };
        int64_t side = 0, event = 0, x;
        bool has_side = pbInt(ev, 2, side);
        int pl = 0, pr = 0;
        if (pbInt(ev, 3, event))
            for (size_t i = 0; i < sizeof(kLegacy) / sizeof(kLegacy[0]); ++i)
                if (kLegacy[i][0] == event) { pl = kLegacy[i][1]; pr = kLegacy[i][2]; }
        v.aa_type = has_side && side == 1 ? pl : has_side && side == 2 ? pr : (pl == pr ? pl : 0);
        v.road = pbText(ev, 1);
        if (pbInt(ev, 6, x)) { v.has_exit_angle = true; v.exit_angle = x; }
    } else {
        return false;
    }
    PbSpan cp = NavMessages::span(m.position), de = NavMessages::span(m.turn_dist);
    if (m.has_position && cp.len) {
        PbSpan d = pbMsg(pbMsg(cp, 1), 1);
        int64_t x;
        if (pbInt(d, 1, x)) { v.has_step_m = true; v.step_m = x; }
        v.dist_text = pbText(d, 2);
        if (pbInt(d, 3, x)) v.aa_unit = (int)x;
        if (pbInt(pbMsg(pbMsg(cp, 2), 1), 1, x)) { v.has_dest_m = true; v.dest_m = x; }
    } else if (m.has_turn_dist && de.len) {
        // NextTurnDistanceEvent {1 distance_m, 2 time_s, 3 display_distance_e3, 4 display_distance_unit}
        int64_t x, e3;
        if (pbInt(de, 1, x)) { v.has_step_m = true; v.step_m = x; }
        if (pbInt(de, 4, x)) v.aa_unit = (int)x;
        if (pbInt(de, 3, e3)) {
            v.dist_text = (v.aa_unit == 3 || v.aa_unit == 5) ? fmt1((double)e3 / 1000.0) : fmtInt(roundE3HalfEven(e3));
        }
    }
    return true;
}

// ---- view -> KOMBI ------------------------------------------------------------------------------------------------

// Maneuver type -> KOMBI direction enum (BMW.IC.Guidance ManeuverDescription). LEFT 6 / RIGHT 7 and the destination
// flag 10 are car-verified; the others follow the KOMBI enum names: 1 STRAIGHT_ON, 2/3 HALF_LEFT/RIGHT, 4/5
// TIGHT_LEFT/RIGHT, 8/9 UTURN_LEFT/RIGHT, 14/15 ROUNDABOUT_CW/CCW, 25/26 HIGHWAY_IN_LEFT/RIGHT, 28/29
// HIGHWAY_OUT_LEFT/RIGHT, 32/33 LANE_CHANGE_LEFT/RIGHT, 37/38 FORK_LEFT/RIGHT.
inline int kombiDirection(int aa_type) {
    switch (aa_type) {
        case 3: case 5: return 2;            // KEEP_LEFT, TURN_SLIGHT_LEFT
        case 4: case 6: return 3;            // KEEP_RIGHT, TURN_SLIGHT_RIGHT
        case 7: return 6;                    // TURN_NORMAL_LEFT
        case 8: return 7;                    // TURN_NORMAL_RIGHT
        case 9: return 4;                    // TURN_SHARP_LEFT
        case 10: return 5;                   // TURN_SHARP_RIGHT
        case 11: return 8;                   // U_TURN_LEFT
        case 12: return 9;                   // U_TURN_RIGHT
        case 13: case 15: case 17: case 19: return 25;   // ON_RAMP_*_LEFT
        case 14: case 16: case 18: case 20: return 26;   // ON_RAMP_*_RIGHT
        case 21: case 23: return 28;         // OFF_RAMP_*_LEFT
        case 22: case 24: return 29;         // OFF_RAMP_*_RIGHT
        case 25: return 37;                  // FORK_LEFT
        case 26: return 38;                  // FORK_RIGHT
        case 27: return 32;                  // MERGE_LEFT
        case 28: return 33;                  // MERGE_RIGHT
        case 32: case 33: case 43: case 44: return 14;   // clockwise roundabouts (left-hand traffic)
        case 30: case 31: case 34: case 35: case 45: case 46: return 15;   // the others: counter-clockwise
        case 39: case 40: case 41: case 42: return 10;   // DESTINATION*: checkered flag
        default: return 1;                   // depart, straight, name change, ferry, unknown
    }
}

// Display units -> the KOMBI unit byte after the distance text (0x01 mi, 0x05 ft); -1 = no KOMBI unit known.
inline int kombiUnit(int aa_unit) {
    switch (aa_unit) {
        case 6: return 0x05;
        case 4: case 5: return 0x01;
        default: return -1;
    }
}

// Google-Maps-style US display: from 0.1 mi as one-decimal miles, below that feet rounded down to 50.
inline void mapsDisplay(double m, std::string& text, int& aa_unit) {
    double mi = m / 1609.344;
    if (mi >= 0.095) { text = fmt1(mi); aa_unit = 5; return; }
    int64_t ft = (int64_t)(m * 3.28084 / 50.0) * 50;
    if (ft < 50) ft = 50;
    text = fmtInt(ft);
    aa_unit = 6;
}

// Metres to the next maneuver: the phone's value, else parsed from the display text + unit.
inline bool distanceMetres(const NavView& v, double& m) {
    if (v.has_step_m) { m = (double)v.step_m; return true; }
    if (v.dist_text.empty()) return false;
    const char* s = v.dist_text.c_str();
    char* e = NULL;
    double x = strtod(s, &e);
    if (e == s) return false;
    while (*e == ' ' || *e == '\t') ++e;
    if (*e) return false;
    double f;
    switch (v.aa_unit) {
        case 1: f = 1.0; break;
        case 2: case 3: f = 1000.0; break;
        case 4: case 5: f = 1609.344; break;
        case 6: f = 0.3048; break;
        case 7: f = 0.9144; break;
        default: return false;
    }
    m = x * f;
    return true;
}

// UTF-8 prefix of at most n code points (never splits a character).
inline std::string utf8Prefix(const std::string& s, size_t n) {
    size_t i = 0, cps = 0;
    while (i < s.size() && cps < n) {
        unsigned char c = (unsigned char)s[i];
        size_t k = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 1;
        if (i + k > s.size()) k = s.size() - i;
        i += k;
        ++cps;
    }
    return s.substr(0, i);
}

inline size_t utf8Length(const std::string& s) {
    size_t n = 0;
    for (size_t i = 0; i < s.size(); ++i)
        if (((unsigned char)s[i] & 0xC0) != 0x80) ++n;
    return n;
}

// Distance text of at most n characters with a KOMBI mi/ft unit, so the cluster shows at most n + 3 ("0.5 mi",
// "450 ft"): the phone's own text when it fits, else reformatted from metres.
inline void shortDistance(const NavView& v, size_t n, std::string& text, int& aa_unit) {
    if (utf8Length(v.dist_text) <= n && kombiUnit(v.aa_unit) >= 0) {
        text = v.dist_text; aa_unit = v.aa_unit;
        return;
    }
    double m;
    if (!distanceMetres(v, m)) { text = "-"; aa_unit = v.aa_unit; return; }
    mapsDisplay(m, text, aa_unit);
    if (text.size() > n) { text = fmtInt((int64_t)(m / 1609.344 + 0.5)); aa_unit = 5; }
    text = utf8Prefix(text, n);
}

} // namespace kombinav
} // namespace nbt

#endif
