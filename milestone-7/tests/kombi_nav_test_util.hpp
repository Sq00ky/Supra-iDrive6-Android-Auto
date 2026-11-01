// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host-test helpers for kombi_nav: a protobuf encoder for Android Auto ch 10 messages and a scripted drive.
#ifndef KOMBI_NAV_TEST_UTIL_HPP
#define KOMBI_NAV_TEST_UTIL_HPP

#include "kombi_nav_msg.hpp"
#include <string>
#include <vector>

namespace kntest {

typedef std::vector<uint8_t> Bytes;

inline void varint(Bytes& o, uint64_t v) {
    do { uint8_t b = v & 0x7F; v >>= 7; o.push_back(b | (v ? 0x80 : 0)); } while (v);
}
inline Bytes& fInt(Bytes& o, uint32_t f, uint64_t v) { varint(o, f << 3); varint(o, v); return o; }
inline Bytes& fBytes(Bytes& o, uint32_t f, const Bytes& v) {
    varint(o, (f << 3) | 2); varint(o, v.size()); o.insert(o.end(), v.begin(), v.end()); return o;
}
inline Bytes& fStr(Bytes& o, uint32_t f, const std::string& s) { return fBytes(o, f, Bytes(s.begin(), s.end())); }
inline Bytes msgStr(uint32_t f, const std::string& s) { Bytes o; return fStr(o, f, s); }

inline Bytes status(int st) { Bytes o; return fInt(o, 1, st); }

// NavigationState with one step {maneuver {type, [exit_number, exit_angle]}, road {name}} + next steps.
inline Bytes navState(int type, const std::string& road, int exit_angle = -1,
                      const std::vector<std::pair<int, std::string> >& next = std::vector<std::pair<int, std::string> >()) {
    Bytes man, step, o;
    fInt(man, 1, type);
    if (exit_angle >= 0) { fInt(man, 2, 2); fInt(man, 3, exit_angle); }
    fBytes(step, 1, man);
    fBytes(step, 2, msgStr(1, road));
    fBytes(step, 4, msgStr(1, "cue text"));
    fBytes(o, 1, step);
    for (size_t i = 0; i < next.size(); ++i) {
        Bytes m2, s2;
        fInt(m2, 1, next[i].first);
        fBytes(s2, 1, m2);
        fBytes(s2, 2, msgStr(1, next[i].second));
        fBytes(o, 1, s2);
    }
    fBytes(o, 2, msgStr(1, "1 DEMO WAY"));
    return o;
}

// NavigationCurrentPosition {step_distance {distance {meters, display, unit}, seconds}, destination {...}}.
inline Bytes curPos(int64_t step_m, const std::string& display, int unit, int seconds = 30, int64_t dest_m = 5000,
                    bool with_meters = true) {
    Bytes d, sd, dd, ddist, o;
    if (with_meters) fInt(d, 1, (uint64_t)step_m);
    fStr(d, 2, display);
    fInt(d, 3, unit);
    fBytes(sd, 1, d);
    fInt(sd, 2, seconds);
    fBytes(o, 1, sd);
    fInt(ddist, 1, (uint64_t)dest_m);
    fStr(ddist, 2, "3.1");
    fInt(ddist, 3, 5);
    fBytes(dd, 1, ddist);
    fStr(dd, 2, "12:17 AM");
    fInt(dd, 3, 400);
    fBytes(o, 2, dd);
    return o;
}

struct Tick {
    double t;
    nbt::kombinav::NavMessages m;
};

struct RouteStep {
    int type;
    const char* road;
    double dist_m;
    int angle;
};

// A test route (LEFT, RIGHT, roundabout 90°, slight left, destination right), driven at speed_mps with
// Google-Maps-style display values; the first tick has guidance on but no step yet, the last turns it off.
inline std::vector<Tick> demoDrive(const std::vector<RouteStep>& route, double speed_mps = 15.0) {
    std::vector<Tick> out;
    double t = 0;
    Tick first;
    first.t = 0;
    first.m.store(0x8003, &status(1)[0], 2);
    out.push_back(first);
    for (size_t k = 0; k < route.size(); ++k) {
        std::vector<std::pair<int, std::string> > nxt;
        for (size_t j = k + 1; j < route.size() && j < k + 3; ++j) nxt.push_back(std::make_pair(route[j].type, route[j].road));
        Bytes st = navState(route[k].type, route[k].road, route[k].angle, nxt);
        double left = route[k].dist_m;
        while (left > 0) {
            std::string disp;
            int unit;
            nbt::kombinav::mapsDisplay(left, disp, unit);
            Bytes cp = curPos((int64_t)left, disp, unit, (int)(left / speed_mps));
            Tick tk;
            tk.t = t;
            tk.m.store(0x8003, &status(1)[0], 2);
            tk.m.store(0x8006, &st[0], st.size());
            tk.m.store(0x8007, &cp[0], cp.size());
            out.push_back(tk);
            t += 1.0;
            left -= speed_mps;
        }
    }
    Tick last;
    last.t = t;
    last.m.store(0x8003, &status(2)[0], 2);
    out.push_back(last);
    return out;
}

inline std::vector<RouteStep> demoRoute() {
    RouteStep r[] = { {7, "TEST ST", 800, -1}, {8, "MAIN ST", 480, -1}, {35, "CIRCLE RD", 320, 90},
                      {5, "OAK AVE", 400, -1}, {42, "DESTINATION", 160, -1} };
    return std::vector<RouteStep>(r, r + 5);
}

} // namespace kntest

#endif
