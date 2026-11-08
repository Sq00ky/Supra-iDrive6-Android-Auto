// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test: what the J29 path sends, pinned. Scripted guidance (scripted time, no sleeps) goes through the guidance
// state machine, the graph and the SOME/IP client to a fake KOMBI on 127.0.0.1, and every request the KOMBI receives
// (16-byte header and body, on which connection), with every log line in between, is written as a transcript and
// compared with tests/fixtures/j29_golden.txt. Any change to a byte, the order, the session ids, the connections or
// the log fails it.
//
// Made for the cluster driver split: the transcript must stay identical while the code behind it moves. Only the
// setup (Golden below) may change with the code.
//
//   test_j29_golden FIXTURE             compare
//   test_j29_golden FIXTURE --update    write the fixture from this build (only for an intended change)
//   test_j29_golden FIXTURE --frames    compare, with one line per frame instead of one per burst (debugging)
//
// Every road name, distance and lane layout here is invented. The transcript holds no frame text: frames are
// summarised by connection, sessions, methods and a hash.
#include "cluster/nav_core.hpp"
#include "fake_kombi.hpp"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

using namespace nbt::kombinav;

static bool g_perFrame = false;

static std::string fmt(const char* f, ...) {
    char b[512];
    va_list ap;
    va_start(ap, f);
    vsnprintf(b, sizeof(b), f, ap);
    va_end(ap);
    return b;
}

static uint64_t fnv(uint64_t h, const Bytes& b) {
    for (size_t i = 0; i < b.size(); ++i) { h ^= b[i]; h *= 1099511628211ull; }
    return h;
}

// ---- the rig -------------------------------------------------------------------------------------------------------

struct Golden {
    std::vector<std::string>& out;
    FakeKombi k;
    Graph g;
    KombiClient c;
    Link link;
    J29Display d;
    Core core;
    size_t seen;
    bool lastShown;
    int lastStop;

    Golden(std::vector<std::string>& o, const char* name, const Settings& s)
        : out(o), g(0xC8, 255), c("127.0.0.1", k.port()), d(g, makeLink()), core(d, s, &logFn, this), seen(0),
          lastShown(false), lastStop(0) {
        out.push_back(fmt("== %s", name));
    }
    ~Golden() { out.push_back(""); }

    Link makeLink() {
        link.send = &sendFn;
        link.cancel = &cancelFn;
        link.user = this;
        return link;
    }
    static KombiClient::Result sendFn(void* u, const std::vector<Frame>& f, std::string* d) {
        return static_cast<Golden*>(u)->c.send(f, d);
    }
    static bool cancelFn(void* u, std::string* d) {
        Golden* t = static_cast<Golden*>(u);
        return t->c.cancel(t->g, 0.0, 0.2, NULL, NULL, d);
    }
    static void logFn(const char* line, void* u) {
        Golden* t = static_cast<Golden*>(u);
        t->frames();                      // what was sent before this line
        t->out.push_back(fmt("  log %s", line));
    }

    // The requests received since the last call, one line per run on one connection.
    void frames() {
        std::vector<Seen> s = k.seen();
        size_t i = seen;
        while (i < s.size()) {
            size_t j = i;
            uint64_t h = 1469598103934665603ull;
            std::string methods;
            uint16_t lastM = 0;
            int run = 0;
            while (j < s.size() && s[j].conn == s[i].conn) {
                if (g_perFrame)
                    out.push_back(fmt("  rx c%d s%02X%02X m%04X t%02X i%06X n%u %016llx", s[j].conn, s[j].header[10],
                                      s[j].header[11], s[j].method, modelType(s[j].body), modelIndex(s[j].body),
                                      (unsigned)s[j].body.size(), (unsigned long long)fnv(fnv(1469598103934665603ull,
                                      s[j].header), s[j].body)));
                h = fnv(fnv(h, s[j].header), s[j].body);
                if (run && s[j].method != lastM) { methods += fmt(" %X*%d", lastM, run); run = 0; }
                lastM = s[j].method;
                ++run;
                ++j;
            }
            methods += fmt(" %X*%d", lastM, run);
            if (!g_perFrame)
                out.push_back(fmt("  rx %u frame(s) on c%d, sessions %02X%02X..%02X%02X, methods%s, hash %016llx",
                                  (unsigned)(j - i), s[i].conn, s[i].header[10], s[i].header[11], s[j - 1].header[10],
                                  s[j - 1].header[11], methods.c_str(), (unsigned long long)h));
            i = j;
        }
        seen = s.size();
    }

    void after() {
        frames();
        if (core.shown() != lastShown || (int)core.stopped() != lastStop) {
            lastShown = core.shown();
            lastStop = (int)core.stopped();
            out.push_back(fmt("  state shown=%d stopped=%d", lastShown ? 1 : 0, lastStop));
        }
    }

    void step(double t, const NavView* v, const char* why = "guidance ended") {
        out.push_back(v ? fmt("t=%.2f %s", t, describe(*v).c_str()) : fmt("t=%.2f none (%s)", t, why));
        core.step(v, t, why);
        after();
    }
    void clear(const char* why) {
        out.push_back(fmt("clear (%s)", why));
        core.clear(why);
        after();
    }
    void yield() {
        out.push_back("yield to factory");
        core.yieldToFactory();
        after();
    }
    void resetStop() {
        out.push_back("reset stop");
        core.resetStop();
        after();
    }
    void rejectNext() {
        out.push_back("the KOMBI rejects the next request");
        k.rejectNth(k.count() + 1);
    }

    static std::string describe(const NavView& v) {
        std::string s = fmt("type %d road '%s' dist '%s' unit %d", v.aa_type, v.road.c_str(), v.dist_text.c_str(), v.aa_unit);
        if (v.has_step_m) s += fmt(" step %lld m", (long long)v.step_m);
        if (v.has_exit_angle) s += fmt(" angle %lld", (long long)v.exit_angle);
        if (!v.lanes.empty()) {
            s += " lanes";
            for (size_t i = 0; i < v.lanes.size(); ++i) {
                s += i ? "|" : " ";
                for (size_t k = 0; k < v.lanes[i].size(); ++k)
                    s += fmt("%s%d%s", k ? "+" : "", v.lanes[i][k].shape, v.lanes[i][k].highlighted ? "*" : "");
            }
        }
        return s;
    }
};

// ---- invented guidance ---------------------------------------------------------------------------------------------

static NavView view(int type, const char* road, const char* dist, int unit, long long step_m = -1, int angle = -1) {
    NavView v;
    v.aa_type = type;
    v.road = road;
    v.dist_text = dist;
    v.aa_unit = unit;
    if (step_m >= 0) { v.has_step_m = true; v.step_m = step_m; }
    if (angle >= 0) { v.has_exit_angle = true; v.exit_angle = angle; }
    return v;
}

// Lanes from a compact spec: lanes split by '|', directions by '+', '*' = highlighted. "4*|1|1+5" = left
// (highlighted), straight, straight or right.
static NavView withLanes(NavView v, const char* spec) {
    v.lanes.clear();
    AaLane cur;
    const char* p = spec;
    while (*p) {
        int shape = (int)strtol(p, (char**)&p, 10);
        bool hl = *p == '*';
        if (hl) ++p;
        cur.push_back(LaneDirection(shape, hl));
        if (*p == '+') { ++p; continue; }
        v.lanes.push_back(cur);
        cur.clear();
        if (*p == '|') ++p;
    }
    if (!cur.empty()) v.lanes.push_back(cur);
    return v;
}

static Settings plain(int inplace) {   // no approach hold, no distance rate limit
    Settings s;
    s.approach_mi = 0;
    s.min_update_s = 0;
    s.inplace = inplace;
    return s;
}

// ---- scenarios -----------------------------------------------------------------------------------------------------

static void fullGraphMode(std::vector<std::string>& o) {
    Golden g(o, "full graph for every maneuver (inplace 0)", plain(0));
    NavView a = view(7, "Testroad Alpha", "0.5", 5, 805);
    g.step(0, &a);
    a.dist_text = "0.4"; a.step_m = 644;
    g.step(1, &a);
    g.step(1.2, &a);                                     // nothing changed
    NavView b = view(8, "Sample Lane Beta", "300", 6, 91);
    g.step(2, &b);
    g.step(3, NULL);
    g.step(5, NULL);
    g.step(10.9, NULL);                                  // inside the grace time
    g.step(11, NULL);                                    // 8 s: the clear
    g.step(12, NULL);
}

static void defaultSettings(std::vector<std::string>& o) {
    Golden g(o, "default settings (approach 1 mi, in-place updates)", Settings());
    NavView a = view(7, "Testroad Alpha", "3.0", 5, 4828);
    g.step(0, &a);                                        // too far: held back
    a.dist_text = "1.0"; a.step_m = 1609;
    g.step(1, &a);
    a.dist_text = "0.9"; a.step_m = 1448;
    g.step(1.2, &a);                                      // under min_update_s
    g.step(1.6, &a);
    NavView b = view(8, "Sample Lane Beta", "0.8", 5, 1287);
    g.step(2, &b);                                        // in place
    b.dist_text = "1.1"; b.step_m = 1770;
    g.step(3, &b);                                        // further, but inside the hysteresis
    b.dist_text = "1.2"; b.step_m = 1900;
    g.step(4, &b);                                        // past it: hidden
    b.dist_text = "0.7"; b.step_m = 1126;
    g.step(5, &b);
    g.step(6, NULL);                                      // a gap (reroute)
    NavView c = view(35, "Demo Circle", "500", 6, 152, 90);
    g.step(7, &c);                                        // full graph after the gap
    c.dist_text = "400"; c.step_m = 122;
    g.step(8, &c);
    NavView d = view(42, "Example End", "200", 6, 61);
    g.step(9, &d);
    g.step(9.5, &d);
    NavView e = view(8, "Long Distance Road", "", 0, 2000);
    g.step(10, &e);                                       // no display value, metres known: approach check by metres
    g.step(11, NULL);
    g.step(19, NULL);
}

static void inplaceNoModel(std::vector<std::string>& o) {
    Golden g(o, "in-place without the guiding model (inplace 2)", plain(2));
    NavView a = view(9, "Fictional Street One", "250", 1, 250);
    g.step(0, &a);
    NavView b = view(10, "Fictional Street Two", "150", 1, 150);
    g.step(1, &b);
    g.clear("test end");
}

static void lanes(std::vector<std::string>& o) {
    Golden g(o, "lanes and position feed", plain(1));
    NavView a = withLanes(view(7, "Lane Test Road", "0.3", 5, 483), "4*|1|1|1+5");
    g.step(0, &a);                                       // graph with the lane row, then a position update
    a.dist_text = "0.2"; a.step_m = 322;
    g.step(0.5, &a);                                     // distance; the position feed waits for lane_feed_s
    a.step_m = 300;
    g.step(1.5, &a);                                     // outside the lane section: the same clamped position
    a.step_m = 150;
    g.step(2.0, &a);                                     // inside it: a position update
    a = withLanes(a, "4|1|1|1+5*");
    g.step(2.5, &a);                                     // lanes changed, but under lane_resend_s
    g.step(3.1, &a);                                     // resent
    g.step(4.5, &a);                                     // not moved: no position update
    a.step_m = 120;
    g.step(5.6, &a);
    a = withLanes(a, "4|0|1");
    g.step(6.5, &a);                                     // a shape with no KOMBI arrow: no lane row
    NavView b = withLanes(view(5, "Lane Test Exit", "0.1", 5, 161), "2*|2*|1|1");
    g.step(7, &b);
    NavView c = view(1, "Plain Road", "0.4", 5, 644);
    g.step(8, &c);                                        // no lanes after lanes
    g.step(9, NULL);
    g.step(17, NULL, "Android Auto session ended");
}

static void distances(std::vector<std::string>& o) {
    Golden g(o, "distance text and units", plain(1));
    NavView a = view(1, "Unit Test Road", "", 0);
    g.step(0, &a);                                        // no distance yet: wait
    g.step(2.9, &a);
    g.step(3.0, &a);                                      // shown as "-"
    a.dist_text = "250"; a.aa_unit = 1; a.has_step_m = true; a.step_m = 250;
    g.step(4, &a);
    NavView snap = view(1, "Unit Test Road", "", 0);
    g.step(5, &snap);                                     // a snapshot without its distance keeps the last one
    a.dist_text = "1.5"; a.aa_unit = 2; a.step_m = 1500;
    g.step(6, &a);
    a.dist_text = "12.5"; a.aa_unit = 3; a.step_m = 12500;
    g.step(7, &a);
    a.dist_text = "300"; a.aa_unit = 7; a.step_m = 274;
    g.step(8, &a);
    a.dist_text = "0.25"; a.aa_unit = 4; a.step_m = 402;
    g.step(9, &a);
    a.dist_text = "x"; a.aa_unit = 0; a.step_m = 1000;
    g.step(10, &a);                                       // unknown unit: worked out from the metres
    a.dist_text = "123456789"; a.aa_unit = 1;
    g.step(11, &a);                                       // longer than the KOMBI's field
    NavView b = view(2, "A Very Long Invented Road Name That Goes On And On Past Forty", "90", 1, 90);
    g.step(12, &b);
    NavView c = view(3, "Pr\xc3\xbc" "fweg \xc3\x84" "rger \xe2\x86\x92 Nirgendwo", "80", 1, 80);
    g.step(13, &c);                                       // UTF-8 (invented)
    NavView d = view(4, "", "70", 1, 70);
    g.step(14, &d);                                       // no road name
    g.clear("test end");
}

static void approachEdges(std::vector<std::string>& o) {
    Settings s;
    s.min_update_s = 0;
    Golden g(o, "approach hold edges", s);
    NavView a = view(7, "Approach Test Road", "", 0);
    g.step(0, &a);                                        // no distance at all: fail open after first_dist_s
    g.step(3.5, &a);
    NavView b = view(8, "Approach Test Two", "5", 2, 5000);
    g.step(4, &b);                                        // far: the shown arrow is cleared
    b.dist_text = "1.6"; b.step_m = 1600;
    g.step(5, &b);
    b.dist_text = "800"; b.aa_unit = 1; b.step_m = 800;
    g.step(6, &b);                                        // short distance in metres
    NavView c = view(42, "Approach Destination", "0.6", 5, 966);
    g.step(7, &c);
    g.clear("test end");
}

static void typeSweep(std::vector<std::string>& o) {
    Golden g(o, "every maneuver type, in place", plain(1));
    NavView a = view(1, "Sweep Start", "0.5", 5, 805);
    g.step(0, &a);
    for (int t = 0; t <= 60; ++t) {
        int angle = (t >= 30 && t <= 35) || (t >= 43 && t <= 46) ? 45 * (1 + t % 4) : -1;
        NavView v = view(t, "Sweep Road", "0.3", 5, 483, angle);
        g.step(1 + t, &v);
    }
    g.clear("test end");
}

static void rejections(std::vector<std::string>& o) {
    Golden g(o, "KOMBI rejections", plain(1));
    NavView a = view(7, "Reject Test Road", "0.5", 5, 805);
    g.step(0, &a);
    g.rejectNext();
    NavView b = view(8, "Reject Test Two", "0.4", 5, 644);
    g.step(1, &b);                                        // in-place rejected: full graph instead
    NavView c = view(7, "Reject Test Three", "0.3", 5, 483);
    g.step(2, &c);                                        // full graphs until the next clear
    g.rejectNext();
    c.dist_text = "0.2"; c.step_m = 322;
    g.step(3, &c);                                        // a rejected distance stops it
    c.dist_text = "0.1"; c.step_m = 161;
    g.step(4, &c);                                        // stopped: nothing
    g.resetStop();
    g.step(5, &c);
    g.clear("test end");
}

static void yieldAndRestart(std::vector<std::string>& o) {
    Golden g(o, "yield to an iDrive route", plain(1));
    NavView a = view(7, "Yield Test Road", "0.5", 5, 805);
    g.step(0, &a);
    g.yield();                                            // no cancel
    a.dist_text = "0.4"; a.step_m = 644;
    g.step(1, &a);                                        // a new route: full graph
    g.step(2, NULL);
    g.step(10, NULL, "guidance stale");
    g.clear("nothing shown");                             // nothing to cancel
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s FIXTURE [--update | --frames]\n", argv[0]);
        return 2;
    }
    bool update = argc > 2 && !strcmp(argv[2], "--update");
    g_perFrame = argc > 2 && !strcmp(argv[2], "--frames");

    std::vector<std::string> out;
    out.push_back("# test_j29_golden transcript: generated with --update, do not edit");
    out.push_back("");
    fullGraphMode(out);
    defaultSettings(out);
    inplaceNoModel(out);
    lanes(out);
    distances(out);
    approachEdges(out);
    typeSweep(out);
    rejections(out);
    yieldAndRestart(out);

    std::string got;
    for (size_t i = 0; i < out.size(); ++i) got += out[i] + "\n";
    if (update) {
        FILE* f = fopen(argv[1], "w");
        assert(f);
        fwrite(got.data(), 1, got.size(), f);
        fclose(f);
        printf("test_j29_golden: wrote %s (%u lines)\n", argv[1], (unsigned)out.size());
        return 0;
    }
    if (g_perFrame) {
        fwrite(got.data(), 1, got.size(), stdout);
        return 0;
    }
    std::string want;
    FILE* f = fopen(argv[1], "r");
    if (!f) {
        printf("test_j29_golden: no fixture %s (write one with --update)\n", argv[1]);
        return 1;
    }
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) want.append(buf, n);
    fclose(f);
    if (got == want) {
        printf("test_j29_golden: %u lines match\n", (unsigned)out.size());
        return 0;
    }
    std::vector<std::string> w;
    size_t p = 0;
    while (p < want.size()) {
        size_t e = want.find('\n', p);
        if (e == std::string::npos) e = want.size();
        w.push_back(want.substr(p, e - p));
        p = e + 1;
    }
    size_t i = 0;
    while (i < out.size() && i < w.size() && out[i] == w[i]) ++i;
    printf("test_j29_golden: MISMATCH at line %u\n", (unsigned)(i + 1));
    for (size_t k = i > 3 ? i - 3 : 0; k < i; ++k) printf("    %s\n", w[k].c_str());
    printf("  - %s\n", i < w.size() ? w[i].c_str() : "<end of fixture>");
    printf("  + %s\n", i < out.size() ? out[i].c_str() : "<end of output>");
    return 1;
}
