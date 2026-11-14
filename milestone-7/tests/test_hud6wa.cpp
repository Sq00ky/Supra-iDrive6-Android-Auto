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
#include "fake_dsi_client.hpp"
#include "kombi_nav_test_util.hpp"
#include <algorithm>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
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
    FakePort() : st(kReady), ep(0), result(kQueued), idle(true), wait(false), resets(0), afterReset(kReady) {}
    SubmitResult submit(const Batch& b) {
        if (st != kReady) return kNotReady;
        for (size_t i = 0; i < b.calls.size(); ++i) assert(callOk(b.calls[i], handlesOf(k6wa)));
        assert(b.calls.size() <= Config().maxBatch);
        batches.push_back(b);
        return result;
    }
    Status status() const { return st; }
    uint32_t epoch() const { return ep; }
    bool waiting(int) const { return wait; }
    void reset() { ++resets; if (st == kFailed) st = afterReset; }
    bool waitIdle(uint32_t) { return st == kReady && idle; }
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
    assert(describeSpec(shapeFor(34, false, 0)) == "roundabout ccw exit 180");   // no angle: straight across
    assert(describeSpec(shapeFor(34, true, 90)) == "roundabout ccw exit 90");
    assert(describeSpec(shapeFor(33, true, 270)) == "roundabout cw exit 270");
    assert(describeSpec(shapeFor(34, true, 400)) == "roundabout ccw exit 180");  // out of range: ignored
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
                if (kombinav::kombiDirection(t) == 10) {
                    assert(!has);                                // the destination keeps its flag
                    continue;
                }
                assert(has && L.maxTail >= 8 && !L.roads.empty());
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

static void testDigits() {
    for (int n = 0; n <= 9; ++n) {
        Shape s;
        digits::drawNumber(n, 50, 10, 18, s);
        assert(s.size() == (n == 0 || n == 6 || n == 8 || n == 9 ? 2u : 1u));
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

// The near stage through the driver: the arrow first, the junction view (roads in 5/6, route in 7/8) at the main
// prompt, the route's stages on the way in, held while a route waits, all drawn again after lost work, the route
// emptied for the next maneuver's arrow, and the clear putting 7/8 back.
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

    // A roundabout already near: its junction view at once, with the exit number in the route's colour.
    NavView ra = timed(32, "Ring Road", "150", 1, 150, 12);
    ra.has_exit_angle = true;
    ra.exit_angle = 270;
    ra.has_exit_number = true;
    ra.exit_number = 3;
    assert(d.showManeuver(ra, 11, true, false) == ClusterDisplay::kOk);
    assert(port.batches.size() == 11 && d.nearShown() && logged("junction view at once"));
    assert(logged("exit 3 |"));
    const Batch& rb = port.batches[9];
    assert(rb.label == "maneuver" && fills(rb) == 3 && rb.calls[rb.calls.size() - 1].fkt == kSetVisibility);
    assert(port.batches[10].label == "route" && d.routeStage() == 4);

    // The destination keeps its flag, near or not.
    NavView dest = timed(39, "Example Close", "50", 1, 50, 5);
    assert(d.showManeuver(dest, 12, true, false) == ClusterDisplay::kOk);
    assert(port.batches.size() == 12 && !d.nearShown() && !d.sameManeuver(dest, 13));
    assert(callsOn(port.batches[11], 7) == 1 && callsOn(port.batches[11], 8) == 1);   // the roundabout's route emptied

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

// ---- KombiNav with NBT_CLUSTER=6wa ----------------------------------------------------------------------------------

static size_t hudRequests() {
    size_t n = 0;
    for (size_t i = 0; i < fake_dsi::calls.size(); ++i) {
        const fake_dsi::Call& c = fake_dsi::calls[i];
        if (c.kind != fake_dsi::Call::kRequest || c.id != most::kReqProcessMessage || c.payload.size() < 4) continue;
        most::View v;
        if (most::parseView(&c.payload[4], c.payload.size() - 4, v) && v.fblock == kFBlock) ++n;
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

int main() {
    testShapesInside();
    testShapeFamilies();
    testMirror();
    testRoundaboutExit();
    testTexts();
    testShowBatches();
    testClearBatch();
    testHold();
    testRemind();
    testJunctionGeometry();
    testDigits();
    testLanes();
    testJunctionDriver();
    testJunctionNoOverlay();
    testJunctionLanes();
    testJunctionCore();
    testStates();
    testShadow();
    testCore();
    runKombiNav(true);
    runKombiNav(false);
    testKombiNavUnknownCluster();
    printf("test_hud6wa: all passed\n");
    return 0;
}
