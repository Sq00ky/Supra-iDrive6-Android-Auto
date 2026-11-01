// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test: kombi_nav ch 10 decode + the AA -> KOMBI tables (src/kombi_nav_msg.hpp).
#include "kombi_nav_msg.hpp"
#include "kombi_nav_test_util.hpp"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
using namespace nbt::kombinav;
using namespace kntest;

static NavMessages msgs(const Bytes& state, const Bytes& pos, int st = 1) {
    NavMessages m;
    Bytes s = status(st);
    m.store(0x8003, &s[0], s.size());
    if (!state.empty()) m.store(0x8006, &state[0], state.size());
    if (!pos.empty()) m.store(0x8007, &pos[0], pos.size());
    return m;
}

int main() {
    // A real Google Maps pair, parked (road text made neutral): 0x8006 = {1:{1:{1:1} 2:{1:"toward Main St"}
    // 4:{1:"toward Main St"}} 2:{1:"<dest>"}}, 0x8007 = {1:{1:{2:"0" 3:6}} 2:{1:{1:44306 2:"28" 3:4} 2:"12:17 AM"
    // 3:1844}}: no metres on the step while parked.
    {
        Bytes man, road, cue, step, st, d, sd, dd, ddist, cp;
        fInt(man, 1, 1);
        fBytes(step, 1, man);
        fBytes(step, 2, msgStr(1, "toward Main St"));
        fBytes(step, 4, msgStr(1, "toward Main St"));
        fBytes(st, 1, step);
        fBytes(st, 2, msgStr(1, "1 Some Place"));
        fStr(d, 2, "0"); fInt(d, 3, 6);
        fBytes(sd, 1, d);
        fBytes(cp, 1, sd);
        fInt(ddist, 1, 44306); fStr(ddist, 2, "28"); fInt(ddist, 3, 4);
        fBytes(dd, 1, ddist); fStr(dd, 2, "12:17 AM"); fInt(dd, 3, 1844);
        fBytes(cp, 2, dd);
        NavView v;
        NavMessages m = msgs(st, cp);
        assert(m.guidanceActive());
        assert(viewFromMessages(m, v));
        assert(v.aa_type == 1 && v.road == "toward Main St" && v.dist_text == "0" && v.aa_unit == 6);
        assert(!v.has_step_m && v.has_dest_m && v.dest_m == 44306);
        assert(kombiDirection(v.aa_type) == 1 && kombiUnit(v.aa_unit) == 0x05);
        double mm;
        assert(distanceMetres(v, mm) && mm == 0.0);   // parsed from "0" ft
    }
    // the scripted drive: first tick has no step, LEFT 0.5 mi first, feet later, roundabout 90°
    {
        std::vector<Tick> drive = demoDrive(demoRoute());
        NavView v;
        assert(drive.front().m.guidanceActive() && !viewFromMessages(drive.front().m, v));
        assert(!drive.back().m.guidanceActive());
        assert(viewFromMessages(drive[1].m, v));
        assert(v.aa_type == 7 && v.road == "TEST ST" && v.dist_text == "0.5" && v.aa_unit == 5 && v.step_m == 800);
        assert(kombiDirection(v.aa_type) == 6 && kombiUnit(v.aa_unit) == 0x01);
        bool feet = false, ra = false;
        for (size_t i = 1; i + 1 < drive.size(); ++i) {
            assert(viewFromMessages(drive[i].m, v));
            if (v.aa_unit == 6) { feet = true; assert(kombiUnit(6) == 0x05); }
            if (v.aa_type == 35) { ra = true; assert(v.has_exit_angle && v.exit_angle == 90 && kombiDirection(35) == 15); }
        }
        assert(feet && ra);
    }
    // status: only ACTIVE (1) is guidance; REROUTING (3) is off (the caller's grace period keeps the arrow)
    {
        assert(msgs(Bytes(), Bytes(), 1).guidanceActive());
        assert(!msgs(Bytes(), Bytes(), 2).guidanceActive());
        assert(!msgs(Bytes(), Bytes(), 3).guidanceActive());
        NavMessages none;
        assert(!none.guidanceActive());
    }
    // legacy 0x8004 / 0x8005
    {
        NavMessages m;
        Bytes s = status(1), ev, de;
        m.store(0x8003, &s[0], s.size());
        fStr(ev, 1, "ELM ST"); fInt(ev, 2, 1); fInt(ev, 3, 4);
        fInt(de, 1, 300); fInt(de, 2, 20); fInt(de, 3, 200000); fInt(de, 4, 6);
        m.store(0x8004, &ev[0], ev.size());
        m.store(0x8005, &de[0], de.size());
        NavView v;
        assert(viewFromMessages(m, v) && v.aa_type == 7 && v.road == "ELM ST" && v.dist_text == "200" && v.aa_unit == 6);
        de.clear();
        fInt(de, 1, 480); fInt(de, 2, 30); fInt(de, 3, 300); fInt(de, 4, 5);   // display e3: 0.3 mi
        m.store(0x8005, &de[0], de.size());
        assert(viewFromMessages(m, v) && v.dist_text == "0.3");
        de.clear();
        fInt(de, 1, 3); fInt(de, 3, 2500); fInt(de, 4, 6);                      // 2.5 ft: half to even -> 2
        m.store(0x8005, &de[0], de.size());
        assert(viewFromMessages(m, v) && v.dist_text == "2");
        ev.clear();
        fStr(ev, 1, "X"); fInt(ev, 2, 3); fInt(ev, 3, 4);                       // TURN, side unknown: no guessed LEFT
        m.store(0x8004, &ev[0], ev.size());
        assert(viewFromMessages(m, v) && v.aa_type == 0);
        ev.clear();
        fStr(ev, 1, "X"); fInt(ev, 2, 3); fInt(ev, 3, 14);                      // STRAIGHT has no side
        m.store(0x8004, &ev[0], ev.size());
        assert(viewFromMessages(m, v) && v.aa_type == 36);
    }
    // 0x8006 wins over 0x8004; a step without its 0x8007 has no distance yet
    {
        NavMessages m = msgs(navState(8, "MAIN ST"), Bytes());
        Bytes ev;
        fStr(ev, 1, "OLD"); fInt(ev, 2, 1); fInt(ev, 3, 4);
        m.store(0x8004, &ev[0], ev.size());
        NavView v;
        assert(viewFromMessages(m, v) && v.aa_type == 8 && v.road == "MAIN ST" && v.dist_text.empty() && !v.has_step_m);
    }
    // malformed input never crashes and never invents a maneuver
    {
        NavView v;
        Bytes nested, inner, st;
        fInt(inner, 9, 9);
        fBytes(nested, 1, inner);
        Bytes step;
        fBytes(step, 1, nested);
        fBytes(st, 1, step);
        assert(viewFromMessages(msgs(st, Bytes()), v) && v.aa_type == 0);
        Bytes text = msgStr(1, "text");                                         // step is a string, not a message
        assert(!viewFromMessages(msgs(text, Bytes()), v));
        Bytes garbage;
        garbage.push_back(0xFF); garbage.push_back(0xFF); garbage.push_back(0x07);
        assert(!viewFromMessages(msgs(garbage, Bytes()), v));
        srand(1);
        for (int i = 0; i < 20000; ++i) {                                       // random bytes, both slots
            Bytes a(rand() % 48), b(rand() % 48);
            for (size_t k = 0; k < a.size(); ++k) a[k] = (uint8_t)rand();
            for (size_t k = 0; k < b.size(); ++k) b[k] = (uint8_t)rand();
            NavMessages m = msgs(a, b);
            viewFromMessages(m, v);
        }
        int64_t x;
        Bytes trunc;
        trunc.push_back(0x08);                                                  // tag without its varint
        PbSpan sp(&trunc[0], trunc.size());
        assert(!pbInt(sp, 1, x));
    }
    // direction table (LEFT 6 / RIGHT 7 / destination 10 car-verified)
    {
        assert(kombiDirection(7) == 6 && kombiDirection(8) == 7 && kombiDirection(42) == 10 && kombiDirection(39) == 10);
        assert(kombiDirection(3) == 2 && kombiDirection(4) == 3 && kombiDirection(11) == 8 && kombiDirection(12) == 9);
        assert(kombiDirection(13) == 25 && kombiDirection(24) == 29 && kombiDirection(25) == 37 && kombiDirection(28) == 33);
        assert(kombiDirection(32) == 14 && kombiDirection(34) == 15 && kombiDirection(36) == 1 && kombiDirection(999) == 1);
        assert(kombiUnit(6) == 0x05 && kombiUnit(4) == 0x01 && kombiUnit(5) == 0x01);
        // car-read KOMBI unit bytes (J29 sweep 2026-11-01): 0 km, 1 mi, 3 m, 4 yd, 5 ft
        assert(kombiUnit(1) == 0x03 && kombiUnit(2) == 0x00 && kombiUnit(3) == 0x00 && kombiUnit(7) == 0x04);
        assert(kombiUnit(0) < 0 && kombiUnit(8) < 0);
        assert(!strcmp(kombiUnitName(0x00), "km") && !strcmp(kombiUnitName(0x03), "m") && !strcmp(kombiUnitName(0x04), "yd"));
    }
    // Google-Maps-style display and the 3-character cap
    {
        std::string t;
        int u;
        mapsDisplay(250, t, u); assert(t == "0.2" && u == 5);   // 250 m -> 0.2 mi, never "250 ft"
        mapsDisplay(150, t, u); assert(t == "450" && u == 6);   // 0.093 mi: still feet
        mapsDisplay(153, t, u); assert(t == "0.1" && u == 5);
        mapsDisplay(137, t, u); assert(t == "400" && u == 6);
        mapsDisplay(5, t, u); assert(t == "50" && u == 6);
        NavView v;
        v.dist_text = "0.5"; v.aa_unit = 5; v.has_step_m = true; v.step_m = 800;
        shortDistance(v, 3, t, u); assert(t == "0.5" && u == 5);
        v.dist_text = "450"; v.aa_unit = 6; v.step_m = 137;
        shortDistance(v, 3, t, u); assert(t == "450" && u == 6);
        v.dist_text = "1,000"; v.aa_unit = 6; v.step_m = 305;
        shortDistance(v, 3, t, u); assert(t == "0.2" && u == 5);
        v.dist_text = "12.5"; v.aa_unit = 5; v.has_step_m = false;
        shortDistance(v, 3, t, u); assert(t == "13" && u == 5);
        int ms[] = { 10, 150, 152, 153, 1000, 3000, 15000, 30000 };
        for (size_t i = 0; i < sizeof(ms) / sizeof(ms[0]); ++i) {
            v.dist_text = "xxxxxx"; v.aa_unit = 0; v.has_step_m = true; v.step_m = ms[i];
            shortDistance(v, 3, t, u);
            assert(t.size() <= 3 && (u == 5 || u == 6));
        }
    }
    // metric and yards: never converted to miles; the phone's own text when it fits
    {
        std::string t;
        int u;
        metricDisplay(1500, t, u); assert(t == "1.5" && u == 3);
        metricDisplay(999, t, u); assert(t == "950" && u == 1);
        metricDisplay(999.6, t, u); assert(t == "1.0" && u == 3);
        metricDisplay(330, t, u); assert(t == "300" && u == 1);
        metricDisplay(87, t, u); assert(t == "80" && u == 1);
        metricDisplay(3, t, u); assert(t == "10" && u == 1);
        yardsDisplay(250, t, u); assert(t == "0.2" && u == 5);
        yardsDisplay(120, t, u); assert(t == "100" && u == 7);
        yardsDisplay(5, t, u); assert(t == "50" && u == 7);
        NavView v;
        v.dist_text = "1.2"; v.aa_unit = 3; v.has_step_m = true; v.step_m = 1200;
        shortDistance(v, 3, t, u); assert(t == "1.2" && u == 3 && kombiUnit(u) == 0x00);
        v.dist_text = "300"; v.aa_unit = 1; v.step_m = 300;
        shortDistance(v, 3, t, u); assert(t == "300" && u == 1 && kombiUnit(u) == 0x03);
        v.dist_text = "12.5"; v.aa_unit = 3; v.step_m = 12500;
        shortDistance(v, 3, t, u); assert(t == "13" && u == 2 && kombiUnit(u) == 0x00);
        v.dist_text = "1,000"; v.aa_unit = 1; v.step_m = 1000;
        shortDistance(v, 3, t, u); assert(t == "1.0" && u == 3);
        v.dist_text = "xxxx"; v.aa_unit = 1; v.step_m = 5432;
        shortDistance(v, 3, t, u); assert(t == "5.4" && u == 3);
        v.dist_text = "350"; v.aa_unit = 7; v.step_m = 320;
        shortDistance(v, 3, t, u); assert(t == "350" && u == 7 && kombiUnit(u) == 0x04);
        v.dist_text = "xxxx"; v.aa_unit = 7; v.step_m = 100;
        shortDistance(v, 3, t, u); assert(t == "100" && u == 7);
        int ms[] = { 3, 10, 150, 999, 1000, 3000, 15000, 300000 };
        for (size_t i = 0; i < sizeof(ms) / sizeof(ms[0]); ++i) {
            v.dist_text = "xxxxxx"; v.aa_unit = 2; v.has_step_m = true; v.step_m = ms[i];
            shortDistance(v, 3, t, u);
            assert(t.size() <= 3 && unitSystem(u) == 'm');
        }
    }
    // half to even, like the reference (Python round)
    {
        assert(roundE3HalfEven(2500) == 2 && roundE3HalfEven(3500) == 4 && roundE3HalfEven(2501) == 3);
        assert(roundE3HalfEven(499) == 0 && roundE3HalfEven(-2500) == -2 && roundE3HalfEven(-3500) == -4);
        assert(roundE3HalfEven(200000) == 200);
    }
    // UTF-8 prefix never splits a character
    {
        assert(utf8Prefix("Stra\xc3\x9f" "e", 5) == "Stra\xc3\x9f");
        assert(utf8Length("Stra\xc3\x9f" "e") == 6);
        assert(utf8Prefix(std::string(300, 'X'), 40).size() == 40);
    }
    // lanes: NavigationStep field 3 {1 lane_directions {1 shape, 2 is_highlighted}}, left to right
    {
        NavView v;
        assert(viewFromMessages(msgs(navStateLanes(7, "TEST ST", demoLanes(0)), curPos(320, "0.2", 5)), v));
        assert(v.lanes.size() == 4 && v.lanes[0].size() == 1 && v.lanes[0][0].shape == 4 && v.lanes[0][0].highlighted);
        assert(v.lanes[3].size() == 2 && v.lanes[3][1].shape == 5 && !v.lanes[3][1].highlighted);
        assert(v.road == "TEST ST" && v.aa_type == 7 && v.has_step_m && v.step_m == 320);
        KombiLanes k;
        assert(kombiLanes(v, k) && describeLanes(k) == "LEFT* STRAIGHT_ON STRAIGHT_ON STRAIGHT_ON");
        // the four parked-test junctions, as the car showed them
        const char* want[] = { "LEFT* STRAIGHT_ON STRAIGHT_ON STRAIGHT_ON", "LEFT STRAIGHT_ON STRAIGHT_ON RIGHT*",
                               "LEFT STRAIGHT_ON RIGHT*", "HALF_LEFT* HALF_LEFT* STRAIGHT_ON STRAIGHT_ON" };
        for (int j = 0; j < 4; ++j) {
            assert(viewFromMessages(msgs(navStateLanes(7, "R", demoLanes(j)), Bytes()), v));
            assert(kombiLanes(v, k) && describeLanes(k) == want[j]);
        }
        // an explicit is_highlighted 0 (as the phone sends it) is "not highlighted"
        Bytes d, ln, step, man, st;
        fInt(d, 1, 1); fInt(d, 2, 0);
        fBytes(ln, 1, d);
        fInt(man, 1, 8);
        fBytes(step, 1, man); fBytes(step, 2, msgStr(1, "R")); fBytes(step, 3, ln);
        fBytes(st, 1, step);
        assert(viewFromMessages(msgs(st, Bytes()), v) && v.lanes.size() == 1 && !v.lanes[0][0].highlighted);
        assert(kombiLanes(v, k) && k.size() == 1 && k[0].kind == kLaneOther && k[0].arrow == 1);
    }
    // lanes we can't map: no lane row at all (never a partial or wrong one)
    {
        NavView v;
        KombiLanes k;
        assert(viewFromMessages(msgs(navState(7, "TEST ST"), Bytes()), v) && v.lanes.empty() && !kombiLanes(v, k));
        std::vector<TLane> l = demoLanes(0);
        l[1] = lane(0, false);                                     // UNKNOWN shape
        assert(viewFromMessages(msgs(navStateLanes(7, "R", l), Bytes()), v) && !kombiLanes(v, k) && k.empty());
        l = demoLanes(0);
        l[2] = TLane();                                            // a lane without directions
        assert(viewFromMessages(msgs(navStateLanes(7, "R", l), Bytes()), v) && !kombiLanes(v, k));
        l = std::vector<TLane>(9, lane(1, false));                 // more than kLaneMax lanes
        assert(viewFromMessages(msgs(navStateLanes(7, "R", l), Bytes()), v) && !kombiLanes(v, k));
        l = std::vector<TLane>(8, lane(1, false));                 // 8 is fine
        assert(viewFromMessages(msgs(navStateLanes(7, "R", l), Bytes()), v) && kombiLanes(v, k) && k.size() == 8);
        // multi-direction lane: the highlighted direction wins, else the first; every shape maps
        l.clear();
        l.push_back(lane2(1, false, 5, true));
        l.push_back(lane2(6, false, 1, false));
        l.push_back(lane(7, false)); l.push_back(lane(8, false)); l.push_back(lane(9, false)); l.push_back(lane(3, false));
        assert(viewFromMessages(msgs(navStateLanes(7, "R", l), Bytes()), v) && kombiLanes(v, k));
        assert(describeLanes(k) == "RIGHT* TIGHT_LEFT TIGHT_RIGHT UTURN_LEFT UTURN_RIGHT HALF_RIGHT");
        assert(laneArrow(10) == -1 && laneArrow(-1) == -1);
    }
    printf("test_kombi_nav_msg OK\n");
    return 0;
}
