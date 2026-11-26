// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test: the 6WA cluster driver (cluster/hud6wa_display.hpp) and what it draws.
//   - shapes: every Android Auto maneuver type and roundabout exit gives items of 1..99 points that stay inside the
//     design square without clamping and inside the profile's box on the cluster; left and right mirror exactly
//   - texts: the street, distance and unit filter (cluster/hudterm.hpp toClusterText)
//   - batches: the exact calls for a maneuver, the next one, a distance and a clear, against a fake transport
//   - the driver's answers to the transport's states (failed, offline, factory navigation, lost work), shadow mode
//   - the guidance state machine (cluster/nav_core.hpp Core) driving the driver
//   - KombiNav with NBT_CLUSTER=6wa on a MOST client over the fake DsiClient: shadow mode sends nothing, live mode
//     sends to 82/00
// Every road name and distance here is invented.
#include "cluster/nav_core.hpp"
#include "cluster/mapbox_hud.hpp"
#include "cluster/hudterm_ring.hpp"
#include "cluster/hud_ring60.hpp"
#include "cluster/hud_unified.hpp"
#include "fake_dsi_client.hpp"
#include "kombi_nav_test_util.hpp"
#include <algorithm>
#include <utility>
#include <map>
#include <math.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

using namespace nbt;
using namespace nbt::hudterm;
using nbt::kombinav::ClusterDisplay;

using nbt::kombinav::Hud6waDisplay;
using nbt::kombinav::HudTexts;
using nbt::kombinav::KombiNav;
using nbt::kombinav::NavView;
using nbt::kombinav::Settings;

// ---- a fake transport ---------------------------------------------------------------------------------------------

struct FakePort : public Port {
    Status st;
    uint32_t ep;
    SubmitResult result;
    bool idle;
    bool wait;               // a batch waits behind the one in progress
    int resets;
    Status afterReset;
    std::vector<Batch> batches;
    int mw, mh;              // the picture buffer's size once "measured" (ImageGetSize); 0 = no answer
    bool claimable;          // claim() takes the HUD from the navigation we yield to
    int claims;
    const HudProfile& profile;
    explicit FakePort(const HudProfile& p = k6wa) : st(kReady), ep(0), result(kQueued), idle(true), wait(false), resets(0), afterReset(kReady), mw(0),
                 mh(0), claimable(false), claims(0), profile(p) {}
    SubmitResult submit(const Batch& b) {
        if (st != kReady) return kNotReady;
        for (size_t i = 0; i < b.calls.size(); ++i) assert(callOk(b.calls[i], handlesOf(profile)));
        assert(b.calls.size() <= Config().maxBatch);
        batches.push_back(b);
        return result;
    }
    Status status() const { return st; }
    uint32_t epoch() const { return ep; }
    bool waiting(int) const { return wait; }
    void reset() { ++resets; if (st == kFailed) st = afterReset; }
    bool waitIdle(uint32_t) { return st == kReady && idle; }
    bool measuredSize(int& w, int& h) const { if (!mw) return false; w = mw; h = mh; return true; }
    bool claim() { ++claims; if (st == kNavActive && claimable) st = kReady; return st == kReady; }
};

static std::vector<std::string> g_log;
static void logFn(const char* line, void*) {
    g_log.push_back(line);
    printf("  | %s\n", line);
}
static bool logged(const char* part) {
    for (size_t i = 0; i < g_log.size(); ++i)
        if (strstr(g_log[i].c_str(), part)) return true;
    return false;
}

static NavView view(int type, const char* road, const char* dist, int unit, int64_t m) {
    NavView v;
    v.aa_type = type;
    v.road = road;
    v.dist_text = dist;
    v.aa_unit = unit;
    v.has_step_m = m >= 0;
    v.step_m = m;
    return v;
}

// The arrow alone, as before junction views: the tests of the arrow's batches use it.
static Settings arrows() {
    Settings s;
    s.junction = false;
    return s;
}

static std::string text(const Call& c) { return decodeText(&c.payload[0], c.payload.size()); }

// ---- shapes ---------------------------------------------------------------------------------------------------------

static long area2(const std::vector<Point>& p) {
    long a = 0;
    for (size_t i = 0; i < p.size(); ++i) {
        const Point& q = p[i];
        const Point& r = p[(i + 1) % p.size()];
        a += (long)q.x * r.y - (long)r.x * q.y;
    }
    return a < 0 ? -a : a;
}

static void checkShape(const ShapeSpec& s, const char* what) {
    Shape d, f;
    drawShape(s, d);
    assert(!d.empty() && d.size() <= 5);
    fitShape(d, k6wa.glyph, f);
    assert(f.size() == d.size());
    for (size_t i = 0; i < d.size(); ++i) {
        assert(d[i].type == kPolygon);
        assert(d[i].pts.size() >= 3 && d[i].pts.size() <= (size_t)kMaxPoints);
        assert(area2(d[i].pts) > 0);
        for (size_t k = 0; k < d[i].pts.size(); ++k) {
            const Point& p = d[i].pts[k];
            if (p.x < 0 || p.x > kDesign || p.y < 0 || p.y > kDesign) {
                printf("%s: item %u point %u at %d,%d outside the design square\n", what, (unsigned)i, (unsigned)k,
                       p.x, p.y);
                assert(false);
            }
            const Point& q = f[i].pts[k];
            assert(q.x >= k6wa.glyph.x0 && q.x <= k6wa.glyph.x1 && q.y >= k6wa.glyph.y0 && q.y <= k6wa.glyph.y1);
            assert(q.x >= k6wa.canvas.x0 && q.x <= k6wa.canvas.x1 && q.y >= k6wa.canvas.y0 && q.y <= k6wa.canvas.y1);
        }
        std::vector<uint8_t> pl = addPolygon(5, (uint16_t)i, f[i].type, f[i].pts);
        assert(payloadShapeOk(kAddPolygon, pl));
    }
}

static void testShapesInside() {
    for (int t = 0; t <= 60; ++t) {
        char what[32];
        snprintf(what, sizeof(what), "type %d", t);
        checkShape(shapeFor(t, false, 0), what);
        checkShape(shapeFor(t, true, 90), what);
    }
    for (int a = 1; a <= 360; ++a) {
        char what[48];
        snprintf(what, sizeof(what), "roundabout exit %d", a);
        checkShape(shapeFor(34, true, a), what);       // anticlockwise
        checkShape(shapeFor(33, true, a), what);       // clockwise
    }
    for (int a = -180; a <= 180; ++a) {
        ShapeSpec s;
        s.angle = a;
        checkShape(s, "arrow sweep");
        s.stub = -a / 2;
        checkShape(s, "arrow with stub sweep");
    }
}

static void testShapeFamilies() {
    assert(describeSpec(shapeFor(36, false, 0)) == "arrow +0");
    assert(describeSpec(shapeFor(7, false, 0)) == "arrow -90");
    assert(describeSpec(shapeFor(8, false, 0)) == "arrow +90");
    assert(describeSpec(shapeFor(5, false, 0)) == "arrow -45");
    assert(describeSpec(shapeFor(10, false, 0)) == "arrow +135");
    assert(describeSpec(shapeFor(11, false, 0)) == "u-turn left");
    assert(describeSpec(shapeFor(12, false, 0)) == "u-turn right");
    assert(describeSpec(shapeFor(24, false, 0)) == "arrow +45 stub -20");
    assert(describeSpec(shapeFor(25, false, 0)) == "arrow -40 stub +40");
    for (int t = 39; t <= 42; ++t) assert(describeSpec(shapeFor(t, false, 0)) == "flag");
    assert(describeSpec(shapeFor(34, false, 0)) == "roundabout ccw exit unknown");   // no angle: no invented exit
    assert(describeSpec(shapeFor(34, true, 90)) == "roundabout ccw exit 90");
    assert(describeSpec(shapeFor(33, true, 270)) == "roundabout cw exit 270");
    assert(describeSpec(shapeFor(34, true, 400)) == "roundabout ccw exit unknown");  // out of range: unknown
    assert(describeSpec(shapeFor(0, false, 0)) == "arrow +0");                    // unknown: straight on
}

// Left and right are mirror images in the design square: x -> 1000 - x.
static void testMirror() {
    int pairs[][2] = { {5, 6}, {7, 8}, {9, 10}, {11, 12}, {23, 24}, {25, 26}, {27, 28} };
    for (size_t i = 0; i < sizeof(pairs) / sizeof(pairs[0]); ++i) {
        Shape l, r;
        drawShape(shapeFor(pairs[i][0], false, 0), l);
        drawShape(shapeFor(pairs[i][1], false, 0), r);
        assert(l.size() == r.size());
        for (size_t k = 0; k < l.size(); ++k) {
            assert(l[k].pts.size() == r[k].pts.size());
            for (size_t j = 0; j < l[k].pts.size(); ++j) {
                // Mirrored polygons run the other way round, so compare as point sets.
                bool found = false;
                for (size_t m = 0; m < r[k].pts.size() && !found; ++m)
                    found = r[k].pts[m].x == kDesign - l[k].pts[j].x && r[k].pts[m].y == l[k].pts[j].y;
                assert(found);
            }
        }
    }
}

// The roundabout's exit arrow (the last item) points where the direction of travel says.
static void testRoundaboutExit() {
    struct { int type, angle; int dx, dy; } c[] = {
        {34, 90, 1, 0}, {34, 180, 0, -1}, {34, 270, -1, 0},     // anticlockwise (right-hand traffic)
        {33, 90, -1, 0}, {33, 180, 0, -1}, {33, 270, 1, 0},     // clockwise (left-hand traffic)
    };
    for (size_t i = 0; i < sizeof(c) / sizeof(c[0]); ++i) {
        Shape s;
        drawShape(shapeFor(c[i].type, true, c[i].angle), s);
        const Point& tip = s.back().pts[3];
        int dx = tip.x - 500, dy = tip.y - 420;
        if (c[i].dx) assert(dx * c[i].dx > 300 && dy > -20 && dy < 20);
        else assert(dy * c[i].dy > 300 && dx > -20 && dx < 20);
    }
}

// ---- texts ----------------------------------------------------------------------------------------------------------

static void testTexts() {
    assert(toClusterText("Testroad Alpha", "-./", 32, false) == "Testroad Alpha");
    assert(toClusterText("Pr\xc3\xbc" "fweg \xc3\x84rger \xe2\x86\x92 Nirgendwo", "-./", 32, false) ==
           "Pruefweg Aerger Nirgendwo");
    assert(toClusterText("Gro\xc3\x9fweg Beta", "-./", 32, false) == "Grossweg Beta");
    assert(toClusterText("Caf\xc3\xa9 Lane, Sample", "-./", 32, false) == "Cafe Lane Sample");   // comma -> space
    assert(toClusterText("  Sample-Lane  St. 4/5 & Co ", "-./", 32, false) == "Sample-Lane St. 4/5 Co");
    assert(toClusterText("\xc5\x83\xc4\x85\xc5\xba\xc4\x99 Test", "", 32, false) == "Naze Test");   // invented
    assert(toClusterText("Emoji \xf0\x9f\x9a\x97 Way", "", 32, false) == "Emoji Way");
    assert(toClusterText("Bad \xff\xfe bytes", "", 32, false) == "Bad bytes");
    assert(toClusterText("Testroad Alpha Testroad Beta Testroad Gamma", "", 32, false) ==
           "Testroad Alpha Testroad Beta Tes");
    assert(toClusterText("Testroad Alpha Testroad Beta Tes ", "", 32, false).size() <= 32);
    assert(toClusterText("1,2", ".", 8, true) == "1.2");
    assert(toClusterText("0.3", ".", 8, true) == "0.3");
    // Cyrillic street names (NBT_HUD_CYRILLIC): blanked by default, kept verbatim with the opt-in (re-encoded to UTF-8,
    // then to UTF-16BE in setText). "\xd0\xa2\xd0\xb2\xd0\xb5\xd1\x80" is the UTF-8 for "Твер".
    assert(toClusterText("\xd0\xa2\xd0\xb2\xd0\xb5\xd1\x80", "", 32, false) == "");
    assert(toClusterText("\xd0\xa2\xd0\xb2\xd0\xb5\xd1\x80", "", 32, false, true) == "\xd0\xa2\xd0\xb2\xd0\xb5\xd1\x80");

    HudTexts t = kombinav::hudTexts(view(7, "Sample Lane Beta", "1,2", 3, 1200), k6wa);
    assert(t.street == "Sample Lane Beta" && t.distance == "1.2" && t.unit == "km");
    t = kombinav::hudTexts(view(7, " ", "-", 1, -1), k6wa);
    assert(t.street.empty() && t.distance.empty() && t.unit.empty());
    t = kombinav::hudTexts(view(7, "Testroad", "300", 0, 300), k6wa);
    assert(t.distance == "300" && t.unit.empty());
    const char* units[] = { "", "m", "km", "km", "mi", "mi", "ft", "yd", "" };
    for (int u = 0; u <= 8; ++u) assert(strcmp(kombinav::hudUnitText(u), units[u]) == 0);
}

// ---- batches --------------------------------------------------------------------------------------------------------

static void expectCall(const Call& c, uint16_t fkt, uint16_t h) {
    if (c.fkt != fkt || c.handle() != h) {
        printf("expected %03X h%u, got %s\n", fkt, h, describeCall(c).c_str());
        assert(false);
    }
}

static void testShowBatches() {
    g_log.clear();
    FakePort port;
    Hud6waDisplay d(port, k6wa, false);
    d.setSettings(arrows());
    d.setLog(&logFn, NULL);
    assert(strcmp(d.name(), "6wa") == 0 && !d.sentAny() && d.visibleBuffer() == 6);

    NavView v = view(8, "Testroad Alpha", "300", 1, 300);
    assert(d.showManeuver(v, 0, true, true) == ClusterDisplay::kOk);
    assert(port.batches.size() == 1 && d.sentAny() && d.visibleBuffer() == 5);
    const Batch& b = port.batches[0];
    assert(b.full && b.label == "maneuver");
    Shape s;
    drawShape(shapeFor(8, false, 0), s);
    size_t n = s.size(), i = 0;
    assert(b.calls.size() == 2 + n + 2 + 3 + 3 + 1);
    expectCall(b.calls[i++], kRemoveAllItems, 5);
    expectCall(b.calls[i], kSetFillColor, 5);
    assert(b.calls[i++].payload[2] == 4);
    for (size_t k = 0; k < n; ++k, ++i) {
        expectCall(b.calls[i], kAddPolygon, 5);
        assert(rdBe16(&b.calls[i].payload[2]) == k);
    }
    expectCall(b.calls[i], kSwitchVisibility, 6);
    assert(rdBe16(&b.calls[i++].payload[2]) == 5);
    expectCall(b.calls[i++], kRemoveAllItems, 6);
    for (uint16_t h = 2; h <= 4; ++h) {
        expectCall(b.calls[i], kSetVisibility, h);
        assert(b.calls[i++].payload[2] == 1);
    }
    expectCall(b.calls[i], kTextareaSetText, 2);
    assert(text(b.calls[i++]) == "Testroad Alpha");
    expectCall(b.calls[i], kTextareaSetText, 3);
    assert(text(b.calls[i++]) == "300");
    expectCall(b.calls[i], kTextareaSetText, 4);
    assert(text(b.calls[i++]) == "m");
    expectCall(b.calls[i], kSetVisibility, 1);
    assert(b.calls[i++].payload[2] == 1);
    assert(i == b.calls.size());
    assert(logged("SHOW type 8 -> arrow +90 | '300' 'm' | road 'Testroad Alpha' -> 'Testroad Alpha'"));

    // The next maneuver: into the other buffer, the texts already on.
    NavView v2 = view(7, "Sample Lane Beta", "0.2", 5, 320);
    assert(d.showManeuver(v2, 1, true, false) == ClusterDisplay::kOk);
    const Batch& b2 = port.batches[1];
    drawShape(shapeFor(7, false, 0), s);
    assert(b2.calls.size() == 2 + s.size() + 2 + 3 + 1 && d.visibleBuffer() == 6);
    expectCall(b2.calls[0], kRemoveAllItems, 6);
    expectCall(b2.calls[1], kSetFillColor, 6);
    expectCall(b2.calls[2 + s.size()], kSwitchVisibility, 5);
    assert(rdBe16(&b2.calls[2 + s.size()].payload[2]) == 6);
    expectCall(b2.calls[3 + s.size()], kRemoveAllItems, 5);
    assert(text(b2.calls[4 + s.size()]) == "Sample Lane Beta");
    assert(text(b2.calls[5 + s.size()]) == "0.2" && text(b2.calls[6 + s.size()]) == "mi");

    // Distances: only what changed, as a partial batch.
    NavView v3 = v2;
    v3.dist_text = "0.1";
    assert(d.updateDistance(v3, 2) == ClusterDisplay::kOk);
    assert(port.batches.size() == 3 && !port.batches[2].full && port.batches[2].calls.size() == 1);
    expectCall(port.batches[2].calls[0], kTextareaSetText, 3);
    assert(text(port.batches[2].calls[0]) == "0.1");
    NavView v4 = v2;
    v4.dist_text = "500";
    v4.aa_unit = 6;
    assert(d.updateDistance(v4, 3) == ClusterDisplay::kOk);
    assert(port.batches[3].calls.size() == 2 && text(port.batches[3].calls[1]) == "ft");
    assert(d.updateDistance(v4, 4) == ClusterDisplay::kOk && port.batches.size() == 4);   // nothing new
    assert(logged("DIST '500' 'ft'"));

    // Every maneuver type's batch keeps the limits (FakePort::submit checks each call and the length).
    for (int t = 0; t <= 60; ++t) assert(d.showManeuver(view(t, "Testroad", "1", 2, 1000), 5, true, false) == 0);
}

static void testF15Batches() {
    assert(k6wa.glyph.x0 == 70 && !k6wa.singlePicture);
    assert(k6waF15.bufA == 4 && k6waF15.bufB == 5 && k6waF15.singlePicture);
    assert(k6waF15.street == 0x0C && k6waF15.distance == 0x10 && k6waF15.unit == 0x0D);
    assert(k6waF15.glyph.x0 == 35 && k6waF15.glyph.y0 == 20 &&
           k6waF15.glyph.x1 == 136 && k6waF15.glyph.y1 == 110 && k6waF15.fill == 4);
    assert(!k6waF15.routeA && !k6waF15.laneA);
    // The F30-specific 176x126 calibration must not move the F15's already verified box.
    HudProfile fitted = k6waF15;
    fitMeasured(0, 0, fitted);
    assert(fitted.canvas.x0 == 35 && fitted.glyph.x1 == 136);
    fitMeasured(176, 126, fitted);
    assert(fitted.canvas.x1 == 175 && fitted.glyph.x0 == 35 && fitted.glyph.y1 == 110);
    fitMeasured(126, 77, fitted);
    assert(fitted.canvas.x1 == 125 && fitted.glyph.x1 < 126 && fitted.glyph.y1 < 77);
    FakePort port(k6waF15);
    Hud6waDisplay display(port, k6waF15, false);
    Settings settings = arrows();
    settings.hud_bar = true;
    display.setSettings(settings);
    NavView v = view(8, "Testroad Alpha", "300", 1, 300);
    assert(display.showManeuver(v, 0, true, true) == ClusterDisplay::kOk);
    assert(display.showManeuver(v, 1, true, true) == ClusterDisplay::kOk);
    assert(port.batches.size() == 2 && display.visibleBuffer() == 5);
    for (size_t n = 0; n < port.batches.size(); ++n) {
        bool panel = false, polygon = false, text = false, bar = false;
        const Batch& batch = port.batches[n];
        for (size_t i = 0; i < batch.calls.size(); ++i) {
            const Call& call = batch.calls[i];
            if (call.fkt == kSetVisibility && call.handle() == 1) panel = call.payload[2] == 1;
            if (call.fkt == kAddPolygon) {
                polygon = true;
                assert(call.handle() == 5);
            }
            if (call.fkt == kTextareaSetText) {
                assert(panel && (text || call.afterReplies));
                text = true;
            }
            assert(call.fkt != kSwitchVisibility);
            if (call.fkt == kPotentiometerSetValue) {
                bar = true;
                assert(call.handle() == 0x13 && call.payload[5] == 255 && call.payload[6] == 2);
            }
        }
        assert(panel && polygon && text && bar);
    }
    v.step_m = 150;
    assert(display.updateDistance(v, 2) == ClusterDisplay::kOk);
    assert(port.batches.back().calls.size() == 1);
    assert(port.batches.back().calls[0].fkt == kPotentiometerSetValue && port.batches.back().calls[0].payload[5] == 128);
    assert(display.clear(NULL));
    const Batch& clear = port.batches.back();
    assert(clear.calls.size() == 6);
    const uint16_t hide[] = {0x13, 0x0C, 0x10, 0x0D, 1};
    expectCall(clear.calls[0], kRemoveAllItems, 5);
    for (size_t i = 0; i < sizeof(hide) / sizeof(hide[0]); ++i) {
        expectCall(clear.calls[i + 1], kSetVisibility, hide[i]);
        assert(clear.calls[i + 1].payload[2] == 0);
    }
    assert(clear.calls.back().afterReplies);
}

static void testF15RouteRestart() {
    FakePort port(k6waF15);
    Hud6waDisplay display(port, k6waF15, false);
    Settings settings = arrows();
    settings.show_departure = true;
    settings.clear_route_end = true;
    nbt::kombinav::Core core(display, settings);
    NavView departure = view(1, "", "0", 1, 0);
    core.step(&departure, 0);
    core.step(&departure, 10);
    assert(core.shown() && port.batches.size() == 1);
    core.step(NULL, 11, "guidance ended", true);
    assert(!core.shown() && port.batches.back().label == "clear");
    core.idle("Song one", 11.2);
    assert(core.idleShown() && port.batches.back().label == "media");
    expectCall(port.batches.back().calls[0], kRemoveAllItems, 5);
    core.idle("", 11.3);                        // between tracks, the phone can briefly report nothing playing
    assert(!core.idleShown() && port.batches.back().label == "clear");
    core.idle("Song two", 11.4);
    assert(core.idleShown() && port.batches.back().label == "media");
    core.idle("Song three", 11.5);
    const Batch& title = port.batches.back();
    assert(title.label == "media-text" && title.calls.size() == 3);
    expectCall(title.calls[0], kSetVisibility, 1);
    expectCall(title.calls[1], kSetVisibility, 0x0C);
    expectCall(title.calls[2], kTextareaSetText, 0x0C);
    assert(title.calls[2].afterReplies && title.calls[2].allowTimeout);
    core.step(&departure, 12);
    assert(core.shown() && !core.idleShown() && port.batches.back().label == "maneuver");
    NavView turn = view(8, "Next Road", "100", 1, 100);
    core.step(&turn, 13);
    core.step(&departure, 14);
    assert(core.shown() && core.current().aa_type == 8);
}

static void testClearBatch() {
    FakePort port;
    Hud6waDisplay d(port, k6wa, false);
    d.setSettings(arrows());
    d.setLog(&logFn, NULL);
    assert(d.showManeuver(view(36, "Testroad", "100", 1, 100), 0, true, true) == ClusterDisplay::kOk);
    assert(d.visibleBuffer() == 5);
    std::string detail;
    assert(d.clear(&detail));
    const Batch c = port.batches.back();          // a copy: more batches follow
    assert(c.full && c.label == "clear" && c.calls.size() == 8);
    expectCall(c.calls[0], kSetVisibility, 1);
    assert(c.calls[0].payload[2] == 0);
    expectCall(c.calls[1], kRemoveAllItems, 5);
    expectCall(c.calls[2], kRemoveAllItems, 6);
    expectCall(c.calls[3], kSwitchVisibility, 5);
    assert(rdBe16(&c.calls[3].payload[2]) == 6);                 // the navigation's buffer left visible
    for (int k = 4; k <= 6; ++k) assert(c.calls[k].fkt == kTextareaSetText && text(c.calls[k]).empty());
    expectCall(c.calls[7], kSetVisibility, 2);
    assert(c.calls[7].payload[2] == 0);
    d.forget();
    assert(d.visibleBuffer() == 6 && !d.sentAny());

    // Buffer 6 on show by our count: the same clear, swap included, since a restart or lost work can leave 5 on show.
    assert(d.showManeuver(view(36, "Testroad", "100", 1, 100), 0, true, true) == ClusterDisplay::kOk);
    assert(d.showManeuver(view(7, "Testroad", "90", 1, 90), 1, true, false) == ClusterDisplay::kOk);
    assert(d.visibleBuffer() == 6 && d.clear(&detail));
    const Batch& c2 = port.batches.back();
    assert(c2.calls.size() == 8);
    for (size_t k = 0; k < 8; ++k) assert(c2.calls[k].fkt == c.calls[k].fkt && c2.calls[k].payload == c.calls[k].payload);
    Hud6waDisplay fresh(port, k6wa, false);                     // just started, nothing drawn yet: the same clear
    fresh.setSettings(arrows());
    assert(fresh.clear(&detail));
    assert(port.batches.back().calls.size() == 8 && port.batches.back().calls[3].payload == c.calls[3].payload);

    port.idle = false;                                           // the cluster does not answer everything
    assert(!d.clear(&detail) && strstr(detail.c_str(), "no answer"));
}

// A maneuver while a batch is still waiting is held, not put in its place: the waiting batch was built first and its
// calls (the texts switched on, the buffer swap) are what the next picture builds on.
static void testHold() {
    g_log.clear();
    FakePort port;
    Hud6waDisplay d(port, k6wa, false);
    d.setSettings(arrows());
    d.setLog(&logFn, NULL);
    assert(d.showManeuver(view(8, "Testroad Alpha", "300", 1, 300), 0, true, true) == ClusterDisplay::kOk);
    assert(port.batches.size() == 1 && d.visibleBuffer() == 5);
    port.wait = true;
    NavView b = view(7, "Sample Lane Beta", "150", 1, 150);
    assert(d.showManeuver(b, 0.1, true, false) == ClusterDisplay::kOk);
    assert(port.batches.size() == 1 && logged("HOLD type 7"));
    assert(d.sameManeuver(b, 0.35));                             // still waiting: held again, nothing sent
    assert(d.showManeuver(b, 0.35, false, false) == ClusterDisplay::kOk && port.batches.size() == 1);
    port.wait = false;
    assert(d.sameManeuver(b, 0.6));
    assert(d.showManeuver(b, 0.6, false, false) == ClusterDisplay::kOk && port.batches.size() == 2);
    const Batch& r = port.batches[1];
    expectCall(r.calls[0], kRemoveAllItems, 6);                  // into the buffer the first batch hid
    for (size_t i = 0; i < r.calls.size(); ++i) {
        if (r.calls[i].fkt == kSwitchVisibility) assert(r.calls[i].handle() == 5 && rdBe16(&r.calls[i].payload[2]) == 6);
        if (r.calls[i].fkt == kSetVisibility) assert(r.calls[i].handle() == 1);   // the texts are on already
        if (r.calls[i].fkt == kTextareaSetText && r.calls[i].handle() == 2) assert(text(r.calls[i]) == "Sample Lane Beta");
    }
    assert(d.visibleBuffer() == 6 && !d.sameManeuver(b, 0.7));

    // The state machine: the held maneuver goes out on the next step once nothing waits.
    FakePort p2;
    Hud6waDisplay d2(p2, k6wa, false);
    d2.setSettings(arrows());
    Settings s = arrows();
    kombinav::Core core(d2, s, &logFn, NULL);
    NavView a = view(8, "Testroad Alpha", "300", 1, 300);
    core.step(&a, 0);
    assert(p2.batches.size() == 1);
    p2.wait = true;
    core.step(&b, 0.25);
    core.step(&b, 0.5);
    assert(p2.batches.size() == 1 && core.shown());
    p2.wait = false;
    core.step(&b, 0.75);
    assert(p2.batches.size() == 2 && p2.batches[1].full);
    bool texts = false;
    for (size_t i = 0; i < p2.batches[1].calls.size(); ++i)
        texts = texts || (p2.batches[1].calls[i].fkt == kTextareaSetText && text(p2.batches[1].calls[i]) == "150");
    assert(texts);
    core.step(&b, 1.0);
    assert(p2.batches.size() == 2);
}

// The main-prompt reminder: one partial SetVisibility(frame, on), nothing before a picture, in shadow mode or while
// the factory navigation has the panel; through the state machine, once per maneuver at main_s.
static void testRemind() {
    g_log.clear();
    FakePort port;
    Hud6waDisplay d(port, k6wa, false);
    d.setSettings(arrows());
    d.setLog(&logFn, NULL);
    NavView v = view(8, "Testroad Alpha", "300", 1, 300);
    assert(d.remind(v, 0) == ClusterDisplay::kOk && port.batches.empty());   // nothing of ours is up
    assert(d.showManeuver(v, 0, true, true) == ClusterDisplay::kOk && port.batches.size() == 1);
    port.wait = true;                                            // a batch waits: partial, so merged, never a replacement
    assert(d.remind(v, 1) == ClusterDisplay::kOk && port.batches.size() == 2);
    const Batch& r = port.batches[1];
    assert(r.label == "remind" && !r.full && r.calls.size() == 1);
    expectCall(r.calls[0], kSetVisibility, 1);
    assert(r.calls[0].payload.size() == 3 && r.calls[0].payload[2] == 1);
    assert(logged("REMIND type 8: panel on again"));
    port.st = kNavActive;
    assert(d.remind(v, 2) == ClusterDisplay::kOk && port.batches.size() == 2);
    port.st = kFailed;
    assert(d.remind(v, 3) == ClusterDisplay::kRejected && port.batches.size() == 2);

    FakePort ps;
    Hud6waDisplay ds(ps, k6wa, true);
    ds.setSettings(arrows());
    ds.setLog(&logFn, NULL);
    assert(ds.showManeuver(v, 0, true, true) == ClusterDisplay::kOk);
    assert(ds.remind(v, 1) == ClusterDisplay::kOk && ps.batches.empty() && logged("[shadow] 204 SetVisibility h1 on"));

    FakePort p2;
    Hud6waDisplay d2(p2, k6wa, false);
    d2.setSettings(arrows());
    kombinav::Core core(d2, arrows(), &logFn, NULL);
    NavView a = view(8, "Testroad Alpha", "0.4", 5, 640);
    a.has_step_s = true;
    a.step_s = 60;
    core.step(&a, 0);
    assert(!core.shown() && p2.batches.empty());
    a.step_s = 45;
    core.step(&a, 1);
    assert(core.shown() && p2.batches.size() == 1 && p2.batches[0].full);
    a.step_s = 25;
    a.dist_text = "0.2"; a.step_m = 350;
    core.step(&a, 2);
    assert(p2.batches.size() == 2 && p2.batches[1].label == "distance");
    a.step_s = 18;
    core.step(&a, 3);
    assert(p2.batches.size() == 3 && p2.batches[2].label == "remind" && logged("MAIN type 8: 18 s, 350 m"));
    a.step_s = 9;
    core.step(&a, 4);
    assert(p2.batches.size() == 3);
}

// ---- junction views -------------------------------------------------------------------------------------------------

static void checkPicture(const Picture& pic, const char* what) {
    for (size_t i = 0; i < pic.size(); ++i) {
        const Poly& q = pic[i].poly;
        if (q.pts.size() < 3 || q.pts.size() > (size_t)kMaxPoints || area2(q.pts) == 0) {
            printf("%s: item %u has %u points, area2 %ld\n", what, (unsigned)i, (unsigned)q.pts.size(), area2(q.pts));
            assert(false);
        }
        for (size_t k = 0; k < q.pts.size(); ++k) {
            const Point& p = q.pts[k];
            if (p.x < -120 || p.x > 320 || p.y < -120 || p.y > 260) {
                printf("%s: item %u point %u at %d,%d far off the canvas\n", what, (unsigned)i, (unsigned)k, p.x, p.y);
                assert(false);
            }
        }
        assert(payloadShapeOk(kAddPolygon, addPolygon(5, (uint16_t)i, q.type, q.pts)));
    }
}

static bool onCanvas(const Point& p) {
    return p.x >= k6wa.canvas.x0 && p.x <= k6wa.canvas.x1 && p.y >= k6wa.canvas.y0 && p.y <= k6wa.canvas.y1;
}

static std::vector<std::pair<int, int> > allPoints(const Picture& pic, bool mirror) {
    std::vector<std::pair<int, int> > out;
    int bx = (k6wa.glyph.x0 + k6wa.glyph.x1) / 2;
    for (size_t i = 0; i < pic.size(); ++i)
        for (size_t k = 0; k < pic[i].poly.pts.size(); ++k)
            out.push_back(std::make_pair(mirror ? 2 * bx - pic[i].poly.pts[k].x : (int)pic[i].poly.pts[k].x,
                                         (int)pic[i].poly.pts[k].y));
    std::sort(out.begin(), out.end());
    return out;
}

// Every maneuver type, exit angle and exit number: items of 3..99 points with an area, near the canvas, the route's
// arrowhead on it, edges before roads, the stages' tails growing, and each batch within the transport's limit.
static void testJunctionGeometry() {
    const JunctionColours c = { 7, 3, 4 };
    const int angles[] = { -1, 1, 45, 90, 135, 180, 225, 270, 315, 360 };
    const int numbers[] = { 0, 1, 3, 12 };
    FakePort port;
    Hud6waDisplay d(port, k6wa, false);
    int views = 0;
    for (int t = 0; t <= 60; ++t)
        for (size_t a = 0; a < sizeof(angles) / sizeof(angles[0]); ++a)
            for (size_t n = 0; n < sizeof(numbers) / sizeof(numbers[0]); ++n) {
                char what[64];
                snprintf(what, sizeof(what), "type %d angle %d exit %d", t, angles[a], numbers[n]);
                Layout L;
                bool has = junctionLayout(t, angles[a] >= 0, angles[a], numbers[n] > 0, numbers[n], k6wa.glyph,
                                          k6wa.canvas.y1, L);
                int family = kombinav::kombiDirection(t);
                if (family == 10 || ((family == 14 || family == 15) && (angles[a] < 1 || angles[a] > 360))) {
                    assert(!has);   // destination, or unknown roundabout geometry: use the plain picture
                    continue;
                }
                assert(has && L.maxTail >= 8 && !L.roads.empty());
                assert(junctionFits(L, Hud6waDisplay::kStages, k6wa.canvas, k6wa.glyph, k6wa.canvas.y1 + 1, k6wa.glyph,
                                    k6wa.canvas.y1 + 1));   // the display's guard agrees (the F20: no scaling)
                ++views;
                Picture roads;
                drawRoads(L, c, kJunctionStyle, roads);
                checkPicture(roads, what);
                int changes = 0;
                for (size_t i = 0; i < roads.size(); ++i)
                    if (!i || roads[i].colour != roads[i - 1].colour) ++changes;
                assert(roads[0].colour == 7 && changes <= 3);
                assert(L.exitNumber == (L.ring ? numbers[n] : 0));
                Batch mb = d.pictureBatch("t", roads, HudTexts(), true);
                assert(mb.calls.size() <= Config().maxBatch);
                for (int st = 1; st <= Hud6waDisplay::kStages; ++st) {
                    if (st > 1)
                        assert(tailFor(L, st, Hud6waDisplay::kStages) > tailFor(L, st - 1, Hud6waDisplay::kStages));
                    Picture r;
                    drawRoute(L, tailFor(L, st, Hud6waDisplay::kStages), c, kJunctionStyle, r);
                    checkPicture(r, what);
                    assert(r.size() == 2 && r[0].colour == 7 && r[1].colour == 4);
                    size_t tip = (r[1].poly.pts.size() - 3) / 2 + 1;   // left edge, wing, tip, wing, right edge
                    if (!onCanvas(r[1].poly.pts[tip])) {
                        printf("%s: route tip at %d,%d off the canvas\n", what, r[1].poly.pts[tip].x,
                               r[1].poly.pts[tip].y);
                        assert(false);
                    }
                    assert(d.routeBatch(r).calls.size() <= Config().maxBatch);
                    Picture both = roads;                        // a profile without an overlay: one picture
                    both.insert(both.end(), r.begin(), r.end());
                    assert(d.pictureBatch("t", both, HudTexts(), true).calls.size() <= Config().maxBatch);
                }
            }
    assert(views > 1000);

    // Left and right turns, forks, exits, U-turns and the two ways round a roundabout mirror each other exactly.
    const int pairs[][2] = { {5, 6}, {7, 8}, {9, 10}, {11, 12}, {13, 14}, {21, 22}, {25, 26}, {27, 28}, {32, 30} };
    for (size_t i = 0; i < sizeof(pairs) / sizeof(pairs[0]); ++i) {
        Layout l, r;
        assert(junctionLayout(pairs[i][0], true, 90, false, 0, k6wa.glyph, k6wa.canvas.y1, l));
        assert(junctionLayout(pairs[i][1], true, 90, false, 0, k6wa.glyph, k6wa.canvas.y1, r));
        Picture pl, pr;
        drawRoads(l, c, kJunctionStyle, pl);
        drawRoute(l, l.maxTail, c, kJunctionStyle, pl);
        drawRoads(r, c, kJunctionStyle, pr);
        drawRoute(r, r.maxTail, c, kJunctionStyle, pr);
        if (allPoints(pl, false) != allPoints(pr, true)) {
            fprintf(stderr, "types %d and %d don't mirror\n", pairs[i][0], pairs[i][1]);
            assert(false);
        }
    }
}

// Mapbox glyphs (cluster/mapbox_hud.hpp) are opt-in: unset or off draws the parametric shapes the tests above count;
// on, every maneuver they have a picture for comes as valid convex pieces near the canvas.
static void testMapboxGlyphs() {
    Picture none;
    unsetenv("NBT_HUD_GLYPHS");
    assert(!mbxManeuver(shapeFor(8, false, 0), k6wa, none) && none.empty());   // default: the parametric shape
    setenv("NBT_HUD_GLYPHS", "off", 1);
    assert(!mbxManeuver(shapeFor(8, false, 0), k6wa, none) && none.empty());
    setenv("NBT_HUD_GLYPHS", "1", 1);
    int drawn = 0;
    for (int t = 0; t <= 60; ++t)
        for (int a = 0; a <= 360; a += 90) {
            Picture pic;
            if (!mbxManeuver(shapeFor(t, true, a), k6wa, pic)) continue;
            char what[48];
            snprintf(what, sizeof(what), "glyph type %d angle %d", t, a);
            checkPicture(pic, what);
            ++drawn;
        }
    unsetenv("NBT_HUD_GLYPHS");
    assert(drawn > 0);
    printf("  Mapbox glyphs: opt-in, %d pictures valid\n", drawn);
}

// AA exit angles are measured from the entry, for both traffic directions; CarPlay uses a heading instead.
static void testMapboxRoundaboutExit() {
    HudProfile profiles[] = { k6wa, k6wa };
    fitMeasured(126, 77, profiles[1]);
    for (size_t p = 0; p < sizeof(profiles) / sizeof(profiles[0]); ++p)
        for (int cw = 0; cw <= 1; ++cw)
            for (int angle = 1; angle <= 360; ++angle) {
                Picture pic;
                mbxRoundabout(angle, cw != 0, profiles[p], pic);
                checkPicture(pic, "Mapbox roundabout");
                rb::P16 c = rb::centre(angle);
                Point centre = mbxPoint(rb::boxX(c.x), rb::boxY(c.y), cw != 0, profiles[p].glyph);
                Point tip = centre;
                long farthest = 0;
                for (size_t i = 0; i < pic.size(); ++i)
                    for (size_t j = 0; j < pic[i].poly.pts.size(); ++j) {
                        Point q = pic[i].poly.pts[j];
                        long dx = q.x - centre.x, dy = q.y - centre.y, distance = dx * dx + dy * dy;
                        if (distance > farthest) { farthest = distance; tip = q; }
                    }
                double rad = angle * 3.141592653589793 / 180;
                double x = sin(rad) * (cw ? -1 : 1), y = cos(rad);
                int dx = tip.x - centre.x, dy = tip.y - centre.y;
                assert(dx * x + dy * y > 20);
                assert(fabs(dx * y - dy * x) < 2);
            }
}

// sabifa's test (#264), on a 6WA with a HUD (a 120 x 72 picture, the factory arrow box 37,23-82,68): the junction
// view, laid out on the F20 and scaled onto it, keeps the route's arrowhead on the canvas, the ring and the roads
// no wider than the factory's here (roads 11, the route 6), and every item a valid call.
static void testJunctionScaled() {
    HudProfile p = k6wa;
    const Rect canvas = { 0, 0, 119, 71 }, glyph = { 37, 23, 82, 68 };
    p.canvas = canvas;
    p.glyph = glyph;
    const JunctionColours c = { 7, 3, 4 };
    for (int t = 0; t <= 60; ++t) {
        Layout L;
        if (!junctionLayout(t, true, 90, true, 2, k6wa.glyph, k6wa.canvas.y1, L)) continue;
        Picture pic;
        drawRoads(L, c, kJunctionStyle, pic);
        drawRoute(L, L.maxTail, c, kJunctionStyle, pic);
        fitPicture(pic, k6wa.glyph, k6wa.canvas.y1 + 1, p.glyph, p.canvas.y1 + 1);
        for (size_t i = 0; i < pic.size(); ++i)
            assert(payloadShapeOk(kAddPolygon, addPolygon(5, (uint16_t)i, pic[i].poly.type, pic[i].poly.pts)));
        const Poly& route = pic.back().poly;
        const Point& tip = route.pts[(route.pts.size() - 3) / 2 + 1];
        if (tip.x < p.canvas.x0 || tip.x > p.canvas.x1 || tip.y < p.canvas.y0 || tip.y > p.canvas.y1) {
            printf("type %d: route tip at %d,%d off the 120 x 72 canvas\n", t, tip.x, tip.y);
            assert(false);
        }
    }
    Picture one;
    Item it;
    it.colour = 4;
    it.poly = shapes::bar(120, 129, 0, 100, kJunctionStyle.roadW);    // a road on the F20, 22 wide
    one.push_back(it);
    fitPicture(one, k6wa.glyph, k6wa.canvas.y1 + 1, p.glyph, p.canvas.y1 + 1);
    int w = one[0].poly.pts[3].x - one[0].poly.pts[0].x;
    assert(w >= 11 && w <= 13);                                     // 22 * 72 / 130
}

static void testDigits() {
    for (int n = 0; n <= 9; ++n) {
        Shape s;
        digits::drawNumber(n, 50, 10, 18, s);
        assert(s.size() == (n == 0 || n == 4 || n == 6 || n == 8 || n == 9 ? 2u : 1u));
        for (size_t i = 0; i < s.size(); ++i) {
            assert(s[i].pts.size() >= 3 && area2(s[i].pts) > 0);
            for (size_t k = 0; k < s[i].pts.size(); ++k)
                assert(s[i].pts[k].x >= 45 && s[i].pts[k].x <= 55 && s[i].pts[k].y >= 10 && s[i].pts[k].y <= 28);
        }
    }
    Shape s;
    digits::drawNumber(808, 100, 0, 20, s);                      // 8 fills its cell, so its ink shows the centring
    int x0 = 999, x1 = -999;
    for (size_t i = 0; i < s.size(); ++i)
        for (size_t k = 0; k < s[i].pts.size(); ++k) {
            if (s[i].pts[k].x < x0) x0 = s[i].pts[k].x;
            if (s[i].pts[k].x > x1) x1 = s[i].pts[k].x;
        }
    assert(s.size() == 6 && x0 + x1 >= 198 && x0 + x1 <= 202);
}

static kombinav::AaLane oneLane(int shape, bool take) {
    return kombinav::AaLane(1, kombinav::LaneDirection(shape, take));
}

// Read the actual wire polygons so caption checks do not depend on the production layout helper.
static Shape packetPolygons(const Batch& batch) {
    Shape out;
    for (size_t i = 0; i < batch.calls.size(); ++i) {
        const Call& c = batch.calls[i];
        if (c.fkt != kAddPolygon) continue;
        Poly p; p.type = (PolyType)c.payload[4];
        for (size_t j = 0; j < c.payload[5]; ++j)
            p.pts.push_back(shapes::pt((int16_t)rdBe16(&c.payload[6 + j * 4]),
                                      (int16_t)rdBe16(&c.payload[8 + j * 4])));
        out.push_back(p);
    }
    return out;
}

static Rect boundsForTest(const Shape& s) {
    assert(!s.empty() && !s[0].pts.empty());
    Rect b = {s[0].pts[0].x, s[0].pts[0].y, s[0].pts[0].x, s[0].pts[0].y};
    for (size_t i = 0; i < s.size(); ++i)
        for (size_t j = 0; j < s[i].pts.size(); ++j) {
            b.x0 = std::min(b.x0, s[i].pts[j].x); b.x1 = std::max(b.x1, s[i].pts[j].x);
            b.y0 = std::min(b.y0, s[i].pts[j].y); b.y1 = std::max(b.y1, s[i].pts[j].y);
        }
    return b;
}

static void samePolygons(const Shape& actual, size_t offset, const Shape& expected, int dx = 0, int dy = 0) {
    assert(actual.size() >= offset + expected.size());
    for (size_t i = 0; i < expected.size(); ++i) {
        const Poly& a = actual[offset + i]; const Poly& e = expected[i];
        assert(a.type == e.type && a.pts.size() == e.pts.size());
        for (size_t j = 0; j < e.pts.size(); ++j) {
            if (a.pts[j].x != e.pts[j].x + dx || a.pts[j].y != e.pts[j].y + dy)
                fprintf(stderr, "polygon mismatch offset=%u poly=%u point=%u: actual=%d,%d expected=%d,%d\n",
                        (unsigned)offset, (unsigned)i, (unsigned)j, a.pts[j].x, a.pts[j].y,
                        e.pts[j].x + dx, e.pts[j].y + dy);
            assert(a.pts[j].x == e.pts[j].x + dx && a.pts[j].y == e.pts[j].y + dy);
        }
    }
}

// Independent analytic reference for the selected SVG: 92-degree circular arcs, tangent 18x10 heads,
// 0.4-unit quadratic corner trims and rotations of 120 degrees. Oversample here, not from production tables.
static Shape approvedCirculation() {
    const double pi = 3.141592653589793, outer = 26.296875, inner = 18.046875;
    const double middle = (outer+inner)/2;
    Shape out;
    for (int arrow = 0; arrow < 3; ++arrow) {
        std::vector<std::pair<double,double> > q;
        for (int angle = -92; angle <= 0; ++angle) {
            double a = angle*pi/180;
            q.push_back(std::make_pair(outer*sin(a),-outer*cos(a)));
        }
        const double head[][2] = {{0,-outer},{0,-middle-9},{10,-middle},{0,-middle+9},{0,-inner}};
        for (int corner = 1; corner < 4; ++corner) {
            double bx = head[corner-1][0]-head[corner][0], by = head[corner-1][1]-head[corner][1];
            double ax = head[corner+1][0]-head[corner][0], ay = head[corner+1][1]-head[corner][1];
            double before = sqrt(bx*bx+by*by), after = sqrt(ax*ax+ay*ay);
            bx = head[corner][0]+0.4*bx/before; by = head[corner][1]+0.4*by/before;
            ax = head[corner][0]+0.4*ax/after; ay = head[corner][1]+0.4*ay/after;
            for (int step = 0; step <= 4; ++step) {
                double t = step/4.0, u = 1-t;
                q.push_back(std::make_pair(u*u*bx+2*u*t*head[corner][0]+t*t*ax,
                                          u*u*by+2*u*t*head[corner][1]+t*t*ay));
            }
        }
        for (int angle = 0; angle >= -92; --angle) {
            double a = angle*pi/180;
            q.push_back(std::make_pair(inner*sin(a),-inner*cos(a)));
        }
        Poly poly; poly.type = kPolygon;
        double a = arrow*120*pi/180;
        for (size_t i = 0; i < q.size(); ++i)
            poly.pts.push_back(shapes::pt((int)floor(62+q[i].first*cos(a)-q[i].second*sin(a)+0.5),
                                          (int)floor(34+q[i].first*sin(a)+q[i].second*cos(a)+0.5)));
        out.push_back(poly);
    }
    Poly entrance; entrance.type = kPolygon;
    const int xy[] = {58,52,66,52,66,76,58,76};
    for (int i = 0; i < 4; ++i) entrance.pts.push_back(shapes::pt(xy[2*i],xy[2*i+1]));
    out.push_back(entrance);
    return out;
}

static void sameRect(const Rect& a, const Rect& b) {
    assert(a.x0 == b.x0 && a.x1 == b.x1 && a.y0 == b.y0 && a.y1 == b.y1);
}

static void checkCirculation(const Shape& actual, const HudProfile& p, const Rect& frame,
                             int number, bool clockwise, bool standardHud = false) {
    sameRect(ringGlyphBox(p), frame);
    const RingLayout layout = circulationLayout(p);
    int wanted = std::min(frame.x1-frame.x0, frame.y1-frame.y0);
    assert(layout.side > 0 && layout.side <= wanted);
    // The reference fits a symmetric 54-by-74 envelope at side 66, including its entrance.
    // Permit its subpixel extrema, but reject gratuitous shrinking even if the resulting picture would fit.
    int available = std::min((p.canvas.x1-p.canvas.x0)*66/54, (p.canvas.y1-p.canvas.y0)*66/74);
    assert(layout.side >= std::min(wanted, available)-1);
    if (standardHud) assert(layout.cx == 62 && layout.cy == 34 && layout.side == 66);
    const Shape approved = approvedCirculation();
    assert(actual.size() >= approved.size());
    for (size_t i = 0; i < approved.size(); ++i) {
        assert(actual[i].type == kPolygon && actual[i].pts.size() <= approved[i].pts.size());
        // Compare contour segments in both directions: rounding may remove a redundant native vertex.
        std::vector<std::pair<double,double> > paths[2];
        for (size_t j = 0; j < actual[i].pts.size(); ++j)
            paths[0].push_back(std::make_pair(actual[i].pts[j].x,actual[i].pts[j].y));
        for (size_t j = 0; j < approved[i].pts.size(); ++j) {
            double x = approved[i].pts[j].x-62, y = approved[i].pts[j].y-34;
            if (i < 3 && !clockwise) x = -x;
            paths[1].push_back(std::make_pair(layout.cx+x*layout.side/66,layout.cy+y*layout.side/66));
        }
        double tolerance = 0.51+0.51*layout.side/66;
        for (int direction = 0; direction < 2; ++direction) {
            const std::vector<std::pair<double,double> >& from = paths[direction];
            const std::vector<std::pair<double,double> >& to = paths[1-direction];
            for (size_t j = 0; j < from.size(); ++j) {
                bool close = false;
                for (size_t k = 0; k < to.size(); ++k) {
                    const std::pair<double,double>& a = to[k];
                    const std::pair<double,double>& b = to[k+1 < to.size() ? k+1 : 0];
                    double dx = b.first-a.first, dy = b.second-a.second, length2 = dx*dx+dy*dy;
                    double t = length2 ? ((from[j].first-a.first)*dx+(from[j].second-a.second)*dy)/length2 : 0;
                    t = std::max(0.0,std::min(1.0,t));
                    if (fabs(from[j].first-a.first-t*dx) <= tolerance &&
                        fabs(from[j].second-a.second-t*dy) <= tolerance) { close = true; break; }
                }
                if (!close) fprintf(stderr,"circulation reference: side%d shape%zu direction%d point%zu\n",
                                    layout.side,i,direction,j);
                assert(close);
            }
        }
    }
    const Poly& entrance = actual[3];
    assert(entrance.pts.size() == 4);     // plain attachment: no incoming arrowhead
    assert(entrance.pts[0].x == entrance.pts[3].x && entrance.pts[1].x == entrance.pts[2].x);
    assert(entrance.pts[0].y == entrance.pts[1].y && entrance.pts[2].y == entrance.pts[3].y);
    assert(entrance.pts[0].x < entrance.pts[1].x && entrance.pts[0].y < entrance.pts[3].y);
    for (size_t i = 0; i < actual.size(); ++i) {
        assert(actual[i].type == kPolygon && actual[i].pts.size() >= 3 && actual[i].pts.size() <= 99);
        assert(area2(actual[i].pts) > 0);
        for (size_t j = 0; j < actual[i].pts.size(); ++j) {
            Point q = actual[i].pts[j];
            assert(q.x >= p.canvas.x0 && q.x <= p.canvas.x1 && q.y >= p.canvas.y0 && q.y <= p.canvas.y1);
        }
    }
    int h = layout.side * (number > 9 ? 15 : 24) / 66;
    if (layout.side < 66) --h;
    if (number < 1 || number > 99 || h < 6) { assert(actual.size() == 4); return; }
    if (standardHud) assert(h == (number > 9 ? 15 : 24));
    Shape expected;
    digits::drawNumber(number, layout.cx, layout.cy - h/2, h, expected);
    assert(actual.size() == 4 + expected.size());
    samePolygons(actual, 4, expected);   // the central text is upright in both traffic directions
    Shape body(actual.begin(), actual.begin()+4);
    Rect before = boundsForTest(body), after = boundsForTest(actual);
    sameRect(before, after);            // a number cannot enlarge, shrink or displace the ring
}

static void checkCirculationPacket(const Batch& b, const HudProfile& p, const Rect& frame,
                                   int number, bool clockwise, bool standardHud = false) {
    if (standardHud) {
        Picture expected;
        assert(ring60::picture(shapeFor(clockwise ? 33 : 34, false, 0), p, expected, number));
        Shape shapes;
        for (size_t i = 0; i < expected.size(); ++i) shapes.push_back(expected[i].poly);
        Shape actual = packetPolygons(b);
        assert(actual.size() == shapes.size());
        samePolygons(actual, 0, shapes);
    } else checkCirculation(packetPolygons(b), p, frame, number, clockwise);
    int colour = -1;
    for (size_t i = 0; i < b.calls.size(); ++i) {
        if (b.calls[i].fkt == kSetFillColor) colour = b.calls[i].payload[2];
        if (b.calls[i].fkt == kAddPolygon) assert(colour == p.fill);
    }
    assert(b.calls.size() <= Config().maxBatch && b.calls.size() <= 64);
}

// Missing/invalid angle never invents an exit direction: show circulation and only the supplied valid number.
static void testUnknownRoundabout() {
    assert(shapeFor(34, false, 0).exit == 0);
    assert(describeSpec(shapeFor(34, false, 0)) == "roundabout ccw exit unknown");
    const int widths[] = {0,126,176,120}, heights[] = {0,77,126,72};
    const Rect expanded[] = {{57,4,182,129},{29,9,95,75},{44,29,122,120},{29,9,91,71}};
    struct ExitNumber { bool has; int value, shown; };
    const ExitNumber numbers[] = {
        {false, 5, 0}, {true, 0, 0}, {true, -1, 0}, {true, 1, 1},
        {true, 2, 2}, {true, 3, 3}, {true, 4, 4}, {true, 5, 5}, {true, 6, 6}, {true, 7, 7}, {true, 8, 8},
        {true, 9, 9}, {true, 10, 10}, {true, 12, 12}, {true, 99, 99}, {true, 100, 0}
    };
    struct ExitAngle { bool has; int value; };
    const ExitAngle angles[] = {{false,0},{false,90},{true,-1},{true,0},{true,361},{true,400}};
    unsetenv("NBT_HUD_CANVAS");
    unsigned cases = 0;
    for (int mb = 0; mb <= 1; ++mb) {
        setenv("NBT_HUD_GLYPHS", mb ? "1" : "0", 1);
        for (int measured = 0; measured < 4; ++measured)
            for (int pinned = 0; pinned <= 1; ++pinned) {
                if (pinned) setenv("NBT_HUD_GLYPH", "4 5 34 35", 1);
                else unsetenv("NBT_HUD_GLYPH");
                HudProfile profile = k6wa; applyHudEnv(profile);
                if (measured) fitMeasured(widths[measured], heights[measured], profile);
                Rect box = pinned ? profile.glyph : expanded[measured];
                for (int type = 33; type <= 34; ++type)
                    for (int junction = 0; junction <= 1; ++junction)
                        for (size_t n = 0; n < sizeof(numbers)/sizeof(numbers[0]); ++n)
                            for (size_t a = 0; a < sizeof(angles)/sizeof(angles[0]); ++a) {
                                FakePort port; Hud6waDisplay display(port, profile, false);
                                Settings settings = arrows(); settings.junction = junction != 0; display.setSettings(settings);
                                NavView v = view(type, "", "580", 1, 580);
                                v.has_exit_number = numbers[n].has; v.exit_number = numbers[n].value;
                                v.has_exit_angle = angles[a].has; v.exit_angle = angles[a].value;
                                ShapeSpec spec = shapeFor(type, angles[a].has, angles[a].value);
                                assert(spec.exit == 0);
                                assert(display.showManeuver(v, 0, true, true) == ClusterDisplay::kOk);
                                assert(!display.nearShown() && port.batches.size() == 1);
                                checkCirculationPacket(port.batches[0], profile, box, numbers[n].shown,
                                                       spec.clockwise, measured == 1 && !pinned);
                                Picture unknown;
                                assert(!mbxManeuver(spec, profile, unknown) && unknown.empty());
                                Layout layout;
                                assert(!junctionLayout(type, angles[a].has, angles[a].value, numbers[n].has,
                                                       numbers[n].value, profile.glyph, profile.canvas.y1, layout));
                                ++cases;
                            }
            }
    }
    assert(cases == 6144);
    unsetenv("NBT_HUD_GLYPH"); unsetenv("NBT_HUD_GLYPHS");

    // Refit from the original profile as guidance moves HUD -> cluster -> HUD -> MINI -> HUD.
    FakePort port; Hud6waDisplay display(port, k6wa, false); display.setSettings(arrows());
    NavView v = view(34, "", "580", 1, 580); v.has_exit_number = true; v.exit_number = 4;
    const int moves[] = {1,2,1,3,1};
    for (size_t i = 0; i < sizeof(moves)/sizeof(moves[0]); ++i) {
        int index = moves[i]; port.mw = widths[index]; port.mh = heights[index];
        assert(display.showManeuver(v, (double)i, i == 0, true) == ClusterDisplay::kOk);
        assert(!display.nearShown() && port.batches.size() == i+1);
        HudProfile profile = k6wa; fitMeasured(widths[index], heights[index], profile);
        checkCirculationPacket(port.batches.back(), profile, expanded[index], 4, false, index == 1);
    }
}

static void testUnknownCanvasPins() {
    unsetenv("NBT_HUD_GLYPH");
    const char* pins[] = {"70 20 170 120", "10 20 135 96"};
    const Rect expected[] = {{70,20,170,120},{39,29,105,95}};
    for (size_t p = 0; p < sizeof(pins)/sizeof(pins[0]); ++p) {
        setenv("NBT_HUD_CANVAS", pins[p], 1);
        HudProfile profile = k6wa; applyHudEnv(profile);
        // P2 is a fitting-box regression, independent of the newly chosen circulation contour and caption.
        sameRect(ringGlyphBox(profile), expected[p]);
        for (int type = 33; type <= 34; ++type) {
            FakePort port; Hud6waDisplay display(port, profile, false); display.setSettings(arrows());
            NavView v = view(type, "", "80", 1, 80); v.has_exit_number = true; v.exit_number = 7;
            assert(display.showManeuver(v, 0, true, true) == ClusterDisplay::kOk);
            checkCirculationPacket(port.batches.back(), profile, expected[p], 7, type == 33);
        }
    }
    setenv("NBT_HUD_CANVAS", "10 20 135 96", 1); setenv("NBT_HUD_GLYPH", "40 30 70 60", 1);
    HudProfile profile = k6wa; applyHudEnv(profile);
    FakePort port; Hud6waDisplay display(port, profile, false); display.setSettings(arrows());
    NavView v = view(34, "", "80", 1, 80); v.has_exit_number = true; v.exit_number = 12;
    assert(display.showManeuver(v, 0, true, true) == ClusterDisplay::kOk);
    checkCirculationPacket(port.batches.back(), profile, profile.glyph, 12, false);
    unsetenv("NBT_HUD_GLYPH"); unsetenv("NBT_HUD_CANVAS");
}

static Shape picturePolygons(const Picture& picture) {
    Shape result;
    for (size_t i = 0; i < picture.size(); ++i) result.push_back(picture[i].poly);
    return result;
}

static void testCirculationGeometry() {
    const int widths[] = {0,126,176,120}, heights[] = {0,77,126,72};
    const Rect frames[] = {{57,4,182,129},{29,9,95,75},{44,29,122,120},{29,9,91,71}};
    const int numbers[] = {0,1,2,3,4,5,6,7,8,12,99,-1,100};
    for (size_t p = 0; p < 4; ++p) {
        HudProfile profile = k6wa; if (p) fitMeasured(widths[p], heights[p], profile);
        profile.nHide = 8;
        for (int i = 0; i < 8; ++i) profile.hide[i] = 20+i;
        for (size_t n = 0; n < sizeof(numbers)/sizeof(numbers[0]); ++n) {
            Picture cw, ccw;
            assert(roundaboutPicture(shapeFor(33, false, 0), profile, cw, numbers[n]));
            assert(roundaboutPicture(shapeFor(34, false, 0), profile, ccw, numbers[n]));
            Shape a = picturePolygons(cw), b = picturePolygons(ccw);
            checkCirculation(a, profile, frames[p], numbers[n], true, p == 1);
            checkCirculation(b, profile, frames[p], numbers[n], false, p == 1);
            int cx = circulationLayout(profile).cx;
            assert(a.size() == b.size());
            for (size_t i = 0; i < a.size(); ++i) {
                assert(cw[i].colour == profile.fill && ccw[i].colour == profile.fill);
                assert(a[i].pts.size() == b[i].pts.size());
                for (size_t j = 0; j < a[i].pts.size(); ++j) {
                    assert(a[i].pts[j].y == b[i].pts[j].y);
                    if (i < 3) assert(a[i].pts[j].x + b[i].pts[j].x == 2*cx);
                    else assert(a[i].pts[j].x == b[i].pts[j].x);   // entrance and text are never flipped
                }
            }
            FakePort port; Hud6waDisplay display(port, profile, false);
            Batch batch = display.pictureBatch("circulation", cw, HudTexts(), true);
            assert(batch.calls.size() <= Config().maxBatch && batch.calls.size() <= 64);
            for (size_t i = 0; i < batch.calls.size(); ++i) assert(callOk(batch.calls[i], handlesOf(profile)));
        }
    }
    // A thin canvas and a tall/narrow glyph pin must reduce both axes uniformly, without clipping or oval rings.
    const char* canvases[] = {"10 20 29 99", "10 20 135 96"};
    const char* glyphs[] = {"11 21 28 98", "40 30 60 90"};
    for (size_t i = 0; i < 2; ++i) {
        setenv("NBT_HUD_CANVAS", canvases[i], 1); setenv("NBT_HUD_GLYPH", glyphs[i], 1);
        HudProfile profile = k6wa; applyHudEnv(profile);
        for (int cw = 0; cw <= 1; ++cw) {
            const int labels[] = {0,7,12};
            for (size_t n = 0; n < 3; ++n) {
                Picture pic;
                int h = circulationLayout(profile).side * (labels[n] > 9 ? 15 : 24) / 66;
                if (circulationLayout(profile).side < 66) --h;
                bool ok = roundaboutPicture(shapeFor(cw ? 33 : 34, false, 0), profile, pic, labels[n]);
                assert(ok == (!labels[n] || h >= 6));
                checkCirculation(picturePolygons(pic), profile, profile.glyph, labels[n], cw != 0);
            }
        }
    }
    unsetenv("NBT_HUD_GLYPH"); unsetenv("NBT_HUD_CANVAS");
}

// Known exits retain a radial arrow outside a circular ring; the circulation fallback is reserved for unknown angles.
static bool containsPoint(const Poly& p, double x, double y) {
    bool inside = false;
    for (size_t i = 0, j = p.pts.size()-1; i < p.pts.size(); j = i++) {
        Point a = p.pts[i], b = p.pts[j];
        if ((a.y > y) != (b.y > y) && x < (b.x-a.x)*(y-a.y)/(b.y-a.y)+a.x) inside = !inside;
    }
    return inside;
}

static int pictureColourAt(const Picture& p, double x, double y) {
    int colour = -1;
    for (size_t i = 0; i < p.size(); ++i)
        if (containsPoint(p[i].poly, x, y)) colour = p[i].colour;
    return colour;
}

static int roundForTest(double n) { return n < 0 ? (int)ceil(n-0.5) : (int)floor(n+0.5); }

static void nearPointSet(const Poly& actual, const Shape& expected, int tolerance = 1) {
    assert(expected.size() == 1 && actual.pts.size() == expected[0].pts.size());
    for (size_t i = 0; i < expected[0].pts.size(); ++i) {
        bool found = false;
        for (size_t j = 0; j < actual.pts.size(); ++j)
            if (abs(actual.pts[j].x-expected[0].pts[i].x) <= tolerance &&
                abs(actual.pts[j].y-expected[0].pts[i].y) <= tolerance) found = true;
        assert(found);
    }
}

static Shape expectedPolygon(const double* xy, size_t n, const RingLayout& l) {
    Poly poly; poly.type = kPolygon;
    for (size_t i = 0; i < n; ++i)
        poly.pts.push_back(shapes::pt(l.cx+roundForTest(xy[2*i]), l.cy+roundForTest(xy[2*i+1])));
    Shape out; out.push_back(poly); return out;
}

static void checkPictureBounds(const Picture& picture, const HudProfile& profile) {
    for (size_t i = 0; i < picture.size(); ++i) {
        const Poly& poly = picture[i].poly;
        assert(poly.type == kPolygon && poly.pts.size() >= 3 && poly.pts.size() <= 99);
        assert(area2(poly.pts) > 0);
        for (size_t j = 0; j < poly.pts.size(); ++j) {
            Point q = poly.pts[j];
            assert(q.x >= profile.canvas.x0 && q.x <= profile.canvas.x1);
            assert(q.y >= profile.canvas.y0 && q.y <= profile.canvas.y1);
        }
    }
}

static void checkKnownBase(const Picture& picture, const ShapeSpec& spec, const HudProfile& p, bool f30) {
    assert(spec.exit >= 1 && spec.exit <= 360 && picture.size() >= 4);
    RingLayout l = knownRingLayout(spec, p);
    assert(l.side > 0);
    if (f30) assert(l.cx == 62 && l.side == 66);   // translate the body; never change its diameter with the angle
    double scale = l.side/66.0, outer = 26.296875*scale, inner = 18.046875*scale;
    Shape circle; circle.push_back(picture[0].poly); circle.push_back(picture[1].poly);
    Rect bounds = boundsForTest(circle);
    assert(bounds.x0+bounds.x1 == 2*l.cx && bounds.y0+bounds.y1 == 2*l.cy);
    assert(bounds.x1-bounds.x0 == bounds.y1-bounds.y0);
    if (f30) assert(bounds.x1-bounds.x0 >= 52 && bounds.x1-bounds.x0 <= 53);
    bool hasInner = false, hasOuter = false;
    for (size_t i = 0; i < 2; ++i) {
        assert(picture[i].colour == (spec.exit == 360 ? p.fill : p.road));
        for (size_t j = 0; j < circle[i].pts.size(); ++j) {
            double dx = circle[i].pts[j].x-l.cx, dy = circle[i].pts[j].y-l.cy;
            double radius = sqrt(dx*dx+dy*dy);
            bool ri = fabs(radius-inner) <= 0.8, ro = fabs(radius-outer) <= 0.8;
            assert(ri || ro); hasInner |= ri; hasOuter |= ro;
        }
    }
    assert(hasInner && hasOuter);
    for (size_t i = 2; i < picture.size(); ++i) assert(picture[i].colour == p.fill);
    checkPictureBounds(picture, p);
    const Poly& head = picture.back().poly;
    assert(head.pts.size() == 7);
    if (spec.exit < 360) {
        Shape input; input.push_back(picture[picture.size()-2].poly);
        Rect entry = boundsForTest(input);
        assert(entry.x0+entry.x1 == 2*l.cx);
        assert(entry.x1-entry.x0 == 2*roundForTest(4.125*scale));   // input and exit use the ring's 8.25px stroke
        assert(entry.y0 == l.cy+roundForTest(inner));
        assert(entry.y1 > bounds.y1);   // the incoming bar must remain visible outside the circular body after fitting
        double a = spec.exit*3.141592653589793/180, dx = sin(a)*(spec.clockwise ? -1 : 1), dy = cos(a);
        double nx = -dy, ny = dx, base = (26.296875+8)*scale, tip = (26.296875+21.75)*scale;
        double wingBase = base-(186.0/256)*scale;
        const double points[] = {inner*dx+4.125*scale*nx, inner*dy+4.125*scale*ny,
                                 base*dx+4.125*scale*nx, base*dy+4.125*scale*ny,
                                 wingBase*dx+8.23828125*scale*nx, wingBase*dy+8.23828125*scale*ny,
                                 tip*dx, tip*dy,
                                 wingBase*dx-8.23828125*scale*nx, wingBase*dy-8.23828125*scale*ny,
                                 base*dx-4.125*scale*nx, base*dy-4.125*scale*ny,
                                 inner*dx-4.125*scale*nx, inner*dy-4.125*scale*ny};
        nearPointSet(head, expectedPolygon(points, 7, l));
        size_t farthest = 0;
        long radius2 = -1;
        for (size_t i = 0; i < head.pts.size(); ++i) {
            long x = head.pts[i].x-l.cx, y = head.pts[i].y-l.cy, r = x*x+y*y;
            if (r > radius2) { radius2 = r; farthest = i; }
        }
        double x = head.pts[farthest].x-l.cx, y = head.pts[farthest].y-l.cy;
        assert(fabs(x*dy-y*dx) <= 1.5);   // strict normal axis at the displayed angle, including clamped boundary inputs
        assert(fabs(x*dx+y*dy-tip) <= 1.0 && x*dx+y*dy > outer+10*scale);
        if (f30)
            for (size_t i = 0; i < picture.size(); ++i)
                for (size_t j = 0; j < picture[i].poly.pts.size(); ++j) {
                    Point q = picture[i].poly.pts[j];
                    long u = q.x-l.cx, v = q.y-l.cy;
                    if (q.x != head.pts[farthest].x || q.y != head.pts[farthest].y)
                        assert(u*u+v*v < radius2);   // an entrance footer cannot paint over the downward tip
                }
    } else if (f30) {
        assert(l.cx == 62 && l.cy == 29 && l.side == 66 && picture.size() == 4);
        const int mirror = spec.clockwise ? -1 : 1;
        const double input[] = {2.875*mirror,18,11.125*mirror,18,
                                11.125*mirror,45,2.875*mirror,45};
        const double output[] = {-12.125*mirror,18,-12.125*mirror,33.25,
                                 -16.23828125*mirror,32.5234375,-8.0*mirror,47,0.23828125*mirror,32.5234375,
                                 -3.875*mirror,33.25,-3.875*mirror,18};
        nearPointSet(picture[picture.size()-2].poly, expectedPolygon(input, 4, l), 0);
        nearPointSet(head, expectedPolygon(output, 7, l), 0);   // the sole nonnormal exception: approved C360
    }
    if (spec.exit % 90 == 0) {
        for (int progress = 45; progress < 360; progress += 90) {
            double a = progress*3.141592653589793/180, radius = (inner+outer)/2;
            double x = l.cx+sin(a)*radius*(spec.clockwise ? -1 : 1), y = l.cy+cos(a)*radius;
            assert(pictureColourAt(picture, x, y) == (progress < spec.exit ? p.fill : p.road));
        }
    }
}

static void checkKnownNumber(const Picture& numbered, const Picture& plain, const ShapeSpec& spec,
                             const HudProfile& profile, int number) {
    Shape actual = picturePolygons(numbered), body = picturePolygons(plain);
    samePolygons(actual, 0, body);
    for (size_t i = 0; i < plain.size(); ++i) assert(numbered[i].colour == plain[i].colour);
    RingLayout l = knownRingLayout(spec, profile);
    if (number < 1 || number > 99) { assert(numbered.size() == plain.size()); return; }
    int h = l.side*(number > 9 ? 18 : 24)/66;
    assert(h >= 6);
    Shape digits;
    hudterm::digits::drawNumber(number, l.cx, l.cy-h/2, h, digits);
    assert(numbered.size() == plain.size()+digits.size());
    samePolygons(actual, plain.size(), digits);
    const double hole = 18.046875*l.side/66.0-0.5;
    for (size_t i = plain.size(); i < numbered.size(); ++i) {
        assert(numbered[i].colour == profile.fill);
        for (size_t j = 0; j < numbered[i].poly.pts.size(); ++j) {
            Point q = numbered[i].poly.pts[j];
            double dx = q.x-l.cx, dy = q.y-l.cy;
            assert(dx*dx+dy*dy < hole*hole);
        }
    }
    sameRect(boundsForTest(actual), boundsForTest(body));
    checkPictureBounds(numbered, profile);
}

static void checkKnownBatch(const Picture& picture, const HudProfile& profile) {
    FakePort port; Hud6waDisplay display(port, profile, false);
    Batch batch = display.pictureBatch("known ring", picture, HudTexts(), true);
    assert(batch.calls.size() <= Config().maxBatch && batch.calls.size() <= 64);
    for (size_t i = 0; i < batch.calls.size(); ++i) assert(callOk(batch.calls[i], handlesOf(profile)));
}

static void testKnownRoundabouts() {
    unsetenv("NBT_HUD_GLYPH"); unsetenv("NBT_HUD_CANVAS");
    const int widths[] = {126,176,120,0}, heights[] = {77,126,72,0};
    const int labels[] = {0,1,2,3,4,5,6,7,8,9,12,99,-1,100};
    unsigned angles = 0;
    for (size_t p = 0; p < 4; ++p) {
        HudProfile profile = k6wa; if (widths[p]) fitMeasured(widths[p], heights[p], profile);
        profile.nHide = 8; for (int h = 0; h < 8; ++h) profile.hide[h] = 20+h;
        for (int angle = 1; angle <= 360; ++angle) {
            Picture pictures[2];
            for (int cw = 0; cw <= 1; ++cw) {
                ShapeSpec spec = shapeFor(cw ? 33 : 34, true, angle);
                assert(spec.exit == (angle < 30 ? 30 : angle > 330 && angle < 360 ? 330 : angle));
                assert(roundaboutPicture(spec, profile, pictures[cw]));
                checkKnownBase(pictures[cw], spec, profile, p == 0);
                Picture seven; assert(roundaboutPicture(spec, profile, seven, 7));
                checkKnownNumber(seven, pictures[cw], spec, profile, 7);
                checkKnownBatch(seven, profile);
                if (angle == 1 || angle % 45 == 0 || angle == 359)
                    for (size_t n = 0; n < sizeof(labels)/sizeof(labels[0]); ++n) {
                        Picture numbered;
                        assert(roundaboutPicture(spec, profile, numbered, labels[n]));
                        checkKnownNumber(numbered, pictures[cw], spec, profile, labels[n]);
                        checkKnownBatch(numbered, profile);
                    }
                ++angles;
            }
            RingLayout ccw = knownRingLayout(shapeFor(34, true, angle), profile);
            RingLayout cw = knownRingLayout(shapeFor(33, true, angle), profile);
            assert(ccw.side == cw.side && ccw.cy == cw.cy && pictures[0].size() == pictures[1].size());
            for (size_t i = 0; i < pictures[0].size(); ++i) {
                Poly reflected = pictures[0][i].poly;
                for (size_t j = 0; j < reflected.pts.size(); ++j)
                    reflected.pts[j].x = cw.cx-(reflected.pts[j].x-ccw.cx);
                Shape expected; expected.push_back(reflected);
                // A centred input rectangle is its own mirror even if its winding remains unchanged.
                nearPointSet(pictures[1][i].poly, expected, 0);
            }
        }
    }
    assert(angles == 2880);
}

// An upward exit can translate the circle down past its original footer: a rectangle wholly inside the ring
// is not a visible entrance. Check native output against the actual circle, including valid top-aligned pins.
static void testKnownEntryVisibility() {
    unsetenv("NBT_HUD_CANVAS"); unsetenv("NBT_HUD_GLYPH");
    const int angles[] = {170,179,180,190};
    for (int p = 0; p < 5; ++p) {
        if (p == 2) setenv("NBT_HUD_GLYPH", "20 0 50 30", 1);
        if (p == 3) setenv("NBT_HUD_GLYPH", "20 0 120 60", 1);
        if (p == 4) {
            setenv("NBT_HUD_CANVAS", "10 20 209 149", 1);
            setenv("NBT_HUD_GLYPH", "30 20 60 50", 1);
        }
        HudProfile profile = k6wa; applyHudEnv(profile);
        if (p == 1) fitMeasured(199, 129, profile);
        for (size_t a = 0; a < sizeof(angles)/sizeof(angles[0]); ++a)
            for (int cw = 0; cw <= 1; ++cw) {
                ShapeSpec spec = shapeFor(cw ? 33 : 34, true, angles[a]);
                Picture picture; assert(roundaboutPicture(spec, profile, picture));
                checkKnownBase(picture, spec, profile, false);
                Shape circle, input;
                circle.push_back(picture[0].poly); circle.push_back(picture[1].poly);
                input.push_back(picture[picture.size()-2].poly);
                Rect body = boundsForTest(circle), entrance = boundsForTest(input);
                assert(entrance.y0 <= body.y1 && entrance.y1 >= body.y1+(p < 2 ? 2 : 1));
                assert(entrance.y1 <= profile.canvas.y1);
                Picture numbered; assert(roundaboutPicture(spec, profile, numbered, 7));
                checkKnownNumber(numbered, picture, spec, profile, 7);
                checkKnownBatch(numbered, profile);
            }
    }
    unsetenv("NBT_HUD_CANVAS"); unsetenv("NBT_HUD_GLYPH");
}

// Read the actual serialized picture, so these checks cover arrowPicture and the real terminal batch.
static Picture batchPicture(const Batch& batch) {
    Picture picture;
    uint8_t colour = 0;
    for (size_t i = 0; i < batch.calls.size(); ++i) {
        const Call& call = batch.calls[i];
        if (call.fkt == kSetFillColor) colour = call.payload[2];
        if (call.fkt != kAddPolygon) continue;
        Item item; item.colour = colour; item.poly.type = (PolyType)call.payload[4];
        unsigned n = call.payload[5];
        assert(call.payload.size() == 6 + n * 4);
        for (unsigned j = 0; j < n; ++j)
            item.poly.pts.push_back(shapes::pt((int16_t)rdBe16(&call.payload[6 + j * 4]),
                                              (int16_t)rdBe16(&call.payload[8 + j * 4])));
        picture.push_back(item);
    }
    return picture;
}

static Rect pictureBounds(const Picture& picture) {
    assert(!picture.empty());
    Rect box = { 32767, 32767, -32768, -32768 };
    for (size_t i = 0; i < picture.size(); ++i)
        for (size_t j = 0; j < picture[i].poly.pts.size(); ++j) {
            const Point& pt = picture[i].poly.pts[j];
            box.x0 = std::min(box.x0, pt.x); box.x1 = std::max(box.x1, pt.x);
            box.y0 = std::min(box.y0, pt.y); box.y1 = std::max(box.y1, pt.y);
        }
    return box;
}

static void samePictureItems(const Picture& actual, const Picture& expected, size_t offset = 0);

static void testReadableHudArrows() {
    unsetenv("NBT_HUD_CANVAS"); unsetenv("NBT_HUD_GLYPH");
    HudProfile profile = k6wa; fitMeasured(126,77,profile);
    std::map<int,Picture> baseline;
    for (int mode = 0; mode < 2; ++mode) {
        setenv("NBT_HUD_GLYPHS",mode ? "1" : "0",1);
        FakePort port; Hud6waDisplay display(port,profile,false);
        for (int type = 0; type <= 60; ++type) {
            ShapeSpec spec = shapeFor(type,true,90);
            if (spec.kind == kShapeRoundabout) continue;
            Picture pic = batchPicture(display.showBatch(true,HudTexts(),spec));
            if (!mode) baseline[type] = pic; else samePictureItems(pic,baseline[type]);
            Rect bounds = pictureBounds(pic);
            assert(std::max(bounds.x1-bounds.x0,bounds.y1-bounds.y0) >= 40);
            for (size_t i = 0; i < pic.size(); ++i) {
                assert(area2(pic[i].poly.pts) > 0 && pic[i].poly.pts.size() <= 99);
                for (size_t j = 0; j < pic[i].poly.pts.size(); ++j) {
                    const Point& q = pic[i].poly.pts[j];
                    assert(q.x >= 0 && q.x <= 125 && q.y >= 0 && q.y <= 76);
                }
            }
            if (spec.kind == kShapeFlag) {
                assert(pic.size() == 2 && pic[0].colour == profile.fill && pic[1].colour == profile.edge);
                assert(pic[0].poly.pts.size() == 18 && pic[1].poly.pts.size() == 16);
            }
        }
        const int pairs[][2] = {{5,6},{7,8},{9,10},{11,12},{13,14},{21,22},{25,26},{27,28}};
        for (size_t pair = 0; pair < sizeof(pairs)/sizeof(pairs[0]); ++pair) {
            const Picture& left = baseline[pairs[pair][0]]; const Picture& right = baseline[pairs[pair][1]];
            assert(left.size() == right.size());
            for (size_t i = 0; i < left.size(); ++i) {
                assert(left[i].colour == right[i].colour && left[i].poly.pts.size() == right[i].poly.pts.size());
                for (size_t j = 0; j < left[i].poly.pts.size(); ++j) {
                    const Point& a = left[i].poly.pts[j];
                    const Point& b = right[i].poly.pts[right[i].poly.pts.size()-1-j];
                    assert(a.x+b.x == 124 && a.y == b.y);
                }
            }
        }
    }
    unsetenv("NBT_HUD_GLYPHS");
    printf("  unified F30 arrows: identical modes, native bounds, exact mirrors and 16-facet pin window\n");
}

static void testReadableHudCompatibility() {
    for (int mode = 0; mode < 2; ++mode)
        for (int layout = 0; layout < 6; ++layout) {
            setenv("NBT_HUD_GLYPHS", mode ? "1" : "0", 1);
            if (layout == 3) setenv("NBT_HUD_GLYPH", "4 5 34 35", 1);
            else unsetenv("NBT_HUD_GLYPH");
            if (layout == 4) setenv("NBT_HUD_CANVAS", "0 0 125 76", 1);
            else unsetenv("NBT_HUD_CANVAS");
            HudProfile profile = k6wa; applyHudEnv(profile);
            if (layout == 1) fitMeasured(176, 126, profile);
            if (layout == 2) fitMeasured(120, 72, profile);
            if (layout >= 3) fitMeasured(126, 77, profile);
            if (layout == 5) profile.glyph = Rect{5, 6, 30, 35};
            FakePort port; Hud6waDisplay display(port, profile, false);
            for (int type = 0; type <= 60; ++type) {
                ShapeSpec spec = shapeFor(type, true, 90);
                if (spec.kind == kShapeRoundabout) continue;   // shared ring renderer is tested separately
                Picture pic;
                if (!mbxManeuver(spec, profile, pic)) {
                    Shape source, fitted; drawShape(spec, source); fitShape(source, profile.glyph, fitted);
                    for (size_t i = 0; i < fitted.size(); ++i) {
                        Item item; item.colour = profile.fill; item.poly = fitted[i]; pic.push_back(item);
                    }
                }
                Batch expected = display.pictureBatch("legacy", pic, HudTexts(), false);
                Batch actual = display.showBatch(true, HudTexts(), spec);
                assert(actual.calls.size() == expected.calls.size());
                for (size_t i = 0; i < actual.calls.size(); ++i)
                    assert(actual.calls[i].fkt == expected.calls[i].fkt && actual.calls[i].payload == expected.calls[i].payload);
            }
        }
    unsetenv("NBT_HUD_CANVAS"); unsetenv("NBT_HUD_GLYPH"); unsetenv("NBT_HUD_GLYPHS");
    printf("  F20, cluster, MINI and manually pinned ordinary pictures retain their fitting\n");
}

static void testGlyphBatchBudget() {
    unsigned largest = 0;
    const int widths[] = {0, 126, 176, 120}, heights[] = {0, 77, 126, 72};
    for (int mode = 0; mode < 2; ++mode) {
        setenv("NBT_HUD_GLYPHS", mode ? "1" : "0", 1);
        for (int layout = 0; layout < 4; ++layout) {
            HudProfile profile = k6wa;
            if (layout) fitMeasured(widths[layout], heights[layout], profile);
            profile.nHide = 8;
            for (int i = 0; i < profile.nHide; ++i) profile.hide[i] = 21 + i;
            FakePort port; Hud6waDisplay display(port, profile, false);
            for (int type = 0; type <= 60; ++type)
                for (int angle = 0; angle <= 360; angle += 90) {
                    // showBatch uses the production fitter; add the worst-case previous-route cleanup to that picture.
                    Batch normal = display.showBatch(true, HudTexts(), shapeFor(type, true, angle));
                    Batch complete = display.pictureBatch("budget", batchPicture(normal), HudTexts(), true);
                    Config config; config.handles = handlesOf(profile);
                    hudterm::Core terminal(config);
                    terminal.setLink(true, 0x2077, 0); terminal.setHost(0x0123, 0);
                    assert(terminal.submit(complete, 0) == kQueued);
                    largest = std::max(largest, (unsigned)complete.calls.size());
                }
        }
    }
    unsetenv("NBT_HUD_GLYPHS");
    assert(largest > 32 && largest <= Config().maxBatch);
    printf("  production glyph batches: maximum %u calls with fresh texts, eight hides and old-route cleanup\n", largest);
}

static void testLanes() {
    std::vector<kombinav::AaLane> lanes;
    lanes.push_back(oneLane(1, false));
    lanes.push_back(oneLane(1, false));
    lanes.push_back(oneLane(5, true));
    Picture p;
    assert(drawLanes(lanes, k6wa.lanes, 3, 4, p));
    assert(p.size() == 3 && p[0].colour == 3 && p[1].colour == 3 && p[2].colour == 4);
    checkPicture(p, "lanes");
    std::vector<kombinav::AaLane> none;
    assert(!drawLanes(none, k6wa.lanes, 3, 4, p) && p.size() == 3);
    std::vector<kombinav::AaLane> many(5, kombinav::AaLane(5, kombinav::LaneDirection(1, false)));
    assert(!drawLanes(many, k6wa.lanes, 3, 4, p));               // 25 arrows: no lane picture rather than a mess
    for (int n = 1; n <= 8; ++n)                                 // every shape, up to 8 lanes: on the canvas
        for (int shape = 0; shape <= 9; ++shape) {
            std::vector<kombinav::AaLane> l(n, oneLane(shape, false));
            l[0][0].highlighted = true;
            Picture q;
            assert(drawLanes(l, k6wa.lanes, 3, 4, q) && q.size() == (size_t)n && q[n - 1].colour == 4);
            checkPicture(q, "lanes");
            for (size_t i = 0; i < q.size(); ++i)
                for (size_t k = 0; k < q[i].poly.pts.size(); ++k) assert(onCanvas(q[i].poly.pts[k]));
        }
}

static NavView timed(int type, const char* road, const char* dist, int unit, int64_t m, int64_t s) {
    NavView v = view(type, road, dist, unit, m);
    v.has_step_s = s >= 0;
    v.step_s = s;
    return v;
}

static size_t callsOn(const Batch& b, uint16_t h) {
    size_t n = 0;
    for (size_t i = 0; i < b.calls.size(); ++i)
        if (b.calls[i].handle() == h) ++n;
    return n;
}

static size_t fills(const Batch& b) {
    size_t n = 0;
    for (size_t i = 0; i < b.calls.size(); ++i)
        if (b.calls[i].fkt == kSetFillColor) ++n;
    return n;
}

// Roundabouts use one common static picture far and near, independently of glyph mode or a supplied number.
static void testKnownRoundaboutDriver() {
    HudProfile profile = k6wa; fitMeasured(126, 77, profile);
    const int angles[] = {1,90,225,359,360};
    for (int mb = 0; mb <= 1; ++mb) {
        setenv("NBT_HUD_GLYPHS", mb ? "1" : "0", 1);
        for (int type = 33; type <= 34; ++type)
            for (size_t a = 0; a < sizeof(angles)/sizeof(angles[0]); ++a)
                for (int label = 0; label <= 1; ++label) {
                    FakePort port; Hud6waDisplay display(port, profile, false);
                    NavView v = timed(type, "Ring Road", "800", 1, 800, 60);
                    v.has_exit_angle = true; v.exit_angle = angles[a];
                    v.has_exit_number = label != 0; v.exit_number = 7;
                    assert(display.showManeuver(v, 0, true, true) == ClusterDisplay::kOk);
                    assert(port.batches.size() == 1 && !display.nearShown() && display.routeStage() == 0);
                    Picture expected;
                    assert(ring60::picture(shapeFor(type, true, angles[a]), profile, expected, label ? 7 : 0));
                    Shape expectedShape = picturePolygons(expected);
                    Shape far = packetPolygons(port.batches[0]);
                    assert(far.size() == expectedShape.size()); samePolygons(far, 0, expectedShape);
                    v.step_m = 280; v.step_s = 15;
                    assert(!display.sameManeuver(v, 1));
                    assert(display.keepUp(v, 1) == ClusterDisplay::kOk && port.batches.size() == 1);
                    v.step_m = 100; v.step_s = 5;
                    assert(!display.sameManeuver(v, 2));
                    assert(display.showManeuver(v, 2, false, false) == ClusterDisplay::kOk);
                    assert(port.batches.size() == 2 && !display.nearShown() && display.routeStage() == 0);
                    Shape near = packetPolygons(port.batches[1]);
                    assert(near.size() == far.size()); samePolygons(near, 0, far);
                    assert(callsOn(port.batches[0], 7) == 0 && callsOn(port.batches[0], 8) == 0);
                    assert(callsOn(port.batches[1], 7) == 0 && callsOn(port.batches[1], 8) == 0);
                    v.step_m = 0; v.step_s = 0;
                    assert(!display.sameManeuver(v, 3));
                    assert(display.keepUp(v, 3) == ClusterDisplay::kOk && port.batches.size() == 2);
                }
    }
    unsetenv("NBT_HUD_GLYPHS");

    // Switching from an ordinary junction clears its dirty overlay in the first roundabout batch, exactly once.
    FakePort port; Hud6waDisplay display(port, k6wa, false);
    NavView turn = timed(8, "Testroad", "200", 1, 280, 15);
    assert(display.showManeuver(turn, 0, true, true) == ClusterDisplay::kOk);
    assert(port.batches.size() == 2 && display.nearShown() && display.routeStage() == 4);
    NavView ring = timed(34, "Ring Road", "100", 1, 100, 8);
    ring.has_exit_angle = true; ring.exit_angle = 225;
    ring.has_exit_number = true; ring.exit_number = 7;
    assert(display.showManeuver(ring, 1, true, false) == ClusterDisplay::kOk);
    assert(port.batches.size() == 3 && !display.nearShown() && display.routeStage() == 0);
    const Batch& first = port.batches[2];
    assert(callsOn(first, 7) == 1 && callsOn(first, 8) == 1);
    for (size_t i = 0; i < first.calls.size(); ++i)
        if (first.calls[i].handle() == 7 || first.calls[i].handle() == 8)
            assert(first.calls[i].fkt == kRemoveAllItems);
    ring.exit_angle = 90;
    assert(display.showManeuver(ring, 2, true, false) == ClusterDisplay::kOk);
    assert(port.batches.size() == 4 && callsOn(port.batches[3], 7) == 0 && callsOn(port.batches[3], 8) == 0);
    assert(display.showManeuver(turn, 3, true, false) == ClusterDisplay::kOk);
    assert(port.batches.size() == 6 && display.nearShown() && port.batches.back().label == "route");
}

// Rendering clamps near-parallel exits while preserving the phone's original angle and the distinct full circuit.
static void testRoundaboutAngleLimits() {
    struct Case { bool has; int raw, shown; };
    const Case cases[] = {{true,1,30},{true,29,30},{true,30,30},{true,31,31},
                          {true,329,329},{true,330,330},{true,331,330},{true,359,330},{true,360,360},
                          {true,0,0},{true,-1,0},{true,361,0},{false,1,0},{false,359,0}};
    HudProfile profile = k6wa; fitMeasured(126,77,profile);
    for (int mode = 0; mode < 2; ++mode) {
        setenv("NBT_HUD_GLYPHS", mode ? "1" : "0", 1);
        for (int type = 33; type <= 34; ++type)
            for (size_t i = 0; i < sizeof(cases)/sizeof(cases[0]); ++i)
                for (int numbered = 0; numbered < 2; ++numbered) {
                    const Case& c = cases[i];
                    assert(shapeFor(type,c.has,c.raw).exit == c.shown);
                    NavView input = timed(type,"Ring Road","100",1,100,8);
                    input.has_exit_angle = c.has; input.exit_angle = c.raw;
                    input.has_exit_number = numbered != 0; input.exit_number = 1;
                    FakePort rawPort, shownPort;
                    Hud6waDisplay rawDisplay(rawPort,profile,false), shownDisplay(shownPort,profile,false);
                    assert(rawDisplay.showManeuver(input,0,true,true) == ClusterDisplay::kOk);
                    assert(input.has_exit_angle == c.has && input.exit_angle == c.raw);
                    NavView shown = input; shown.has_exit_angle = c.shown != 0; shown.exit_angle = c.shown;
                    assert(shownDisplay.showManeuver(shown,0,true,true) == ClusterDisplay::kOk);
                    assert(rawPort.batches.size() == 1 && shownPort.batches.size() == 1);
                    const Batch& a = rawPort.batches[0]; const Batch& b = shownPort.batches[0];
                    assert(a.calls.size() == b.calls.size());
                    for (size_t k = 0; k < a.calls.size(); ++k)
                        assert(a.calls[k].fkt == b.calls[k].fkt && a.calls[k].payload == b.calls[k].payload);
                }
    }
    unsetenv("NBT_HUD_GLYPHS");
}

// The near stage through the driver: the arrow first, the junction view (roads in 5/6, route in 7/8) at the main
// prompt, the route's stages on the way in, held while a route waits, all drawn again after lost work, the route
// emptied for the next maneuver's arrow, and the clear putting 7/8 back.
// The display's guard (cluster/hudterm_junction.hpp junctionFits) checks what is drawn: the junction laid out on the
// F20 and scaled onto the picture area (fitPicture). Counted below on the F30's 126x77 HUD. With the glyph box against
// the canvas's edge some arrowheads leave it, and near those the display keeps the plain arrow (and says so) instead of
// a junction view with its way cut off. A buffer measured while a view is up drops that view, laid out for the old one.
static void testJunctionFits() {
    const int st = Hud6waDisplay::kStages;
    HudProfile hud = k6wa;
    fitMeasured(126, 77, hud);
    HudProfile edge = hud;
    const Rect corner = { 95, 46, 125, 76 };                     // the glyph box in the canvas's bottom-right corner
    edge.glyph = corner;
    int fits = 0, cut = 0, edgeCut = 0, cutType = -1, cutAngle = 0;
    for (int t = 0; t <= 60; ++t)
        for (int a = 0; a <= 360; a += 45) {
            Layout L;
            if (!junctionLayout(t, true, a, false, 0, k6wa.glyph, k6wa.canvas.y1, L)) continue;   // as the display does
            if (junctionFits(L, st, hud.canvas, k6wa.glyph, k6wa.canvas.y1 + 1, hud.glyph, hud.canvas.y1 + 1))
                ++fits;
            else
                ++cut;
            if (!junctionFits(L, st, edge.canvas, k6wa.glyph, k6wa.canvas.y1 + 1, edge.glyph, edge.canvas.y1 + 1)) {
                if (cutType < 0) { cutType = t; cutAngle = a; }
                ++edgeCut;
            }
        }
    assert(fits > 0 && edgeCut > 0);

    g_log.clear();
    FakePort port;
    Hud6waDisplay d(port, edge, false);
    d.setLog(&logFn, NULL);
    NavView v = timed(cutType, "Testroad", "300", 7, 280, 19);    // near: the junction view would be due
    v.has_exit_angle = true;
    v.exit_angle = cutAngle;
    assert(d.showManeuver(v, 0, true, true) == ClusterDisplay::kOk);
    assert(!d.nearShown() && logged("junction view would leave the 126x77 picture buffer"));

    g_log.clear();
    FakePort port2;
    Hud6waDisplay d2(port2, k6wa, false);
    d2.setLog(&logFn, NULL);
    NavView w = timed(8, "Testroad", "300", 7, 280, 19);
    assert(d2.showManeuver(w, 0, true, true) == ClusterDisplay::kOk && d2.nearShown());   // the F20 field: it fits
    port2.mw = 126;                                                         // the terminal answers ImageGetSize
    port2.mh = 77;
    assert(d2.showManeuver(w, 1, false, false) == ClusterDisplay::kOk);   // whose health check takes the measure
    assert(logged("HUD picture buffer measured 126x77") && !d2.nearShown());
    printf("  junctionFits scaled onto 126x77: %d fit, %d plain arrow; box in the corner: %d plain arrow (first type %d"
           " angle %d); a re-measure drops the view\n", fits, cut, edgeCut, cutType, cutAngle);
}

// The guidance moves between the HUD (126 x 77) and the cluster (176 x 126) with the iDrive setting, and each
// measurement is fitted from the profile as given: the factory box scales with either measured field,
// rather than keeping the HUD's small box or the F20's right-shifted cluster box.
static void testRefitFromProfile() {
    g_log.clear();
    FakePort port;
    Hud6waDisplay d(port, k6wa, false);
    d.setLog(&logFn, NULL);
    NavView v = view(8, "Testroad", "300", 7, 300);
    port.mw = 126;
    port.mh = 77;
    assert(d.showManeuver(v, 0, true, true) == ClusterDisplay::kOk);
    assert(logged("HUD picture buffer measured 126x77: canvas 0,0-125,76 glyph 37,23-82,68"));
    port.mw = 176;
    port.mh = 126;
    assert(d.showManeuver(v, 1, false, false) == ClusterDisplay::kOk);
    assert(logged("HUD picture buffer measured 176x126: canvas 0,0-175,125 glyph 52,38-115,111"));
    g_log.clear();
    port.mw = 126;
    port.mh = 77;
    assert(d.showManeuver(v, 2, false, false) == ClusterDisplay::kOk);
    assert(logged("HUD picture buffer measured 126x77: canvas 0,0-125,76 glyph 37,23-82,68"));
    printf("  refit from the profile: HUD 126x77 (factory box), cluster 176x126 (scaled factory box), HUD again\n");
}

static void testJunctionDriver() {
    g_log.clear();
    FakePort port;
    Hud6waDisplay d(port, k6wa, false);
    d.setLog(&logFn, NULL);
    NavView v = timed(8, "Testroad", "0.5", 5, 800, 60);
    assert(d.showManeuver(v, 0, true, true) == ClusterDisplay::kOk);
    assert(port.batches.size() == 1 && !d.nearShown() && d.routeStage() == 0);
    assert(callsOn(port.batches[0], 7) == 0 && callsOn(port.batches[0], 8) == 0);
    v = timed(8, "Testroad", "0.3", 5, 450, 30);
    assert(!d.sameManeuver(v, 1));
    v = timed(8, "Testroad", "300", 7, 280, 19);                  // the main prompt
    assert(d.sameManeuver(v, 2) && logged("NEAR type 8: 19 s, 280 m: junction view"));
    assert(d.showManeuver(v, 2, false, false) == ClusterDisplay::kOk);
    assert(port.batches.size() == 3 && d.nearShown() && d.routeStage() == 4 && d.visibleRoute() == 7);
    const Batch& j = port.batches[1];
    assert(j.label == "redraw" && j.slot == kSlotMain && j.calls.size() == 11);
    expectCall(j.calls[0], kRemoveAllItems, 6);
    expectCall(j.calls[1], kSetFillColor, 6);
    assert(j.calls[1].payload[2] == 7);
    expectCall(j.calls[2], kAddPolygon, 6);
    expectCall(j.calls[3], kSetFillColor, 6);
    assert(j.calls[3].payload[2] == 3);
    expectCall(j.calls[4], kAddPolygon, 6);
    expectCall(j.calls[5], kSwitchVisibility, 5);
    assert(rdBe16(&j.calls[5].payload[2]) == 6);
    assert(logged("REDRAW type 8 -> junction view, arrow +90"));
    const Batch& r = port.batches[2];
    assert(r.label == "route" && r.slot == kSlotRoute && r.full && r.calls.size() == 7);
    expectCall(r.calls[0], kRemoveAllItems, 7);
    expectCall(r.calls[1], kSetFillColor, 7);
    assert(r.calls[1].payload[2] == 7);
    expectCall(r.calls[2], kAddPolygon, 7);
    expectCall(r.calls[3], kSetFillColor, 7);
    assert(r.calls[3].payload[2] == 4);
    expectCall(r.calls[4], kAddPolygon, 7);
    expectCall(r.calls[5], kSwitchVisibility, 8);                 // 8 -> 7, as the factory navigation swaps it
    assert(rdBe16(&r.calls[5].payload[2]) == 7);
    expectCall(r.calls[6], kRemoveAllItems, 8);
    assert(logged("ROUTE stage 4/4"));

    // The stages: down only, held while a route waits.
    v.step_m = 200;                                               // 4 * 200 / 280 = 2.9: stage 3
    assert(!d.sameManeuver(v, 3) && d.keepUp(v, 3) == ClusterDisplay::kOk);
    assert(port.batches.size() == 4 && d.routeStage() == 3 && d.visibleRoute() == 8);
    expectCall(port.batches[3].calls[0], kRemoveAllItems, 8);
    v.step_m = 250;                                               // back up a little: stays at 3
    assert(d.keepUp(v, 4) == ClusterDisplay::kOk && port.batches.size() == 4);
    v.step_m = 100;
    port.wait = true;
    assert(d.keepUp(v, 5) == ClusterDisplay::kOk && port.batches.size() == 4 && d.routeStage() == 3);
    port.wait = false;
    assert(d.keepUp(v, 6) == ClusterDisplay::kOk && port.batches.size() == 5 && d.routeStage() == 2);

    // Lost work: the junction view and its route again.
    port.ep = 1;
    assert(d.sameManeuver(v, 7));
    assert(d.showManeuver(v, 7, false, false) == ClusterDisplay::kOk);
    assert(port.batches.size() == 7 && port.batches[5].label == "redraw" && port.batches[6].label == "route");
    assert(d.routeStage() == 2);

    // The next maneuver, far off: its arrow, and both route buffers emptied in the same batch.
    NavView v2 = timed(7, "Sample Way", "0.6", 5, 1000, 70);
    assert(d.showManeuver(v2, 8, true, false) == ClusterDisplay::kOk);
    assert(port.batches.size() == 8 && !d.nearShown() && d.routeStage() == 0);
    const Batch& a = port.batches[7];
    Shape s;
    drawShape(shapeFor(7, false, 0), s);
    assert(a.label == "maneuver" && a.calls.size() == 2 + s.size() + 2 + 2 + 3 + 1);
    expectCall(a.calls[4 + s.size()], kRemoveAllItems, 7);
    expectCall(a.calls[5 + s.size()], kRemoveAllItems, 8);
    assert(d.keepUp(v2, 9) == ClusterDisplay::kOk && port.batches.size() == 8);
    NavView v3 = timed(7, "Other Way", "0.5", 5, 900, 60);        // another arrow: the route is already empty
    assert(d.showManeuver(v3, 10, true, false) == ClusterDisplay::kOk);
    assert(callsOn(port.batches[8], 7) == 0 && callsOn(port.batches[8], 8) == 0);

    // A known roundabout remains the same standalone picture even when already near; it owns no route overlay.
    NavView ra = timed(32, "Ring Road", "150", 1, 150, 12);
    ra.has_exit_angle = true; ra.exit_angle = 270;
    ra.has_exit_number = true; ra.exit_number = 3;
    assert(d.showManeuver(ra, 11, true, false) == ClusterDisplay::kOk);
    assert(port.batches.size() == 10 && !d.nearShown() && d.routeStage() == 0);
    const Batch& rb = port.batches[9];
    assert(rb.label == "maneuver" && callsOn(rb, 7) == 0 && callsOn(rb, 8) == 0);

    // The destination keeps its flag, near or not; the roundabout left no overlay to empty.
    NavView dest = timed(39, "Example Close", "50", 1, 50, 5);
    assert(d.showManeuver(dest, 12, true, false) == ClusterDisplay::kOk);
    assert(port.batches.size() == 11 && !d.nearShown() && !d.sameManeuver(dest, 13));
    assert(callsOn(port.batches[10], 7) == 0 && callsOn(port.batches[10], 8) == 0);

    // The clear puts the route overlay back as the factory navigation leaves it: both empty, 8 on show.
    std::string detail;
    assert(d.clear(&detail));
    const Batch c = port.batches.back();
    assert(c.slot == kSlotAll && c.calls.size() == 11 && d.visibleRoute() == 8);
    expectCall(c.calls[4], kRemoveAllItems, 7);
    expectCall(c.calls[5], kRemoveAllItems, 8);
    expectCall(c.calls[6], kSwitchVisibility, 7);
    assert(rdBe16(&c.calls[6].payload[2]) == 8);

    // Off: never a junction view, and the clear leaves 7/8 alone.
    FakePort po;
    Hud6waDisplay off(po, k6wa, false);
    off.setSettings(arrows());
    NavView close = timed(8, "Testroad", "50", 1, 50, 5);
    assert(off.showManeuver(close, 0, true, true) == ClusterDisplay::kOk && po.batches.size() == 1);
    assert(!off.nearShown() && !off.sameManeuver(close, 1) && off.clearBatch().calls.size() == 8);

    // Shadow mode: worked out and logged, nothing sent.
    g_log.clear();
    FakePort ps;
    Hud6waDisplay sh(ps, k6wa, true);
    sh.setLog(&logFn, NULL);
    assert(sh.showManeuver(close, 0, true, true) == ClusterDisplay::kOk && ps.batches.empty());
    assert(logged("ROUTE stage 4/4") && logged("[shadow] 21D ImageAddPolygon h7"));
}

static void samePictureItems(const Picture& actual, const Picture& expected, size_t offset) {
    assert(actual.size() >= offset + expected.size());
    for (size_t i = 0; i < expected.size(); ++i) {
        assert(actual[offset + i].colour == expected[i].colour);
        assert(addPolygon(5, (uint16_t)i, actual[offset + i].poly.type, actual[offset + i].poly.pts)
            == addPolygon(5, (uint16_t)i, expected[i].poly.type, expected[i].poly.pts));
    }
}

static long crossForTest(const Point& a, const Point& b, const Point& c) {
    return (long)(b.x - a.x) * (c.y - a.y) - (long)(b.y - a.y) * (c.x - a.x);
}

static bool simplePolygon(const Poly& polygon) {
    size_t n = polygon.pts.size();
    for (size_t i = 0; i < n; ++i)
        for (size_t j = i + 2; j < n; ++j) {
            if (i == 0 && j + 1 == n) continue;
            const Point& a = polygon.pts[i]; const Point& b = polygon.pts[i + 1 < n ? i + 1 : 0];
            const Point& c = polygon.pts[j]; const Point& d = polygon.pts[j + 1 < n ? j + 1 : 0];
            long x = crossForTest(a, b, c), y = crossForTest(a, b, d);
            long u = crossForTest(c, d, a), v = crossForTest(c, d, b);
            if (((x < 0 && y > 0) || (x > 0 && y < 0)) && ((u < 0 && v > 0) || (u > 0 && v < 0))) return false;
        }
    return true;
}

// At a large pinned scale no samples collapse: outer points stay at 7.5 degrees, inner points at 2.5.
static void testKnownInnerCircleSampling() {
    setenv("NBT_HUD_GLYPH","pinned",1);
    HudProfile p = k6wa; p.canvas = Rect{0,0,2000,2000}; p.glyph = Rect{600,600,1260,1260};
    ShapeSpec spec = shapeFor(34,true,90);
    RingLayout l = knownRingLayout(spec,p);
    assert(l.side == 660);
    Picture pic; assert(roundaboutPicture(spec,p,pic));
    for (int half = 0; half < 2; ++half) {
        const Poly& poly = pic[half].poly;
        assert(poly.pts.size() == 98); // 25 outer + 73 inner; no extra draw command or protocol overflow
        for (int j = 0; j < 98; ++j) {
            double degrees = half*180 + (j < 25 ? j*7.5 : 180-(j-25)*2.5);
            double a = degrees*3.141592653589793/180, radius = (j < 25 ? 26.296875 : 18.046875)*10;
            assert(fabs(poly.pts[j].x-(l.cx+radius*sin(a))) <= 0.8);
            assert(fabs(poly.pts[j].y-(l.cy-radius*cos(a))) <= 0.8);
        }
    }
    unsetenv("NBT_HUD_GLYPH");
}

// Freeze known layouts, entry/exit contours and captions from 9a8570b; circle sampling may become denser.
static void testKnownRoundaboutAnchorsUnchanged() {
    uint64_t hash = 14695981039346656037ULL;
    const int widths[] = {0,126,176,120}, heights[] = {0,77,126,72};
    for (int p = 0; p < 4; ++p) {
        HudProfile profile = k6wa; if (p) fitMeasured(widths[p],heights[p],profile);
        for (int type = 33; type <= 34; ++type)
            for (int angle = 1; angle <= 360; ++angle)
                for (int label = 0; label < 2; ++label) {
                    ShapeSpec spec = shapeFor(type,true,angle);
                    Picture body, numbered;
                    assert(roundaboutPicture(spec,profile,body));
                    assert(roundaboutPicture(spec,profile,numbered,label ? 99 : 0));
                    RingLayout l = knownRingLayout(spec,profile);
                    hash = (hash ^ (uint16_t)l.cx) * 1099511628211ULL;
                    hash = (hash ^ (uint16_t)l.cy) * 1099511628211ULL;
                    hash = (hash ^ (uint16_t)l.side) * 1099511628211ULL;
                    assert(body.size() >= 2 && body[body.size()-2].poly.pts.size() == 4);
                    Picture pic(body.end()-2,body.end()); // entry and exit precede the caption
                    pic.insert(pic.end(),numbered.begin()+body.size(),numbered.end());
                    hash = (hash ^ (unsigned)pic.size()) * 1099511628211ULL;
                    for (size_t i = 0; i < pic.size(); ++i) {
                        hash = (hash ^ pic[i].colour) * 1099511628211ULL;
                        hash = (hash ^ (unsigned)pic[i].poly.type) * 1099511628211ULL;
                        hash = (hash ^ (unsigned)pic[i].poly.pts.size()) * 1099511628211ULL;
                        for (size_t j = 0; j < pic[i].poly.pts.size(); ++j) {
                            hash = (hash ^ (uint16_t)pic[i].poly.pts[j].x) * 1099511628211ULL;
                            hash = (hash ^ (uint16_t)pic[i].poly.pts[j].y) * 1099511628211ULL;
                        }
                    }
                }
    }
    assert(hash == 0x62b420aebd59d0a9ULL);
}

static bool onSegmentForTest(const Point& a, const Point& b, const Point& q) {
    return !crossForTest(a,b,q) && q.x >= std::min(a.x,b.x) && q.x <= std::max(a.x,b.x) &&
        q.y >= std::min(a.y,b.y) && q.y <= std::max(a.y,b.y);
}

// Reject boundary contact as well as filled overlap: a touching number visually merges into an arrow.
static bool polygonsTouch(const Poly& a, const Poly& b) {
    for (size_t i = 0; i < a.pts.size(); ++i)
        for (size_t j = 0; j < b.pts.size(); ++j) {
            Point p = a.pts[i], q = a.pts[i+1 < a.pts.size() ? i+1 : 0];
            Point r = b.pts[j], s = b.pts[j+1 < b.pts.size() ? j+1 : 0];
            long x = crossForTest(p,q,r), y = crossForTest(p,q,s);
            long u = crossForTest(r,s,p), v = crossForTest(r,s,q);
            if ((((x < 0 && y > 0) || (x > 0 && y < 0)) &&
                 ((u < 0 && v > 0) || (u > 0 && v < 0))) ||
                onSegmentForTest(p,q,r) || onSegmentForTest(p,q,s) ||
                onSegmentForTest(r,s,p) || onSegmentForTest(r,s,q)) return true;
        }
    return containsPoint(a,b.pts[0].x,b.pts[0].y) || containsPoint(b,a.pts[0].x,a.pts[0].y);
}

static void testCirculationNativeGrid() {
    setenv("NBT_HUD_GLYPH","pinned",1);
    for (int side = 1; side <= 200; ++side)
        for (int type = 33; type <= 34; ++type) {
            HudProfile p = k6wa; p.canvas = Rect{0,0,1000,1000};
            p.glyph = Rect{400,400,(int16_t)(400+side),(int16_t)(400+side)};
            Picture body;
            assert(roundaboutPicture(shapeFor(type,false,0),p,body));
            checkPictureBounds(body,p);
            for (size_t i = 0; i < body.size(); ++i) {
                const Poly& poly = body[i].poly;
                assert(simplePolygon(poly));
                for (size_t j = 0; j < poly.pts.size(); ++j) {
                    for (size_t k = j + 1; k < poly.pts.size(); ++k)
                        assert(poly.pts[j].x != poly.pts[k].x || poly.pts[j].y != poly.pts[k].y);
                    Point a = poly.pts[j ? j-1 : poly.pts.size()-1], b = poly.pts[j];
                    Point c = poly.pts[j+1 < poly.pts.size() ? j+1 : 0];
                    assert(crossForTest(a,b,c) ||
                        (long)(b.x-a.x)*(c.x-b.x)+(long)(b.y-a.y)*(c.y-b.y) > 0);
                }
            }
            for (int number = 1; number <= 99; ++number) {
                Picture pic;
                bool drawn = roundaboutPicture(shapeFor(type,false,0),p,pic,number);
                int captionHeight = circulationLayout(p).side*(number > 9 ? 15 : 24)/66;
                if (circulationLayout(p).side < 66) --captionHeight;
                assert(drawn == (captionHeight >= 6));
                samePictureItems(pic,body);
                if (!drawn) { assert(pic.size() == body.size()); continue; }
                assert(pic.size() > body.size());
                checkPictureBounds(pic,p);
                for (size_t i = body.size(); i < pic.size(); ++i)
                    for (size_t j = 0; j < body.size(); ++j)
                        if (polygonsTouch(pic[i].poly,body[j].poly)) {
                            fprintf(stderr,"circulation contact: side%d type%d number%d caption%zu body%zu\n",
                                    side,type,number,i-body.size(),j);
                            assert(false);
                        }
            }
        }
    unsetenv("NBT_HUD_GLYPH");
}

// Pixel rounding must not fold an exit neck across its own head on a constrained manual canvas.
static void testSmallRoundaboutExits() {
    HudProfile p = k6wa; p.canvas = p.glyph = Rect{0,0,17,17};
    for (int type = 33; type <= 34; ++type)
        for (int angle = 1; angle <= 360; ++angle) {
            Picture picture;
            assert(roundaboutPicture(shapeFor(type,true,angle),p,picture));
            for (size_t i = 0; i < picture.size(); ++i) assert(simplePolygon(picture[i].poly));
        }
}

static void testNearPolygonsSimple() {
    unsetenv("NBT_HUD_CANVAS"); unsetenv("NBT_HUD_GLYPH");
    for (int mode = 0; mode < 2; ++mode)
        for (int overlay = 0; overlay < 2; ++overlay)
            for (int type = 9; type <= 12; ++type) {
                setenv("NBT_HUD_GLYPHS", mode ? "1" : "0", 1);
                HudProfile profile = k6wa; fitMeasured(126, 77, profile);
                if (!overlay) profile.routeA = profile.routeB = 0;
                FakePort port; Hud6waDisplay display(port, profile, false);
                NavView maneuver = timed(type, "Testroad", "100", 1, 100, 5);
                assert(display.showManeuver(maneuver, 0, true, true) == ClusterDisplay::kOk && display.nearShown());
                Layout layout; assert(junctionLayout(type, false, 0, false, 0, k6wa.glyph, k6wa.canvas.y1, layout));
                for (int stage = 4; stage >= 1; --stage) {
                    if (stage != 4) {
                        maneuver.step_m = stage * 25;
                        if (overlay) assert(display.keepUp(maneuver, 5 - stage) == ClusterDisplay::kOk);
                        else {
                            assert(display.sameManeuver(maneuver, 5 - stage));
                            assert(display.showManeuver(maneuver, 5 - stage, false, false) == ClusterDisplay::kOk);
                        }
                    }
                    Picture route = batchPicture(port.batches.back());
                    size_t start = overlay ? 0 : layout.roads.size() * 2;
                    for (size_t i = start; i < route.size(); ++i) {
                        if (!simplePolygon(route[i].poly))
                            fprintf(stderr, "self-intersecting near polygon: mode%d overlay%d type%d stage%d item%u\n",
                                    mode, overlay, type, stage, (unsigned)i);
                        assert(simplePolygon(route[i].poly));
                    }
                }
            }
    unsetenv("NBT_HUD_GLYPHS");
    printf("  sharp turns and U-turns: serialized near polygons have no strict self-intersections\n");
}

static void testReadableNearArrows() {
    unsetenv("NBT_HUD_CANVAS"); unsetenv("NBT_HUD_GLYPH");
    std::map<int,Picture> scenes;
    unsigned checked = 0;
    for (int mode = 0; mode < 2; ++mode) for (int overlay = 0; overlay < 2; ++overlay)
        for (int type = 0; type <= 60; ++type) {
            ShapeSpec spec = shapeFor(type,false,0);
            if (spec.kind == kShapeRoundabout || spec.kind == kShapeFlag) continue;
            setenv("NBT_HUD_GLYPHS",mode ? "1" : "0",1);
            HudProfile profile = k6wa; fitMeasured(126,77,profile);
            if (!overlay) profile.routeA = profile.routeB = 0;
            FakePort port; Hud6waDisplay display(port,profile,false);
            NavView v = timed(type,"Testroad","4000",1,4000,500);
            assert(display.showManeuver(v,0,true,true) == ClusterDisplay::kOk && !display.nearShown());
            Picture far = batchPicture(port.batches.back());
            assert(far.size() >= 4);
            const Poly& farBody = far[far.size()-2].poly;
            const Poly& farEdge = far[far.size()-4].poly;
            int farCap = -32768;
            for (size_t i = 0; i < farBody.pts.size(); ++i) farCap = std::max(farCap,(int)farBody.pts[i].y);
            v.step_m = 100; v.step_s = 5;
            assert(display.sameManeuver(v,1));
            assert(display.showManeuver(v,1,false,false) == ClusterDisplay::kOk && display.nearShown());
            int previousCap = 32767;
            for (int stage = 4; stage >= 1; --stage) {
                if (stage != 4) {
                    v.step_m = stage*25;
                    if (overlay) assert(display.keepUp(v,6-stage) == ClusterDisplay::kOk);
                    else {
                        assert(display.sameManeuver(v,6-stage));
                        assert(display.showManeuver(v,6-stage,false,false) == ClusterDisplay::kOk);
                    }
                }
                assert(display.routeStage() == stage);
                Picture route,roads;
                if (overlay) { roads = batchPicture(port.batches[1]); route = batchPicture(port.batches.back()); }
                else {
                    Picture full = batchPicture(port.batches.back());
                    assert(full.size() >= 4);
                    roads.assign(full.begin(),full.end()-4); route.assign(full.end()-4,full.end());
                }
                assert(route.size() == (overlay ? 2u : 4u));
                if (overlay) assert(route[0].colour == 7 && route[1].colour == 7);
                else { assert(route[0].colour == profile.edge && route[1].colour == profile.edge);
                       assert(route[2].colour == profile.route && route[3].colour == profile.route); }
                samePolygons(picturePolygons(Picture(1,route.back())),0,picturePolygons(Picture(1,far.back())));
                const Poly& body = route[overlay ? 0 : 2].poly; int cap = -32768;
                assert(body.pts.size() == farBody.pts.size());
                for (size_t i = 0; i < body.pts.size(); ++i) if (farBody.pts[i].y == farCap) cap = body.pts[i].y;
                assert(cap <= previousCap); previousCap = cap;
                if (stage == 4) assert(cap-farCap == 16);
                int delta = cap-farCap, moved = 0;
                for (size_t i = 0; i < body.pts.size(); ++i) {
                    assert(body.pts[i].x == farBody.pts[i].x);
                    int d = body.pts[i].y-farBody.pts[i].y;
                    assert(d == 0 || d == delta);
                    if (d) ++moved;
                }
                assert(!delta || moved == 2);
                if (!overlay) {
                assert(route[0].poly.pts.size() == farEdge.pts.size());
                for (size_t i = 0; i < farEdge.pts.size(); ++i) {
                    assert(route[0].poly.pts[i].x == farEdge.pts[i].x);
                    int d = route[0].poly.pts[i].y-farEdge.pts[i].y;
                    assert(d == 0 || d == delta);
                }
                }
                for (size_t i = 0; i < route.size(); ++i) {
                    assert(simplePolygon(route[i].poly) && route[i].poly.pts.size() <= 99);
                    if (i%2) for (size_t j = 0; j < route[i].poly.pts.size(); ++j) {
                        const Point& p = route[i].poly.pts[j];
                        assert(p.x >= 0 && p.x <= 125 && p.y >= 0 && p.y <= 76);
                    }
                }
                Picture full;
                for(size_t i=0;i<roads.size();++i)if(overlay || roads[i].colour==3)full.push_back(roads[i]);
                if(overlay) { Picture ink=route; for(size_t i=0;i<ink.size();++i)ink[i].colour=4; full.insert(full.end(),ink.begin(),ink.end()); }
                else full.insert(full.end(),route.end()-2,route.end());
                int key = type*5+stage;
                if (!mode && !overlay) scenes[key] = full; else samePictureItems(full,scenes[key]);
                ++checked;
            }
        }
    assert(checked == 752);
    unsetenv("NBT_HUD_GLYPHS");
    printf("  unified F30 near: %u stages, equal modes/overlay/fused, fixed heads/curves, all tail-border vertices move\n",checked);
}

static void testNearArrowCompatibility() {
    const int widths[] = {0, 176, 120, 126, 126}, heights[] = {0, 126, 72, 77, 77};
    for (int mode = 0; mode < 2; ++mode)
        for (int choice = 0; choice < 5; ++choice) {
            setenv("NBT_HUD_GLYPHS", mode ? "1" : "0", 1);
            if (choice == 3) setenv("NBT_HUD_GLYPH", "37 23 82 68", 1); else unsetenv("NBT_HUD_GLYPH");
            if (choice == 4) setenv("NBT_HUD_CANVAS", "0 0 125 76", 1); else unsetenv("NBT_HUD_CANVAS");
            HudProfile profile = k6wa; applyHudEnv(profile);
            if (choice) fitMeasured(widths[choice], heights[choice], profile);
            for (int type = 0; type <= 60; ++type) {
                Layout layout;
                if (!junctionLayout(type, true, 90, false, 0, k6wa.glyph, k6wa.canvas.y1, layout) || layout.ring) continue;
                FakePort port; Hud6waDisplay display(port, profile, false);
                NavView v = timed(type, "Testroad", "100", 1, 100, 5);
                bool fits = junctionFits(layout, 4, profile.canvas, k6wa.glyph, k6wa.canvas.y1 + 1,
                                         profile.glyph, profile.canvas.y1 + 1);
                assert(display.showManeuver(v, 0, true, true) == ClusterDisplay::kOk);
                assert(display.nearShown() == fits);
                if (!fits) continue;
                JunctionColours colours = { profile.edge, profile.road, profile.route };
                Picture roads; drawRoads(layout, colours, kJunctionStyle, roads);
                fitPicture(roads, k6wa.glyph, k6wa.canvas.y1 + 1, profile.glyph, profile.canvas.y1 + 1);
                Picture actualRoads = batchPicture(port.batches[0]);
                assert(actualRoads.size() == roads.size()); samePictureItems(actualRoads, roads);
                for (int stage = 4; stage >= 1; --stage) {
                    if (stage != 4) {
                        v.step_m = stage * 25;
                        assert(display.keepUp(v, 5 - stage) == ClusterDisplay::kOk);
                    }
                    Picture expected;
                    drawRoute(layout, tailFor(layout, stage, 4), colours, kJunctionStyle, expected);
                    fitPicture(expected, k6wa.glyph, k6wa.canvas.y1 + 1, profile.glyph, profile.canvas.y1 + 1);
                    Picture actual = batchPicture(port.batches.back());
                    if (!mode) {
                        assert(actual.size() == expected.size()); samePictureItems(actual, expected);
                    } else {
                        assert(actual.size() == 4 && expected.size() == 2);
                        for (size_t layer = 0; layer < 2; ++layer) {
                            const Poly& old = expected[layer].poly;
                            const Poly& body = actual[layer * 2].poly;
                            const Poly& head = actual[layer * 2 + 1].poly;
                            size_t n = (old.pts.size() - 3) / 2;
                            assert(body.pts.size() == n * 2 && head.pts.size() == 5);
                            assert(actual[layer * 2].colour == expected[layer].colour);
                            for (size_t i = 0; i < body.pts.size(); ++i) {
                                const Point& point = old.pts[i < n ? i : i + 3];
                                assert(body.pts[i].x == point.x && body.pts[i].y == point.y);
                            }
                            assert(head.pts[1].x == old.pts[n + 1].x && head.pts[1].y == old.pts[n + 1].y);
                        }
                    }
                }
            }
        }
    unsetenv("NBT_HUD_CANVAS"); unsetenv("NBT_HUD_GLYPH"); unsetenv("NBT_HUD_GLYPHS");
    printf("  other profiles and manual pins: parametric routes unchanged, Mapbox tips and stems unchanged\n");
}

// A profile without a route overlay: the route goes into the picture with its roads, and each stage redraws them.
static void testJunctionNoOverlay() {
    HudProfile p = k6wa;
    p.routeA = p.routeB = 0;
    FakePort port;
    Hud6waDisplay d(port, p, false);
    NavView v = timed(8, "Testroad", "200", 1, 280, 15);
    assert(d.showManeuver(v, 0, true, true) == ClusterDisplay::kOk);
    assert(port.batches.size() == 1 && d.nearShown() && d.routeStage() == 4);
    assert(fills(port.batches[0]) == 4 && callsOn(port.batches[0], 7) == 0);   // edge, road, edge, route
    v.step_m = 120;
    assert(d.keepUp(v, 1) == ClusterDisplay::kOk && port.batches.size() == 1);
    assert(d.sameManeuver(v, 2));
    assert(d.showManeuver(v, 2, false, false) == ClusterDisplay::kOk && port.batches.size() == 2);
    assert(d.routeStage() == 2 && !d.sameManeuver(v, 3));
    assert(d.clearBatch().calls.size() == 8);
}

// Lanes in 9/10 when on: drawn when the phone sends them, once per change, emptied when they go.
static void testJunctionLanes() {
    g_log.clear();
    FakePort port;
    Hud6waDisplay d(port, k6wa, false);
    d.setLog(&logFn, NULL);
    Settings s;
    s.hud_lanes = true;
    d.setSettings(s);
    NavView v = timed(8, "Testroad", "0.5", 5, 800, 60);
    assert(d.showManeuver(v, 0, true, true) == ClusterDisplay::kOk && port.batches.size() == 1);
    v.lanes.push_back(oneLane(1, false));
    v.lanes.push_back(oneLane(1, false));
    v.lanes.push_back(oneLane(5, true));
    assert(d.keepUp(v, 1) == ClusterDisplay::kOk && port.batches.size() == 2);
    const Batch& l = port.batches[1];
    assert(l.label == "lanes" && l.slot == kSlotLanes && l.calls.size() == 8);
    expectCall(l.calls[0], kRemoveAllItems, 9);
    expectCall(l.calls[6], kSwitchVisibility, 10);
    assert(rdBe16(&l.calls[6].payload[2]) == 9);
    assert(logged("LANES 3 | 8 calls"));
    assert(d.keepUp(v, 2) == ClusterDisplay::kOk && port.batches.size() == 2);   // the same lanes
    v.lanes[0][0].highlighted = true;
    assert(d.keepUp(v, 3) == ClusterDisplay::kOk && port.batches.size() == 3);
    expectCall(port.batches[2].calls[0], kRemoveAllItems, 10);                  // the other buffer
    v.lanes.clear();
    assert(d.keepUp(v, 4) == ClusterDisplay::kOk && port.batches.size() == 4);
    assert(port.batches[3].label == "lanes off" && port.batches[3].calls.size() == 2);
    assert(d.keepUp(v, 5) == ClusterDisplay::kOk && port.batches.size() == 4);
    std::string detail;
    assert(d.clear(&detail) && port.batches.back().calls.size() == 14);
}

// The measured F30 lane layer uses the same native artwork, within the terminal's 64-call admission limit.
static void testUnifiedLanes() {
    unsetenv("NBT_HUD_CANVAS"); unsetenv("NBT_HUD_GLYPH"); unsetenv("NBT_HUD_LANES_BOX");
    unsigned largest = 0;
    for (int n = 1; n <= 8; ++n) for (int shape = 0; shape <= 10; ++shape) {
        FakePort port; port.mw = 126; port.mh = 77;
        Hud6waDisplay display(port,k6wa,false); Settings settings; settings.hud_lanes = true;
        display.setSettings(settings);
        NavView v = timed(8,"Testroad","1000",1,1000,100);
        int perLane = 24/n;
        v.lanes.assign(n,kombinav::AaLane(perLane,kombinav::LaneDirection(shape,false)));
        v.lanes[0][0].highlighted = true;
        assert(display.showManeuver(v,0,true,true) == ClusterDisplay::kOk);
        const Batch& batch = port.batches.back();
        assert(batch.slot == kSlotLanes);
        Picture actual = batchPicture(batch);
        assert(actual.size() == (size_t)n*perLane*2);
        for (size_t i = 0; i < actual.size(); ++i)
            for (size_t j = 0; j < actual[i].poly.pts.size(); ++j) {
                const Point& p = actual[i].poly.pts[j];
                assert(p.x >= 0 && p.x <= 183 && p.y >= 0 && p.y <= 16);
            }
        Config config; config.handles = handlesOf(k6wa); hudterm::Core terminal(config);
        terminal.setLink(true,0x2077,0); terminal.setHost(0x0123,0);
        assert(terminal.submit(batch,0) == kQueued);
        largest = std::max(largest,(unsigned)batch.calls.size());
        assert(display.keepUp(v,1) == ClusterDisplay::kOk && port.batches.size() == 2);
        if (shape == 0 || shape == 10) {
            // Unrecognised shapes keep the straight fallback, including its selected colour.
            for (size_t i = 0; i < v.lanes.size(); ++i)
                for (size_t j = 0; j < v.lanes[i].size(); ++j) v.lanes[i][j].shape = 1;
            assert(display.keepUp(v,2) == ClusterDisplay::kOk);
            samePictureItems(batchPicture(port.batches.back()),actual);
        }
    }
    assert(largest == 53);
    for (int choice = 0; choice < 3; ++choice) {
        HudProfile profile = k6wa;
        if (choice == 0) fitMeasured(176,126,profile);
        if (choice == 1) { fitMeasured(126,77,profile); profile.lanes = Rect{2,0,181,16}; }
        if (choice == 2) { fitMeasured(126,77,profile); setenv("NBT_HUD_LANES_BOX","0 0 183 16",1); }
        FakePort port; Hud6waDisplay display(port,profile,false);
        Settings settings; settings.hud_lanes = true; display.setSettings(settings);
        NavView v = timed(8,"Testroad","1000",1,1000,100); v.lanes.push_back(oneLane(6,true));
        assert(display.showManeuver(v,0,true,true) == ClusterDisplay::kOk);
        Picture expected; assert(drawLanes(v.lanes,profile.lanes,profile.road,profile.route,expected));
        Picture actual = batchPicture(port.batches.back()); assert(actual.size() == expected.size());
        samePictureItems(actual,expected);
        unsetenv("NBT_HUD_LANES_BOX");
    }
    printf("  unified lanes: all shapes/fallback, up to 24 arrows / 53 calls, custom boxes and other profiles unchanged\n");
}

// Through the state machine: the arrow at the warning, the junction view at the main prompt (in place of the reminder:
// a picture switches the panel on anyway), then the route's stages.
static void testJunctionCore() {
    g_log.clear();
    FakePort port;
    Hud6waDisplay d(port, k6wa, false);
    kombinav::Core core(d, Settings(), &logFn, NULL);
    NavView a = timed(8, "Testroad Alpha", "0.4", 5, 640, 60);
    core.step(&a, 0);
    assert(!core.shown() && port.batches.empty());
    a.step_s = 45;
    core.step(&a, 1);
    assert(core.shown() && port.batches.size() == 1 && !d.nearShown());
    a.step_s = 18;
    a.step_m = 330;
    core.step(&a, 2);
    assert(port.batches.size() == 3 && d.nearShown() && port.batches[2].label == "route");
    assert(!logged("REMIND"));
    a.step_s = 6;
    a.step_m = 100;
    core.step(&a, 3);
    assert(port.batches.back().label == "route" && d.routeStage() == 2);
}

// ---- the transport's states ----------------------------------------------------------------------------------------

static void testStates() {
    g_log.clear();
    FakePort port;
    Hud6waDisplay d(port, k6wa, false);
    d.setSettings(arrows());
    d.setLog(&logFn, NULL);
    NavView v = view(8, "Testroad", "200", 1, 200);
    std::string detail;

    port.st = kOffline;
    assert(d.showManeuver(v, 0, true, true) == ClusterDisplay::kLink && port.batches.empty());
    assert(!d.ready() && !d.factoryActive());
    assert(!d.clear(&detail) && detail == "HUDTerminal offline");

    port.st = kNavActive;
    assert(d.factoryActive());
    assert(d.showManeuver(v, 0, true, true) == ClusterDisplay::kOk && port.batches.empty());
    assert(d.clear(&detail) && port.batches.empty());           // the navigation clears the panel itself

    port.st = kFailed;
    port.afterReset = kFailed;
    assert(d.showManeuver(v, 0, true, true) == ClusterDisplay::kRejected);
    assert(d.keepUp(v, 0) == ClusterDisplay::kRejected && d.updateDistance(v, 0) == ClusterDisplay::kRejected);
    assert(!d.clear(&detail) && port.resets == 1 && detail == "HUDTerminal failed");
    port.afterReset = kReady;
    assert(d.clear(&detail) && port.resets == 2 && port.batches.size() == 1);   // best effort after a reset
    d.forget();
    port.batches.clear();

    // Work the terminal dropped: the next step draws again, texts switched on again.
    assert(port.st == kReady && d.ready());
    assert(d.showManeuver(v, 0, true, true) == ClusterDisplay::kOk);
    assert(!d.sameManeuver(v, 1));
    port.ep = 7;
    assert(d.sameManeuver(v, 2));
    assert(logged("drawing it again"));
    assert(d.showManeuver(v, 2, false, false) == ClusterDisplay::kOk);
    const Batch& r = port.batches.back();
    assert(r.label == "redraw" && r.full);
    bool vis = false;
    for (size_t i = 0; i < r.calls.size(); ++i) vis = vis || (r.calls[i].fkt == kSetVisibility && r.calls[i].handle() == 2);
    assert(vis && !d.sameManeuver(v, 3));

    // The terminal refused our batch: our data is wrong.
    port.result = kRefused;
    assert(d.showManeuver(view(7, "Testroad", "1", 2, 1000), 4, true, false) == ClusterDisplay::kInternal);
}

// CarPlay Advanced ended mid-route without its frame-off: the terminal yields for good. A new guidance session of ours
// takes the HUD once that navigation is quiet (Port::claim) and draws everything afresh, also when the step it missed
// while yielding already counts as shown. A session that has drawn keeps yielding as before, until it ends.
static void testClaim() {
    g_log.clear();
    FakePort port;
    Hud6waDisplay d(port, k6wa, false);
    d.setSettings(arrows());
    d.setLog(&logFn, NULL);
    NavView v = view(8, "Testroad", "200", 1, 200);
    port.st = kNavActive;
    assert(d.showManeuver(v, 0, true, true) == ClusterDisplay::kOk && port.batches.empty());   // not quiet yet
    assert(port.claims == 1 && port.st == kNavActive);
    port.claimable = true;
    assert(d.updateDistance(view(8, "Testroad", "150", 1, 150), 1) == ClusterDisplay::kOk);     // quiet now: ours
    assert(port.st == kReady && logged("taken for our new guidance"));
    assert(d.sameManeuver(v, 2));                             // the missed step is drawn afresh, texts on
    assert(d.showManeuver(v, 2, false, false) == ClusterDisplay::kOk);
    assert(port.batches.back().label == "redraw" && port.batches.back().full);
    bool texts = false;
    for (size_t i = 0; i < port.batches.back().calls.size(); ++i)
        texts = texts || (port.batches.back().calls[i].fkt == kSetVisibility && port.batches.back().calls[i].handle() == k6wa.street);
    assert(texts);

    int claims = port.claims;
    port.st = kNavActive;                                     // the navigation draws over our session again
    assert(d.showManeuver(view(7, "Other", "300", 1, 300), 3, true, false) == ClusterDisplay::kOk);
    assert(port.st == kNavActive && port.claims == claims);   // a session that drew does not take it back
    std::string detail;
    assert(d.clear(&detail));                                 // our guidance ends; the next one may take it
    assert(d.showManeuver(v, 4, true, true) == ClusterDisplay::kOk && port.st == kReady);
    assert(port.claims == claims + 1 && port.batches.back().label == "maneuver");
}

static void testShadow() {
    g_log.clear();
    FakePort port;
    Hud6waDisplay d(port, k6wa, true);
    d.setSettings(arrows());
    d.setLog(&logFn, NULL);
    NavView v = view(8, "Testroad Alpha", "300", 1, 300);
    assert(d.showManeuver(v, 0, true, true) == ClusterDisplay::kOk);
    NavView v2 = v;
    v2.dist_text = "250";
    assert(d.updateDistance(v2, 1) == ClusterDisplay::kOk);
    std::string detail;
    assert(d.clear(&detail));
    assert(port.batches.empty());
    assert(logged("(shadow: not sent)"));
    assert(logged("[shadow] 21D ImageAddPolygon h5 #0 polygon"));
    assert(logged("[shadow] 210 TextareaSetText h3 '250'"));
    assert(logged("[shadow] 204 SetVisibility h1 off"));
    port.st = kNavActive;                                        // the watch still yields
    assert(d.factoryActive());
}

// ---- the guidance state machine on the 6WA -------------------------------------------------------------------------

static void testCore() {
    g_log.clear();
    FakePort port;
    Hud6waDisplay d(port, k6wa, false);
    d.setSettings(arrows());
    Settings s = arrows();
    kombinav::Core core(d, s, &logFn, NULL);
    NavView a = view(8, "Testroad Alpha", "300", 1, 300);
    core.step(&a, 0);
    assert(core.shown() && port.batches.size() == 1 && port.batches[0].label == "maneuver");
    NavView a2 = view(8, "Testroad Alpha", "250", 1, 250);
    core.step(&a2, 0.2);                                         // under min_update_s: held
    assert(port.batches.size() == 1);
    core.step(&a2, 0.6);
    assert(port.batches.size() == 2 && port.batches[1].label == "distance");
    NavView b = view(7, "Sample Lane Beta", "150", 1, 150);
    core.step(&b, 1.0);
    assert(port.batches.size() == 3 && port.batches[2].label == "maneuver");
    port.ep = 1;                                                 // a link blip dropped work
    core.step(&b, 1.5);
    assert(port.batches.size() == 4 && port.batches[3].label == "redraw");
    core.step(NULL, 2.0);                                        // guidance gone: the grace period first
    assert(port.batches.size() == 4);
    core.step(NULL, 2.0 + s.grace_s);
    assert(!core.shown() && port.batches.size() == 5 && port.batches[4].label == "clear");
    assert(logged("CLEAR (guidance ended)") && logged("cancel: OK"));

    port.st = kFailed;                                           // a failed terminal stops guidance
    port.afterReset = kFailed;
    core.step(&a, 20);
    assert(core.stopped() == kombinav::Core::kStoppedRejected);
}

// ---- the idle text ("now playing") ----------------------------------------------------------------------------------

// The driver: the whole picture once, then only what changed; nothing while the factory navigation has the panel or a
// batch waits; drawn again after lost work; a maneuver takes its place and a clear removes it.
static void testIdleText() {
    g_log.clear();
    FakePort port;
    Hud6waDisplay d(port, k6wa, false);
    d.setLog(&logFn, NULL);
    Settings s;
    d.setSettings(s);

    assert(d.idleText("Example Song - Sample Artist") == "Example Song - Sample Artist");
    assert(d.idleText("Za\xC5\xBC\xC3\xB3\xC5\x82\xC4\x87, g\xC4\x99\xC5\x9Bl\xC4\x85 (live)") == "Zazolc gesla live");
    assert(d.idleText("An Invented Title That Is Far Too Long For The Line").size() == 32);
    assert(d.idleText("\xE6\x97\xA5\xE6\x9C\xAC").empty());            // nothing the font has

    assert(d.showIdleText("", 0) == ClusterDisplay::kOk && port.batches.empty() && !d.sentAny());
    assert(d.showIdleText("Example Song - Sample Artist", 0) == ClusterDisplay::kOk && port.batches.size() == 1);
    const Batch& b = port.batches[0];
    assert(b.label == "media" && b.full && b.calls.size() == 9 && d.sentAny());
    expectCall(b.calls[0], kRemoveAllItems, 5);
    expectCall(b.calls[1], kRemoveAllItems, 6);
    expectCall(b.calls[2], kSetVisibility, 2);
    expectCall(b.calls[3], kSetVisibility, 3);
    expectCall(b.calls[4], kSetVisibility, 4);
    expectCall(b.calls[5], kTextareaSetText, 2);
    assert(text(b.calls[5]) == "Example Song - Sample Artist");
    expectCall(b.calls[6], kTextareaSetText, 3);
    expectCall(b.calls[7], kTextareaSetText, 4);
    assert(text(b.calls[6]).empty() && text(b.calls[7]).empty());
    expectCall(b.calls[8], kSetVisibility, 1);
    assert(b.calls[8].payload[2] == 1);
    assert(logged("MEDIA 'Example Song - Sample Artist' | 9 calls"));

    // The same line again: nothing, however often (no refresh by default).
    assert(d.showIdleText("Example Song - Sample Artist", 5) == ClusterDisplay::kOk);
    assert(d.showIdleText("Example Song - Sample Artist", 500) == ClusterDisplay::kOk && port.batches.size() == 1);

    // The next track: the street text and the panel on again, partial (merged into a waiting batch, never replacing it).
    port.wait = true;
    assert(d.showIdleText("Other Song - Sample Artist", 6) == ClusterDisplay::kOk && port.batches.size() == 2);
    const Batch& t = port.batches[1];
    assert(t.label == "media-text" && !t.full && t.calls.size() == 2);
    expectCall(t.calls[0], kTextareaSetText, 2);
    assert(text(t.calls[0]) == "Other Song - Sample Artist");
    expectCall(t.calls[1], kSetVisibility, 1);
    port.wait = false;

    // The refresh, when it is set: the panel on again, once per interval.
    s.media_refresh_s = 10;
    d.setSettings(s);
    assert(d.showIdleText("Other Song - Sample Artist", 15) == ClusterDisplay::kOk && port.batches.size() == 2);
    assert(d.showIdleText("Other Song - Sample Artist", 16.5) == ClusterDisplay::kOk && port.batches.size() == 3);
    assert(port.batches[2].label == "media-refresh" && !port.batches[2].full && port.batches[2].calls.size() == 1);
    expectCall(port.batches[2].calls[0], kSetVisibility, 1);
    assert(d.showIdleText("Other Song - Sample Artist", 20) == ClusterDisplay::kOk && port.batches.size() == 3);
    assert(logged("MEDIA panel on again"));

    // The factory navigation has the panel: nothing is sent.
    port.st = kNavActive;
    assert(d.showIdleText("Third Song", 30) == ClusterDisplay::kOk && port.batches.size() == 3);
    port.st = kReady;

    // Work the terminal dropped: the whole picture again, texts switched on again; it waits for the queue.
    port.ep = 3;
    port.wait = true;
    assert(d.showIdleText("Third Song", 31) == ClusterDisplay::kOk && port.batches.size() == 3);
    port.wait = false;
    assert(d.showIdleText("Third Song", 32) == ClusterDisplay::kOk && port.batches.size() == 4);
    assert(port.batches[3].label == "media" && port.batches[3].full && port.batches[3].calls.size() == 9);
    assert(text(port.batches[3].calls[5]) == "Third Song" && logged("drawing it again"));

    // A maneuver takes the idle text's place: its own street, distance and unit; the texts are on already. Far enough
    // that it is an arrow, not yet the junction view (near_m).
    NavView v = view(8, "Testroad Alpha", "800", 1, 800);
    assert(d.showManeuver(v, 40, true, true) == ClusterDisplay::kOk && port.batches.size() == 5);
    const Batch& m = port.batches[4];
    assert(m.label == "maneuver");
    for (size_t i = 0; i < m.calls.size(); ++i)
        assert(!(m.calls[i].fkt == kSetVisibility && m.calls[i].handle() != 1));
    NavView v2 = view(8, "Testroad Alpha", "750", 1, 750);
    assert(d.updateDistance(v2, 41) == ClusterDisplay::kOk && port.batches.size() == 6);
    assert(port.batches[5].calls.size() == 1 && text(port.batches[5].calls[0]) == "750");

    // After a clear the idle text is the whole picture again.
    std::string detail;
    assert(d.clear(&detail));
    d.forget();
    assert(!d.sentAny());
    assert(d.showIdleText("Third Song", 50) == ClusterDisplay::kOk);
    assert(port.batches.back().label == "media" && port.batches.back().calls.size() == 9);

    // The transport's states, as for a maneuver.
    port.st = kOffline;
    assert(d.showIdleText("Fourth Song", 60) == ClusterDisplay::kLink);
    port.st = kFailed;
    assert(d.showIdleText("Fourth Song", 61) == ClusterDisplay::kRejected);
    port.st = kReady;
    port.result = kRefused;
    assert(d.showIdleText("Fourth Song", 62) == ClusterDisplay::kInternal);

    // Shadow mode: logged, nothing sent.
    FakePort ps;
    Hud6waDisplay ds(ps, k6wa, true);
    ds.setLog(&logFn, NULL);
    assert(ds.showIdleText("Example Song", 0) == ClusterDisplay::kOk && ps.batches.empty());
    assert(logged("[shadow] 210 TextareaSetText h2 'Example Song'"));
}

// The state machine: the idle text only while no maneuver is up, back after the clear, gone when nothing plays.
static void testCoreIdle() {
    g_log.clear();
    FakePort port;
    Hud6waDisplay d(port, k6wa, false);
    Settings s;
    s.approach = kombinav::kApproachOff;
    kombinav::Core core(d, s, &logFn, NULL);

    core.step(NULL, 0);
    core.idle("", 0);
    assert(!core.idleShown() && port.batches.empty());            // nothing playing: nothing sent
    core.idle("\xE6\x97\xA5\xE6\x9C\xAC", 0);
    assert(!core.idleShown() && port.batches.empty());            // nothing of it can be shown
    core.idle("Example Song - Sample Artist", 1);
    assert(core.idleShown() && !core.shown() && port.batches.size() == 1 && port.batches[0].label == "media");

    NavView a = view(8, "Testroad Alpha", "800", 1, 800);         // guidance: the maneuver has the panel
    core.step(&a, 2);
    core.idle("Example Song - Sample Artist", 2);
    assert(core.shown() && !core.idleShown() && port.batches.size() == 2 && port.batches[1].label == "maneuver");
    core.step(&a, 3);
    core.idle("Other Song", 3);                                   // a new track changes nothing while it is shown
    assert(port.batches.size() == 2);

    core.step(NULL, 4);                                           // guidance gone: the grace period, then the clear
    core.idle("Other Song", 4);
    assert(port.batches.size() == 2);
    core.step(NULL, 4 + s.grace_s);
    assert(!core.shown() && port.batches.size() == 3 && port.batches[2].label == "clear");
    core.idle("Other Song", 4 + s.grace_s);
    assert(core.idleShown() && port.batches.size() == 4 && port.batches[3].label == "media");
    assert(text(port.batches[3].calls[5]) == "Other Song");

    core.step(NULL, 20);                                          // playback stopped: cleared
    core.idle("", 20);
    assert(!core.idleShown() && port.batches.size() == 5 && port.batches[4].label == "clear");
    assert(logged("CLEAR (nothing playing)"));
    core.step(NULL, 21);
    core.idle("", 21);
    assert(port.batches.size() == 5);

    core.idle("Other Song", 22);                                  // the factory navigation takes the panel
    assert(core.idleShown() && port.batches.size() == 6);
    core.yieldToFactory();
    assert(!core.idleShown() && !core.sentAny() && port.batches.size() == 6);

    port.st = kFailed;                                            // a failed terminal stops it, as for a maneuver
    port.afterReset = kFailed;
    core.idle("Other Song", 30);
    assert(core.stopped() == kombinav::Core::kStoppedRejected);

    // media_show_s: a line is up for that long, then cleared, and stays away until it changes.
    {
        g_log.clear();
        FakePort p;
        Hud6waDisplay dd(p, k6wa, false);
        Settings b = s;
        b.media_show_s = 8;
        kombinav::Core c(dd, b, &logFn, NULL);
        c.idle("Example Song", 0);
        assert(c.idleShown() && p.batches.size() == 1 && p.batches[0].label == "media");
        c.idle("Example Song", 7.9);
        assert(c.idleShown() && p.batches.size() == 1);
        c.idle("Example Song", 8);                                // its time is up
        assert(!c.idleShown() && p.batches.size() == 2 && p.batches[1].label == "clear");
        assert(logged("CLEAR (now playing shown)"));
        c.idle("Example Song", 9);                                // the same line: not again
        c.idle("Example Song", 100);
        assert(!c.idleShown() && p.batches.size() == 2);
        c.idle("Other Song", 101);                                // the next track: up again, with its own time
        assert(c.idleShown() && p.batches.size() == 3 && p.batches[2].label == "media");
        c.idle("Third Song", 105);                                // changed while up: the text, and the time starts again
        assert(c.idleShown() && p.batches.size() == 4 && p.batches[3].label == "media-text");
        c.idle("Third Song", 112.9);
        assert(c.idleShown() && p.batches.size() == 4);
        c.idle("Third Song", 113);
        assert(!c.idleShown() && p.batches.size() == 5 && p.batches[4].label == "clear");

        c.idle("", 120);                                          // paused, then playing again: its line is shown again
        c.idle("Third Song", 121);
        assert(c.idleShown() && p.batches.size() == 6 && p.batches[5].label == "media");

        NavView m = view(8, "Testroad Alpha", "800", 1, 800);     // a maneuver cuts it short ...
        c.step(&m, 122);
        c.idle("Third Song", 122);
        assert(c.shown() && !c.idleShown() && p.batches.size() == 7);
        c.step(NULL, 123);
        c.step(NULL, 123 + b.grace_s);
        assert(!c.shown() && p.batches.size() == 8 && p.batches[7].label == "clear");
        c.idle("Third Song", 123 + b.grace_s);                    // ... so it gets its time afterwards
        assert(c.idleShown() && p.batches.size() == 9 && p.batches[8].label == "media");
    }

    // A cluster without an idle text (the default of the interface): never asked to show one.
    struct NoIdle : public ClusterDisplay {
        int shows;
        NoIdle() : shows(0) {}
        const char* name() const { return "none"; }
        void setSettings(const Settings&) {}
        void setLog(kombinav::LogFn, void*) {}
        bool sameManeuver(const NavView&, double) { return false; }
        Result showManeuver(const NavView&, double, bool, bool) { return kOk; }
        Result updateDistance(const NavView&, double) { return kOk; }
        Result keepUp(const NavView&, double) { return kOk; }
        Result showIdleText(const std::string&, double) { ++shows; return kOk; }
        void guidanceGap() {}
        bool clear(std::string*) { return true; }
        void forget() {}
        bool sentAny() const { return false; }
    } none;
    kombinav::Core c2(none, s, &logFn, NULL);
    c2.idle("Example Song", 0);
    assert(none.shows == 0 && !c2.idleShown());
}

// ---- KombiNav with NBT_CLUSTER=6wa ----------------------------------------------------------------------------------

// ImageGetSize is setup, not a drawing: it is also sent when guidance and now-playing are off.
static size_t hudRequests() {
    size_t n = 0;
    for (size_t i = 0; i < fake_dsi::calls.size(); ++i) {
        const fake_dsi::Call& c = fake_dsi::calls[i];
        if (c.kind != fake_dsi::Call::kRequest || c.id != most::kReqProcessMessage || c.payload.size() < 4) continue;
        most::View v;
        if (most::parseView(&c.payload[4], c.payload.size() - 4, v) && v.fblock == kFBlock && v.fkt != kGetSize) ++n;
    }
    return n;
}

static void runKombiNav(bool shadow) {
    char dir[] = "/tmp/hud6wa_testXXXXXX";
    assert(mkdtemp(dir));
    std::string shown = std::string(dir) + "/shown";
    KombiNav::kFlagFile = "/nonexistent/kombi-nav";
    KombiNav::kManualFile = "/nonexistent/kombi-nav";
    KombiNav::kShownFile = shown.c_str();
    setenv("NBT_KOMBI_NAV", "1", 1);
    setenv("NBT_KOMBI_NAV_YIELD", "0", 1);
    setenv("NBT_CLUSTER", "6wa", 1);
    if (shadow) setenv("NBT_HUDTERM", "log", 1);
    else unsetenv("NBT_HUDTERM");
    if (shadow) {                                                // a picture an earlier live run left up
        FILE* f = fopen(shown.c_str(), "w");
        assert(f);
        fclose(f);
    }
    fake_dsi::reset();
    {
        most::Client client;
        KombiNav nav;
        nav.start(&client);
        assert(strcmp(nav.clusterName(), "6wa") == 0);
        assert(client.start("T"));
        fake_dsi::attach();
        std::vector<uint8_t> resp;
        most::putLe32(resp, 77);
        most::putLe16(resp, 0x2077);
        fake_dsi::data(most::kInfoClientResponse, &resp[0], (uint32_t)resp.size());
        std::vector<uint8_t> reg;
        most::putLe32(reg, 1);
        most::putLe16(reg, 0x0123); most::putLe16(reg, most::registryKey(0x82, 0));
        fake_dsi::data(most::kAttrRegistry, &reg[0], (uint32_t)reg.size());

        kntest::Bytes s1 = kntest::status(1), st = kntest::navState(8, "Testroad Alpha"),
                      cp = kntest::curPos(300, "300", 1);
        KombiNav::navStatusCallback(0x8003, &s1[0], s1.size(), &nav);
        KombiNav::navStatusCallback(0x8006, &st[0], st.size(), &nav);
        KombiNav::navStatusCallback(0x8007, &cp[0], cp.size(), &nav);
        usleep(1000000);
        nav.logHealth();
        nav.stop();                                              // exit clear: logged (shadow) or sent (live)
        fake_dsi::detach();
    }                                                            // the terminal's thread is joined here
    size_t n = hudRequests();
    printf("KombiNav 6wa %s: %u calls to 82/00\n", shadow ? "shadow" : "live", (unsigned)n);
    if (shadow) assert(n == 0 && access(shown.c_str(), F_OK) == 0);   // left for the next live run to clear
    else assert(n >= 8);                                         // a maneuver's calls, up to 8 in flight unanswered
    unsetenv("NBT_CLUSTER");
    unsetenv("NBT_HUDTERM");
    unsetenv("NBT_KOMBI_NAV");
    unsetenv("NBT_KOMBI_NAV_YIELD");
    unlink(shown.c_str());
    rmdir(dir);
}

// KombiNav with only "now playing in the cluster" on (the arrows off): the line goes out to 82/00, and nothing does
// while the setting is off.
static size_t runKombiMedia(bool on) {
    char dir[] = "/tmp/hud6wa_mediaXXXXXX";
    assert(mkdtemp(dir));
    std::string shown = std::string(dir) + "/shown";
    KombiNav::kFlagFile = "/nonexistent/kombi-nav";
    KombiNav::kManualFile = "/nonexistent/kombi-nav";
    KombiNav::kMediaFlagFile = "/nonexistent/kombi-media";
    KombiNav::kMediaManualFile = "/nonexistent/kombi-media";
    KombiNav::kShownFile = shown.c_str();
    setenv("NBT_KOMBI_NAV", "0", 1);
    setenv("NBT_KOMBI_NAV_YIELD", "0", 1);
    setenv("NBT_KOMBI_MEDIA", on ? "1" : "0", 1);
    setenv("NBT_CLUSTER", "6wa", 1);
    unsetenv("NBT_HUDTERM");
    assert(KombiNav::mediaSettingOn() == on);
    fake_dsi::reset();
    {
        most::Client client;
        KombiNav nav;
        nav.start(&client);
        assert(client.start("T"));
        fake_dsi::attach();
        std::vector<uint8_t> resp;
        most::putLe32(resp, 77);
        most::putLe16(resp, 0x2077);
        fake_dsi::data(most::kInfoClientResponse, &resp[0], (uint32_t)resp.size());
        std::vector<uint8_t> reg;
        most::putLe32(reg, 1);
        most::putLe16(reg, 0x0123); most::putLe16(reg, most::registryKey(0x82, 0));
        fake_dsi::data(most::kAttrRegistry, &reg[0], (uint32_t)reg.size());
        nav.setNowPlaying("Example Song - Sample Artist");
        usleep(1500000);                                         // the setting is read once a second
        nav.stop();
        fake_dsi::detach();
    }
    size_t n = hudRequests();
    printf("KombiNav 6wa now playing %s: %u calls to 82/00\n", on ? "on" : "off", (unsigned)n);
    unsetenv("NBT_CLUSTER");
    unsetenv("NBT_KOMBI_NAV");
    unsetenv("NBT_KOMBI_NAV_YIELD");
    unsetenv("NBT_KOMBI_MEDIA");
    unlink(shown.c_str());
    rmdir(dir);
    return n;
}

static void testKombiNavUnknownCluster() {
    setenv("NBT_CLUSTER", "6wb", 1);
    {
        KombiNav nav;
        nav.start(NULL);                                         // logged, nothing started
        assert(strcmp(nav.clusterName(), "none") == 0);
        nav.stop();
    }
    setenv("NBT_CLUSTER", "6wa", 1);
    {
        KombiNav nav;
        nav.start(NULL);                                         // no MOST client: disabled
        assert(strcmp(nav.clusterName(), "none") == 0);
    }
    unsetenv("NBT_CLUSTER");
}

// The picture-buffer size -> canvas and glyph (cluster/hud6wa_profile.hpp fitMeasured): the canvas is the measured
// buffer; the glyph box moves only where the F20 profile's would be cut off (to the factory arrow's place on the F30's
// 126x77 HUD), stays on a buffer it fits, and the profile is left alone on 0x0 (no answer) or an env pin.
static void testFitMeasured() {
    unsetenv("NBT_HUD_CANVAS");
    unsetenv("NBT_HUD_GLYPH");
    {   // the F30's 126x77 HUD cuts the F20 box off: the factory arrow's box there (MOST capture, sabifa in #264)
        HudProfile p = k6wa;
        fitMeasured(126, 77, p);
        assert(p.canvas.x0 == 0 && p.canvas.y0 == 0 && p.canvas.x1 == 125 && p.canvas.y1 == 76);
        assert(p.glyph.x0 == 37 && p.glyph.y0 == 23 && p.glyph.x1 == 82 && p.glyph.y1 == 68);
    }
    {   // a MINI-sized 120x72 buffer: the same proportions
        HudProfile p = k6wa;
        fitMeasured(120, 72, p);
        assert(p.canvas.x1 == 119 && p.canvas.y1 == 71);
        assert(p.glyph.x0 == 35 && p.glyph.y0 == 22 && p.glyph.x1 == 78 && p.glyph.y1 == 64);
    }
    {   // the F30 cluster also fits the F20 box, but the factory arrow is centred further left (sabifa, #264)
        HudProfile p = k6wa;
        fitMeasured(176, 126, p);
        assert(p.glyph.x0 == 52 && p.glyph.y0 == 38 && p.glyph.x1 == 115 && p.glyph.y1 == 111);
    }
    {   // a buffer the F20 box fits (the F20's own): the box it was tuned with stays, only the canvas is the real one
        HudProfile p = k6wa;
        fitMeasured(199, 129, p);
        assert(p.canvas.x1 == 198 && p.canvas.y1 == 128);
        assert(p.glyph.x0 == k6wa.glyph.x0 && p.glyph.y0 == k6wa.glyph.y0 && p.glyph.x1 == k6wa.glyph.x1 &&
               p.glyph.y1 == k6wa.glyph.y1);
    }
    {   // no answer (0x0, a cluster that refuses ImageGetSize): the F20 profile, as before the measuring
        HudProfile p = k6wa;
        fitMeasured(0, 0, p);
        assert(p.canvas.x1 == k6wa.canvas.x1 && p.glyph.x0 == k6wa.glyph.x0 && p.glyph.y1 == k6wa.glyph.y1);
    }
    {   // an env pin wins for its own field; the other is still fitted
        setenv("NBT_HUD_GLYPH", "1 2 3 4", 1);
        HudProfile p = k6wa;
        applyHudEnv(p);                        // as nav_core does before the display sees it
        fitMeasured(126, 77, p);               // would move the box, but it is pinned
        assert(p.glyph.x0 == 1 && p.glyph.y0 == 2 && p.glyph.x1 == 3 && p.glyph.y1 == 4);
        assert(p.canvas.x1 == 125 && p.canvas.y1 == 76);
        unsetenv("NBT_HUD_GLYPH");
    }
    printf("  fitMeasured: F20 box kept where it fits, factory boxes on 126x77/176x126, 0x0 and env pins respected\n");
}

static void testNightOutlinePolicy() {
    HudProfile p=k6wa; fitMeasured(126,77,p);
    for (int type=0;type<=60;++type) for (int stage=0;stage<=4;++stage) {
        bool mirror;const unified::Glyph* glyph=unified::glyph(type,mirror);if(!glyph)continue;
        Picture day,night,expected;
        unified::roads(type,p,stage!=0,day);unified::route(type,p,stage,day);
        unified::roads(type,p,stage!=0,night,true);unified::route(type,p,stage,night,true);
        for(size_t i=0;i<day.size();++i)
            if(day[i].colour!=p.edge || (glyph->hole.count && i+1==day.size())) expected.push_back(day[i]);
        assert(expected.size()==night.size());
        for(size_t i=0;i<night.size();++i)assert(night[i].colour==expected[i].colour);
        samePolygons(picturePolygons(night),0,picturePolygons(expected));
    }
    FakePort port;Hud6waDisplay display(port,p,false);Settings s=arrows();s.junction=false;
    display.setSettings(s);NavView v=timed(8,"Road","800",1,800,60);
    assert(display.showManeuver(v,0,true,true)==ClusterDisplay::kOk);
    Shape day=packetPolygons(port.batches.back());
    s.hud_night=true;display.setSettings(s);
    assert(display.sameManeuver(v,1));
    assert(display.showManeuver(v,1,false,false)==ClusterDisplay::kOk);
    Picture expected;unified::far(8,p,expected,true);
    Shape actual=packetPolygons(port.batches.back());
    assert(actual.size()==expected.size());samePolygons(actual,0,picturePolygons(expected));
    s.hud_night=false;display.setSettings(s);assert(display.sameManeuver(v,2));
    assert(display.showManeuver(v,2,false,false)==ClusterDisplay::kOk);
    Shape restored=packetPolygons(port.batches.back());assert(restored.size()==day.size());samePolygons(restored,0,day);
}

// RemoveAllItems clears contents; it must not be assumed to reset an image's drawing pen.
struct NativePenWitness {
    int width[32], fill[32];
    NativePenWitness() { for(int i=0;i<32;++i){width[i]=6;fill[i]=7;} }
    void apply(const Batch& batch, bool resetRequired) {
        int resetHandle=-1;
        for(size_t i=0;i<batch.calls.size();++i){
            const Call& c=batch.calls[i];unsigned h=c.handle();assert(h<32);
            if(c.fkt==kRemoveAllItems) resetHandle=-1; // retain native width/fill across item clears
            else if(c.fkt==kSetLineWidth){assert(c.payload[2]==0);width[h]=0;resetHandle=h;}
            else if(c.fkt==kSetFillColor)fill[h]=c.payload[2];
            else if(c.fkt==kAddPolygon && resetRequired){
                assert((h==7||h==8) && resetHandle==(int)h && width[h]==0);
                assert(fill[h]==4 || fill[h]==5 || fill[h]==7); // legacy supplied pictures and explicit F30 near ink
            }
        }
    }
};

static void testNativeOverlayPen() {
    HudProfile p=k6wa;fitMeasured(126,77,p);FakePort port;Hud6waDisplay d(port,p,false);
    Settings settings=arrows();settings.junction=true;settings.hud_night=true;d.setSettings(settings);
    NavView v=timed(6,"Road","157",1,157,14);
    NativePenWitness witness;
    assert(d.showManeuver(v,0,true,true)==ClusterDisplay::kOk);
    const Batch first=port.batches.back();assert(first.slot==kSlotRoute);
    witness.apply(first,true);
    assert(first.calls[0].fkt==kRemoveAllItems && first.calls[1].fkt==kSetLineWidth);
    const uint16_t firstHandle=first.calls[1].handle();
    v.step_m=40;d.sameManeuver(v,1);assert(d.keepUp(v,1)==ClusterDisplay::kOk);
    const Batch second=port.batches.back();assert(second.slot==kSlotRoute);
    witness.apply(second,true);assert(second.calls[1].handle()!=firstHandle);
    // Simulate another writer leaving a pen, then check that reuse of each buffer sets it again.
    witness.width[7]=22;witness.width[8]=22;
    v.step_m=10;d.sameManeuver(v,2);assert(d.keepUp(v,2)==ClusterDisplay::kOk);
    witness.apply(port.batches.back(),true);
    assert(witness.width[5]==6 && witness.width[6]==6); // this witness has replayed only route batches
    Picture route;
    for(int night=0;night<=1;++night){
        route.clear();unified::route(6,p,4,route,night!=0);
        Settings st=arrows();st.hud_night=night!=0;
        Hud6waDisplay current(port,p,false);current.setSettings(st);
        Batch b=current.routeBatch(route);int n=0;for(size_t j=0;j<b.calls.size();++j)if(b.calls[j].fkt==kSetLineWidth)++n;
        assert(n==1);witness.width[7]=6;witness.width[8]=6;witness.apply(b,true);
        Batch main=current.pictureBatch("ring",route,HudTexts(),false);
        Batch lanes=current.lanesBatch(route);
        for(size_t j=0;j<main.calls.size();++j)assert(main.calls[j].fkt!=kSetLineWidth);
        for(size_t j=0;j<lanes.calls.size();++j)assert(lanes.calls[j].fkt!=kSetLineWidth);
        Hud6waDisplay fallback(port,k6wa,false);fallback.setSettings(st);
        Batch other=fallback.routeBatch(route);for(size_t j=0;j<other.calls.size();++j)assert(other.calls[j].fkt!=kSetLineWidth);
    }
    setenv("NBT_HUD_GLYPH","37 23 82 68",1);
    Batch pinned=d.routeBatch(route);for(size_t j=0;j<pinned.calls.size();++j)assert(pinned.calls[j].fkt!=kSetLineWidth);
    unsetenv("NBT_HUD_GLYPH");
    HudProfile otherMap=p;otherMap.routeA=9;otherMap.routeB=10;
    Hud6waDisplay mapped(port,otherMap,false);mapped.setSettings(settings);
    Batch unmapped=mapped.routeBatch(route);
    for(size_t j=0;j<unmapped.calls.size();++j)assert(unmapped.calls[j].fkt!=kSetLineWidth);
    Batch empty=d.routeBatch(Picture());for(size_t j=0;j<empty.calls.size();++j)assert(empty.calls[j].fkt!=kSetLineWidth);
}

// Base profile/far/ring roles stay unchanged; explicit near policy is tested through serialized driver batches.
static void testMeasuredF30ColourRoles() {
    HudProfile p=k6wa;fitMeasured(126,77,p);
    assert(p.fill==4 && p.route==4 && p.road==3);
    for(int type=0;type<=52;++type){
        bool mirror;const unified::Glyph* g=unified::glyph(type,mirror);if(!g)continue;
        Picture roads,route;
        unified::roads(type,p,true,roads,true);
        for(size_t i=0;i<roads.size();++i)assert(roads[i].colour==3);
        for(int stage=0;stage<=4;++stage){
            route.clear();unified::route(type,p,stage,route,true);
            assert(!route.empty());
            // A destination's existing interior mask is separate from the arrow/road roles.
            for(size_t i=0;i<route.size();++i)
                assert(route[i].colour==4 || (g->hole.count && i+1==route.size() && route[i].colour==7));
        }
    }
    for(int clockwise=0;clockwise<2;++clockwise){
        ShapeSpec spec=shapeFor(clockwise?33:35,true,225);Picture ring;
        assert(ring60::picture(spec,p,ring,3));bool background=false,active=false;
        for(size_t i=0;i<ring.size();++i){assert(ring[i].colour==3 || ring[i].colour==4);
            background=background || ring[i].colour==3;active=active || ring[i].colour==4;}
        assert(background && active);
    }
}


static void requireZeroPen(const Batch& b) {
    int cleared=-1,zero=-1; bool polygon=false;
    for(size_t i=0;i<b.calls.size();++i){
        const Call& c=b.calls[i];
        if(c.fkt==kRemoveAllItems){cleared=c.handle();zero=-1;}
        if(c.fkt==kSetLineWidth){assert(c.handle()==cleared && c.payload[2]==0);zero=c.handle();}
        if(c.fkt==kAddPolygon){assert(zero==c.handle());polygon=true;}
    }
    assert(polygon);
}

static void testExplicitF30NearPackets() {
    unsetenv("NBT_HUD_CANVAS");unsetenv("NBT_HUD_GLYPH");unsetenv("NBT_HUD_FILL");
    HudProfile p=k6wa;fitMeasured(126,77,p);
    const int types[]={6,8,11,25};
    for(size_t t=0;t<sizeof(types)/sizeof(types[0]);++t){
        FakePort port;Hud6waDisplay d(port,p,false);Settings s;d.setSettings(s);
        NavView v=timed(types[t],"Paint Road","10000",1,10000,600);
        assert(d.showManeuver(v,0,true,true)==ClusterDisplay::kOk);
        assert(!d.nearShown()); // motorway manoeuvres can already be near at800m
        const Picture far=batchPicture(port.batches.back());
        assert(!far.empty());
        assert(far.size()>=2 && far[far.size()-2].colour==4 && far.back().colour==4);
        for(size_t i=0;i<far.size();++i)assert(far[i].colour==3 || far[i].colour==4 || far[i].colour==7);
        const bool modes[]={false,true,false};
        for(size_t mode=0;mode<3;++mode){
            s.hud_night=modes[mode];d.setSettings(s);v.step_m=100;v.step_s=5;
            const size_t before=port.batches.size();
            assert(d.showManeuver(v,1+mode*5,true,false)==ClusterDisplay::kOk && d.nearShown());
            assert(port.batches.size()==before+2);
            Picture roads=batchPicture(port.batches[before]);
            for(size_t i=0;i<port.batches[before].calls.size();++i)assert(port.batches[before].calls[i].fkt!=kSetLineWidth);
            bool mirror;const unified::Glyph* g=unified::glyph(types[t],mirror);assert(g);
            assert(roads.size()==g->nearCount); // one road fill per branch: no duplicate edge polygon
            for(size_t i=0;i<roads.size();++i)assert(roads[i].colour==3);
            for(int stage=4;stage>=1;--stage){
                if(stage!=4){v.step_m=stage*25;d.sameManeuver(v,mode*5+5-stage);assert(d.keepUp(v,mode*5+5-stage)==ClusterDisplay::kOk);}
                assert(d.routeStage()==stage);const Batch& b=port.batches.back();requireZeroPen(b);
                Picture route=batchPicture(b);assert(route.size()==2); // body and head, no edge copies
                for(size_t i=0;i<route.size();++i)assert(route[i].colour==(modes[mode]?5:7));
                samePolygons(picturePolygons(Picture(1,route.back())),0,picturePolygons(Picture(1,far.back())));
                assert(b.calls.size()<=64);
            }
        }
    }
    // Accepted far/ring ink remains4 and the ring geometry never changes with the software night bit.
    for(int cw=0;cw<2;++cw)for(int angle=0;angle<=360;angle+=90){
        FakePort port;Hud6waDisplay d(port,p,false);Settings s=arrows();Picture reference;
        NavView v=timed(cw?33:35,"Ring Road","100",1,100,5);
        v.has_exit_angle=angle!=0;v.exit_angle=angle;v.has_exit_number=true;v.exit_number=3;
        for(int mode=0;mode<3;++mode){s.hud_night=mode==1;d.setSettings(s);assert(d.showManeuver(v,mode,true,true)==ClusterDisplay::kOk);
            const Batch& b=port.batches.back();Picture ink=batchPicture(b);assert(!ink.empty());
            assert(ink.back().colour==4); // the accepted centred caption keeps its native fill role
            for(size_t i=0;i<ink.size();++i)assert(ink[i].colour==3 || ink[i].colour==4);
            for(size_t i=0;i<b.calls.size();++i)assert(b.calls[i].fkt!=kSetLineWidth);
            if(!mode)reference=ink;else samePictureItems(ink,reference);
        }
    }
    // Explicit override retains the previous fill/route split, even on the measured F30 geometry.
    setenv("NBT_HUD_FILL","2",1);HudProfile custom=k6wa;applyHudEnv(custom);fitMeasured(126,77,custom);
    for(int night=0;night<2;++night){
        FakePort port;Hud6waDisplay d(port,custom,false);Settings s;s.hud_night=night!=0;d.setSettings(s);
        NavView v=timed(8,"Override Road","800",1,800,60);assert(d.showManeuver(v,0,true,true)==ClusterDisplay::kOk);
        Picture far=batchPicture(port.batches.back());bool fill=false;for(size_t i=0;i<far.size();++i)fill=fill||far[i].colour==2;assert(fill);
        v.step_m=100;v.step_s=5;assert(d.showManeuver(v,1,true,false)==ClusterDisplay::kOk);
        Picture near=batchPicture(port.batches.back());bool route=false;for(size_t i=0;i<near.size();++i)route=route||near[i].colour==4;assert(route);
    }
    unsetenv("NBT_HUD_FILL");
}

// Real KombiNav worker -> real MOST serializer -> fake DSI. Replies are deliberately not immediate.
struct HudPaintWire {
    struct Reply { most::Msg msg; unsigned due; };
    size_t cursor;unsigned tick;std::vector<Reply> pending;std::vector<most::Msg> sent;
    bool measured;
    HudPaintWire():cursor(0),tick(0),measured(false){}
    void step(){
        std::vector<fake_dsi::Call> calls=fake_dsi::snapshot();
        for(;cursor<calls.size();++cursor){
            const fake_dsi::Call& c=calls[cursor];if(c.kind!=fake_dsi::Call::kRequest || c.id!=most::kReqProcessMessage)continue;
            most::Msg m;assert(c.payload.size()>=4 && most::parseMessage(&c.payload[4],c.payload.size()-4,m));
            if(m.fblock!=0x82 || m.inst!=0)continue;
            assert(m.src==0x2077 && m.dst==0x0123 && m.opType==most::kOpStartResultAck);
            sent.push_back(m);Reply r;r.msg=m;r.msg.src=m.dst;r.msg.dst=m.src;r.msg.txType=0;r.msg.varTag=2;r.msg.varVal=0;r.msg.opType=most::kOpResultAck;
            r.due=tick+(m.fkt==kGetSize?10:2);
            if(m.fkt==kGetSize){assert(m.data==getSize(5));r.msg.data=getSize(5);putBe16(r.msg.data,126);putBe16(r.msg.data,77);}
            pending.push_back(r);
        }
        for(size_t i=0;i<pending.size();){
            if(pending[i].due>tick){++i;continue;}
            if(pending[i].msg.fkt==kGetSize)measured=true;
            std::vector<uint8_t> b=most::buildMessage(pending[i].msg);fake_dsi::data(most::kInfoMessage,&b[0],b.size());pending.erase(pending.begin()+i);
        }
        ++tick;usleep(10000);
    }
    bool nearFrame(size_t first,int active)const{
        int fill[16],width[16];unsigned polygons[16];uint16_t main=0,route=0;
        for(int h=0;h<16;++h){fill[h]=-1;width[h]=-1;polygons[h]=0;}
        bool correct=true;
        for(size_t i=first;i<sent.size();++i){const most::Msg& m=sent[i];if(m.data.size()<2)continue;unsigned h=rdBe16(&m.data[0]);if(h>=16)continue;
            if(m.fkt==kRemoveAllItems)polygons[h]=0;
            if(m.fkt==kSetFillColor)fill[h]=m.data[2];
            if(m.fkt==kSetLineWidth)width[h]=m.data[2];
            if(m.fkt==kAddPolygon){++polygons[h];if(h==5||h==6)correct=correct&&fill[h]==3&&width[h]==-1;if(h==7||h==8)correct=correct&&fill[h]==active&&width[h]==0;}
            if(m.fkt==kSwitchVisibility){uint16_t shown=rdBe16(&m.data[2]);if(shown==5||shown==6)main=shown;if(shown==7||shown==8)route=shown;}
        }
        return correct && measured && main && route && polygons[main]==1 && polygons[route]==2 && pending.empty();
    }
};

static void testRealWorkerNightPaint() {
    const char* oldFlag=KombiNav::kFlagFile;const char* oldManual=KombiNav::kManualFile;const char* oldShown=KombiNav::kShownFile;
    char dir[]="/tmp/hudpaint_workerXXXXXX";assert(mkdtemp(dir));std::string shown=std::string(dir)+"/shown";
    KombiNav::kFlagFile=KombiNav::kManualFile="/nonexistent/hudpaint";KombiNav::kShownFile=shown.c_str();
    setenv("NBT_CLUSTER","6wa",1);setenv("NBT_KOMBI_NAV","1",1);setenv("NBT_KOMBI_NAV_YIELD","0",1);setenv("NBT_KOMBI_MEDIA","0",1);
    unsetenv("NBT_HUDTERM");unsetenv("NBT_HUD_CANVAS");unsetenv("NBT_HUD_GLYPH");unsetenv("NBT_HUD_FILL");fake_dsi::reset();
    {
        most::Client client;KombiNav nav;nav.postNightMode(true);nav.start(&client);assert(client.start("PAINTTEST"));fake_dsi::attach();
        std::vector<uint8_t> r;most::putLe32(r,77);most::putLe16(r,0x2077);fake_dsi::data(most::kInfoClientResponse,&r[0],r.size());
        r.clear();most::putLe32(r,1);most::putLe16(r,0x0123);most::putLe16(r,most::registryKey(0x82,0));fake_dsi::data(most::kAttrRegistry,&r[0],r.size());
        HudPaintWire wire;for(int i=0;i<30;++i)wire.step();assert(wire.measured); // delayed size before guidance -> known F30 guard
        kntest::Bytes status=kntest::status(1),state=kntest::navState(8,"Wire Paint Road"),pos=kntest::curPos(100,"100",1);
        KombiNav::navStatusCallback(0x8003,&status[0],status.size(),&nav);KombiNav::navStatusCallback(0x8006,&state[0],state.size(),&nav);KombiNav::navStatusCallback(0x8007,&pos[0],pos.size(),&nav);
        const bool modes[]={true,false,true,false};
        for(size_t mode=0;mode<4;++mode){
            size_t first=wire.sent.size();nav.postNightMode(modes[mode]);bool found=false;
            for(int i=0;i<250 && !found;++i){wire.step();found=wire.nearFrame(first,modes[mode]?5:7);}
            assert(found);for(int i=0;i<8;++i)wire.step(); // retire all ACKs before taking the next mode's packet slice
        }
        nav.onSessionEnded();for(int i=0;i<50;++i)wire.step();nav.stop();fake_dsi::detach();
    }
    unsetenv("NBT_CLUSTER");unsetenv("NBT_KOMBI_NAV");unsetenv("NBT_KOMBI_NAV_YIELD");unsetenv("NBT_KOMBI_MEDIA");
    KombiNav::kFlagFile=oldFlag;KombiNav::kManualFile=oldManual;KombiNav::kShownFile=oldShown;unlink(shown.c_str());rmdir(dir);
}

int main() {
    testExplicitF30NearPackets();
    testRealWorkerNightPaint();
    testNativeOverlayPen();
    testMeasuredF30ColourRoles();
    testNightOutlinePolicy();
    testShapesInside();
    testShapeFamilies();
    testMirror();
    testRoundaboutExit();
    testFitMeasured();
    testTexts();
    testShowBatches();
    testF15Batches();
    testF15RouteRestart();
    testClearBatch();
    testHold();
    testRemind();
    testJunctionGeometry();
    testMapboxGlyphs();
    testMapboxRoundaboutExit();
    testJunctionScaled();
    testDigits();
    testUnknownRoundabout();
    testUnknownCanvasPins();
    testCirculationGeometry();
    testKnownRoundabouts();
    testKnownEntryVisibility();
    testKnownRoundaboutDriver();
    testRoundaboutAngleLimits();
    testReadableHudArrows();
    testReadableHudCompatibility();
    testGlyphBatchBudget();
    testLanes();
    testJunctionDriver();
    testKnownInnerCircleSampling();
    testKnownRoundaboutAnchorsUnchanged();
    testCirculationNativeGrid();
    testSmallRoundaboutExits();
    testNearPolygonsSimple();
    testReadableNearArrows();
    testNearArrowCompatibility();
    testJunctionFits();
    testRefitFromProfile();
    testJunctionNoOverlay();
    testJunctionLanes();
    testUnifiedLanes();
    testJunctionCore();
    testStates();
    testClaim();
    testShadow();
    testCore();
    testIdleText();
    testCoreIdle();
    runKombiNav(true);
    runKombiNav(false);
    assert(runKombiMedia(false) == 0);
    assert(runKombiMedia(true) >= 8);                            // the idle picture's calls, up to 8 in flight unanswered
    testKombiNavUnknownCluster();
    printf("test_hud6wa: all passed\n");
    return 0;
}
