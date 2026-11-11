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
#ifndef CLUSTER_NAV_MSG_HPP
#define CLUSTER_NAV_MSG_HPP

#include "aa_proto.hpp"

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

namespace nbt {
namespace kombinav {

// One by one: `using namespace proto` would clash with any name in nbt spelled the same.
using proto::PbField;
using proto::PbReader;
using proto::PbSpan;
using proto::pbInt;
using proto::pbMsg;
using proto::pbText;

// ---- the latest ch 10 messages ------------------------------------------------------------------------------------

// The newest body of each message we use (bytes after the 2-byte message id).
struct NavMessages {
    std::vector<uint8_t> status;     // 0x8003
    std::vector<uint8_t> state;      // 0x8006
    std::vector<uint8_t> position;   // 0x8007
    std::vector<uint8_t> turn;       // 0x8004 (legacy)
    std::vector<uint8_t> turn_dist;  // 0x8005 (legacy)
    bool has_status, has_state, has_position, has_turn, has_turn_dist;
    uint32_t pos_seq;                // counts 0x8007 / 0x8005 bodies stored
    uint32_t step_pos_seq;           // pos_seq when the step (0x8006 / 0x8004) last changed: equal = no position since
    NavMessages() : has_status(false), has_state(false), has_position(false), has_turn(false), has_turn_dist(false),
                    pos_seq(0), step_pos_seq(0) {}

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
        bool changed = !*has || slot->size() != len || (len && memcmp(&(*slot)[0], body, len) != 0);
        slot->assign(body, body + len);
        *has = true;
        if (msg_id == 0x8007 || msg_id == 0x8005) ++pos_seq;
        if ((msg_id == 0x8006 || msg_id == 0x8004) && changed) step_pos_seq = pos_seq;
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
// One direction of an AA lane (NavigationStep.lanes[].lane_directions[]): shape (LaneDirection.Shape: 0 UNKNOWN,
// 1 STRAIGHT, 2/3 SLIGHT_LEFT/RIGHT, 4/5 NORMAL_LEFT/RIGHT, 6/7 SHARP_LEFT/RIGHT, 8/9 U_TURN_LEFT/RIGHT) and whether
// the phone highlights it (the lane to take).
struct LaneDirection {
    int shape;
    bool highlighted;
    LaneDirection() : shape(0), highlighted(false) {}
    LaneDirection(int s, bool h) : shape(s), highlighted(h) {}
    bool operator==(const LaneDirection& o) const { return shape==o.shape && highlighted==o.highlighted; }
};
typedef std::vector<LaneDirection> AaLane;

struct NavView {
    int aa_type;             // Maneuver TYPE_*
    std::string road;
    std::string dist_text;   // the phone's display value, e.g. "0.3" or "600"
    int aa_unit;             // 1 m, 2/3 km, 4/5 mi, 6 ft, 7 yd, 0 unknown
    bool has_step_m;
    int64_t step_m;          // metres to the maneuver
    bool has_step_s;
    int64_t step_s;          // seconds to the maneuver, the phone's estimate (0 far from it counts as none)
    uint32_t pos_seq;        // NavMessages::pos_seq of the position this came from; 0 = unknown
    // NavMessages::step_pos_seq: when it equals pos_seq, the position was stored before the step changed, so it may
    // still be the previous step's (the newest 0x8006 and 0x8007 are kept apart)
    uint32_t step_pos_seq;
    bool has_dest_m;
    int64_t dest_m;
    bool has_exit_angle;
    int64_t exit_angle;
    bool has_exit_number; int64_t exit_number;
    bool has_next, next_has_angle; int next_type; int64_t next_angle;
    std::vector<AaLane> lanes;   // the step's lanes, left to right (the phone sends them only near the junction)
    NavView() : aa_type(0), aa_unit(0), has_step_m(false), step_m(0), has_step_s(false), step_s(0), pos_seq(0), step_pos_seq(0),
                has_dest_m(false), dest_m(0), has_exit_angle(false), exit_angle(0), has_exit_number(false), exit_number(0),
                has_next(false), next_has_angle(false), next_type(0), next_angle(0) {}

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

const int64_t kNoSecondsM = 50;   // a step's seconds of 0 count only this close to the maneuver

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
        v.has_exit_number=pbInt(man,2,v.exit_number) && v.exit_number>0 && v.exit_number<=99;
        if (pbInt(man, 3, x)) { v.has_exit_angle = true; v.exit_angle = x; }
        v.road = pbText(pbMsg(step, 2), 1);
        // Second AA step only; its distance is not present here and is never synthesized.
        PbReader steps(ns.data,ns.len); PbField sf; unsigned stepIndex=0;
        while(steps.next(sf)) if(sf.num==1 && sf.wire==2 && ++stepIndex==2) {
            PbSpan next(sf.data,sf.len), nm=pbMsg(next,1); int64_t nt;
            if(pbInt(nm,1,nt) && nt>0) {
                v.has_next=true; v.next_type=(int)nt;
                v.next_has_angle=pbInt(nm,3,v.next_angle);
            }
            break;
        }
        // step field 3: repeated NavigationLane {1 repeated LaneDirection {1 shape, 2 is_highlighted}}
        PbReader lr(step.data, step.len);
        PbField lf;
        while (lr.next(lf)) {
            if (lf.num != 3 || lf.wire != 2) continue;
            AaLane lane;
            PbReader dr(lf.data, lf.len);
            PbField df;
            while (dr.next(df)) {
                if (df.num != 1 || df.wire != 2) continue;
                PbSpan d(df.data, df.len);
                int64_t sh = 0, hl = 0;
                pbInt(d, 1, sh);
                pbInt(d, 2, hl);
                lane.push_back(LaneDirection((int)sh, hl != 0));
            }
            v.lanes.push_back(lane);
        }
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
        v.has_exit_number=pbInt(ev,5,v.exit_number) && v.exit_number>0 && v.exit_number<=99;
        if (pbInt(ev, 6, x)) { v.has_exit_angle = true; v.exit_angle = x; }
    } else {
        return false;
    }
    PbSpan cp = NavMessages::span(m.position), de = NavMessages::span(m.turn_dist);
    int64_t secs = -1;
    if (m.has_position && cp.len) {
        PbSpan sd = pbMsg(cp, 1), d = pbMsg(sd, 1);
        int64_t x;
        if (pbInt(d, 1, x)) { v.has_step_m = true; v.step_m = x; }
        if (pbInt(sd, 2, x)) secs = x;
        v.dist_text = pbText(d, 2);
        if (pbInt(d, 3, x)) v.aa_unit = (int)x;
        if (pbInt(pbMsg(pbMsg(cp, 2), 1), 1, x)) { v.has_dest_m = true; v.dest_m = x; }
    } else if (m.has_turn_dist && de.len) {
        // NextTurnDistanceEvent {1 distance_m, 2 time_s, 3 display_distance_e3, 4 display_distance_unit}
        int64_t x, e3;
        if (pbInt(de, 1, x)) { v.has_step_m = true; v.step_m = x; }
        if (pbInt(de, 2, x)) secs = x;
        if (pbInt(de, 4, x)) v.aa_unit = (int)x;
        if (pbInt(de, 3, e3)) {
            v.dist_text = (v.aa_unit == 3 || v.aa_unit == 5) ? fmt1((double)e3 / 1000.0) : fmtInt(roundE3HalfEven(e3));
        }
    }
    // Seconds to the step. A 0 with the maneuver further than kNoSecondsM is "not known", not "now".
    if (secs > 0 || (secs == 0 && v.has_step_m && v.step_m < kNoSecondsM)) { v.has_step_s = true; v.step_s = secs; }
    if ((m.has_position && cp.len) || (m.has_turn_dist && de.len)) v.pos_seq = m.pos_seq;
    v.step_pos_seq = m.step_pos_seq;
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

// ---- lanes -> the KOMBI lane model (car-verified 2026-11-01: lane row on the cluster and the HUD) -------------------
// AA lane shape -> KOMBI lane arrow (the same enum as the maneuver direction). Car-matched: STRAIGHT 1, NORMAL_LEFT 6,
// NORMAL_RIGHT 7, SLIGHT_LEFT 2 (factory lane captures + the parked lane test); SLIGHT_RIGHT 3 by symmetry; SHARP_* /
// U_TURN_* by the KOMBI enum names only. -1 = no KOMBI arrow (then no lane row at all).
inline int laneArrow(int shape) {
    switch (shape) {
        case 1: return 1;    // STRAIGHT -> STRAIGHT_ON
        case 2: return 2;    // SLIGHT_LEFT -> HALF_LEFT
        case 3: return 3;    // SLIGHT_RIGHT -> HALF_RIGHT
        case 4: return 6;    // NORMAL_LEFT -> LEFT
        case 5: return 7;    // NORMAL_RIGHT -> RIGHT
        case 6: return 4;    // SHARP_LEFT -> TIGHT_LEFT
        case 7: return 5;    // SHARP_RIGHT -> TIGHT_RIGHT
        case 8: return 8;    // U_TURN_LEFT
        case 9: return 9;    // U_TURN_RIGHT
        default: return -1;
    }
}

// One lane as the KOMBI draws it: kind 1 = a lane to take (highlighted), 2 = another lane; one arrow per lane, as in
// every factory lane model.
struct KombiLane {
    uint8_t kind;
    uint8_t arrow;
    KombiLane() : kind(0), arrow(0) {}
    KombiLane(uint8_t k, uint8_t a) : kind(k), arrow(a) {}
    bool operator==(const KombiLane& o) const { return kind == o.kind && arrow == o.arrow; }
};
typedef std::vector<KombiLane> KombiLanes;
const uint8_t kLaneTake = 1;
const uint8_t kLaneOther = 2;
const size_t kLaneMax = 8;          // factory lane models seen: 2..7 lanes

// The phone's lanes -> the KOMBI's, left to right. False (and out empty) when there are none or anything can't be
// mapped (an empty lane, an unknown shape, more than kLaneMax lanes): no lane row rather than a wrong one. A lane
// with several directions shows its highlighted one, else the phone's first (car match: a STRAIGHT+RIGHT through lane
// is the factory's STRAIGHT).
inline bool kombiLanes(const NavView& v, KombiLanes& out) {
    out.clear();
    if (v.lanes.empty() || v.lanes.size() > kLaneMax) return false;
    for (size_t i = 0; i < v.lanes.size(); ++i) {
        const AaLane& l = v.lanes[i];
        if (l.empty()) { out.clear(); return false; }
        int shape = l[0].shape;
        bool take = false;
        for (size_t k = 0; k < l.size(); ++k)
            if (l[k].highlighted) { shape = l[k].shape; take = true; break; }
        int a = laneArrow(shape);
        if (a < 0) { out.clear(); return false; }
        out.push_back(KombiLane(take ? kLaneTake : kLaneOther, (uint8_t)a));
    }
    return true;
}

// Log form: "LEFT* STRAIGHT_ON RIGHT" (* = a lane to take), "none".
inline std::string describeLanes(const KombiLanes& l) {
    if (l.empty()) return "none";
    std::string s;
    for (size_t i = 0; i < l.size(); ++i) {
        const char* n;
        switch (l[i].arrow) {
            case 1: n = "STRAIGHT_ON"; break;
            case 2: n = "HALF_LEFT"; break;
            case 3: n = "HALF_RIGHT"; break;
            case 4: n = "TIGHT_LEFT"; break;
            case 5: n = "TIGHT_RIGHT"; break;
            case 6: n = "LEFT"; break;
            case 7: n = "RIGHT"; break;
            case 8: n = "UTURN_LEFT"; break;
            case 9: n = "UTURN_RIGHT"; break;
            default: n = "?"; break;
        }
        if (i) s += ' ';
        s += n;
        if (l[i].kind == kLaneTake) s += '*';
    }
    return s;
}

// Display units -> the KOMBI unit byte after the distance text; -1 = no KOMBI unit known.
// KOMBI unit bytes, read off the HUD on a J29 (sweep of 0..15): 0 km, 1 mi, 2 mi, 3 m, 4 yd, 5 ft, 6..15 ft.
inline int kombiUnit(int aa_unit) {
    switch (aa_unit) {
        case 1: return 0x03;                 // METERS
        case 2: case 3: return 0x00;         // KILOMETERS, KILOMETERS_P1
        case 4: case 5: return 0x01;         // MILES, MILES_P1
        case 6: return 0x05;                 // FEET
        case 7: return 0x04;                 // YARDS
        default: return -1;
    }
}

inline const char* kombiUnitName(int kombi_unit) {
    switch (kombi_unit) {
        case 0x00: return "km";
        case 0x01: case 0x02: return "mi";
        case 0x03: return "m";
        case 0x04: return "yd";
        case 0x05: return "ft";
        default: return kombi_unit < 0 ? "(unit kept)" : "ft";
    }
}

// Unit system of the phone's display units: 'm' metric, 'y' miles + yards, 'f' miles + feet (also unknown).
inline char unitSystem(int aa_unit) {
    if (aa_unit == 1 || aa_unit == 2 || aa_unit == 3) return 'm';
    if (aa_unit == 7) return 'y';
    return 'f';
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

// Metric display: from 1 km as one-decimal kilometres, below that metres (10 m steps under 100 m, else 50 m steps).
inline void metricDisplay(double m, std::string& text, int& aa_unit) {
    if (m >= 999.5) { text = fmt1(m / 1000.0); aa_unit = 3; return; }
    int64_t step = m < 100.0 ? 10 : 50;
    int64_t r = (int64_t)(m / (double)step) * step;
    if (r < 10) r = 10;
    text = fmtInt(r);
    aa_unit = 1;
}

// Miles + yards (UK): from 0.1 mi as one-decimal miles, below that yards rounded down to 50.
inline void yardsDisplay(double m, std::string& text, int& aa_unit) {
    double mi = m / 1609.344;
    if (mi >= 0.095) { text = fmt1(mi); aa_unit = 5; return; }
    int64_t yd = (int64_t)(m / 0.9144 / 50.0) * 50;
    if (yd < 50) yd = 50;
    text = fmtInt(yd);
    aa_unit = 7;
}

// Reformat metres in the phone's own unit system (never metric -> miles).
inline void unitDisplay(double m, int phone_unit, std::string& text, int& aa_unit) {
    switch (unitSystem(phone_unit)) {
        case 'm': metricDisplay(m, text, aa_unit); break;
        case 'y': yardsDisplay(m, text, aa_unit); break;
        default: mapsDisplay(m, text, aa_unit); break;
    }
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

// Distance text of at most n characters with a KOMBI unit, so the cluster shows at most n + 3 ("0.5 mi", "450 ft",
// "1.2 km", "300 m"): the phone's own text when it fits, else reformatted from metres in the phone's unit system.
inline void shortDistance(const NavView& v, size_t n, std::string& text, int& aa_unit) {
    if (utf8Length(v.dist_text) <= n && kombiUnit(v.aa_unit) >= 0) {
        text = v.dist_text; aa_unit = v.aa_unit;
        return;
    }
    double m;
    if (!distanceMetres(v, m)) { text = "-"; aa_unit = v.aa_unit; return; }
    unitDisplay(m, v.aa_unit, text, aa_unit);
    if (text.size() > n) {
        if (unitSystem(v.aa_unit) == 'm') { text = fmtInt((int64_t)(m / 1000.0 + 0.5)); aa_unit = 2; }
        else { text = fmtInt((int64_t)(m / 1609.344 + 0.5)); aa_unit = 5; }
    }
    text = utf8Prefix(text, n);
}

} // namespace kombinav
} // namespace nbt

#endif
