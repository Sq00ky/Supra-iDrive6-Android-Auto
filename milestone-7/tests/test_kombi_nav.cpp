// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test: kombi_nav state machine + SOME/IP client against a fake KOMBI on 127.0.0.1, the iDrive-route watch,
// and the daemon runtime end to end (src/kombi_nav.cpp, src/kombi_client.cpp).
// Build: g++ -std=gnu++98 -Iinclude -Isrc -Itests tests/test_kombi_nav.cpp src/kombi_nav.cpp src/kombi_client.cpp -lpthread
#include "kombi_nav.hpp"
#include "kombi_nav_test_util.hpp"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
using namespace nbt::kombinav;
using namespace kntest;

static double monoS() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

// ---- fake KOMBI --------------------------------------------------------------------------------------------------
struct Seen { uint16_t method; Bytes body; };

class FakeKombi {
public:
    enum Reject { kNone, kNth, kUntil };
    FakeKombi() : reject_(kNone), nth_(0), until_(0), cookie_ms_(0), stop_(false) {
        pthread_mutex_init(&mu_, NULL);
        srv_ = socket(AF_INET, SOCK_STREAM, 0);
        int one = 1;
        setsockopt(srv_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        struct sockaddr_in a;
        memset(&a, 0, sizeof(a));
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        assert(bind(srv_, (struct sockaddr*)&a, sizeof(a)) == 0 && listen(srv_, 8) == 0);
        socklen_t l = sizeof(a);
        getsockname(srv_, (struct sockaddr*)&a, &l);
        port_ = ntohs(a.sin_port);
        pthread_create(&acc_, NULL, &FakeKombi::acceptStub, this);
    }
    ~FakeKombi() {
        pthread_mutex_lock(&mu_);
        stop_ = true;
        pthread_mutex_unlock(&mu_);
        shutdown(srv_, SHUT_RDWR);
        close(srv_);
        pthread_join(acc_, NULL);
        dropAll();
        usleep(50000);
    }
    uint16_t port() const { return port_; }
    void rejectNth(size_t n) { pthread_mutex_lock(&mu_); reject_ = kNth; nth_ = n; pthread_mutex_unlock(&mu_); }
    void rejectUntil(double t) { pthread_mutex_lock(&mu_); reject_ = kUntil; until_ = t; pthread_mutex_unlock(&mu_); }
    void cookies(int ms) { pthread_mutex_lock(&mu_); cookie_ms_ = ms; pthread_mutex_unlock(&mu_); }
    bool stopping() { pthread_mutex_lock(&mu_); bool s = stop_; pthread_mutex_unlock(&mu_); return s; }
    std::vector<Seen> seen() { pthread_mutex_lock(&mu_); std::vector<Seen> s = seen_; pthread_mutex_unlock(&mu_); return s; }
    size_t count() { pthread_mutex_lock(&mu_); size_t n = seen_.size(); pthread_mutex_unlock(&mu_); return n; }
    void dropAll() {   // the KOMBI closes every connection (link blip)
        pthread_mutex_lock(&mu_);
        for (size_t i = 0; i < conns_.size(); ++i) shutdown(conns_[i], SHUT_RDWR);
        pthread_mutex_unlock(&mu_);
    }

private:
    static void* acceptStub(void* p) { static_cast<FakeKombi*>(p)->acceptLoop(); return NULL; }
    void acceptLoop() {
        while (!stopping()) {
            int c = accept(srv_, NULL, NULL);
            if (c < 0) return;
            pthread_mutex_lock(&mu_);
            conns_.push_back(c);
            int cookie_ms = cookie_ms_;
            pthread_mutex_unlock(&mu_);
            Conn* k = new Conn;
            k->self = this; k->fd = c;
            pthread_t t;
            pthread_create(&t, NULL, &FakeKombi::connStub, k);
            pthread_detach(t);
            if (cookie_ms) {
                Conn* k2 = new Conn;
                k2->self = this; k2->fd = c;
                pthread_create(&t, NULL, &FakeKombi::cookieStub, k2);
                pthread_detach(t);
            }
        }
    }
    struct Conn { FakeKombi* self; int fd; };
    static bool rd(int fd, uint8_t* p, size_t n) {
        while (n) { ssize_t r = recv(fd, p, n, 0); if (r <= 0) return false; p += r; n -= r; }
        return true;
    }
    static void* connStub(void* p) {
        Conn* k = static_cast<Conn*>(p);
        FakeKombi* s = k->self;
        int fd = k->fd;
        delete k;
        for (;;) {
            uint8_t h[16];
            if (!rd(fd, h, 16)) break;
            uint32_t len = ((uint32_t)h[4] << 24) | (h[5] << 16) | (h[6] << 8) | h[7];
            Bytes body(len - 8);
            if (len > 8 && !rd(fd, &body[0], body.size())) break;
            Seen x;
            x.method = (uint16_t)((h[2] << 8) | h[3]);
            x.body = body;
            pthread_mutex_lock(&s->mu_);
            s->seen_.push_back(x);
            size_t n = s->seen_.size();
            bool bad = (s->reject_ == kNth && n == s->nth_) || (s->reject_ == kUntil && monoS() < s->until_);
            pthread_mutex_unlock(&s->mu_);
            uint8_t r[16];
            memcpy(r, h, 16);
            r[4] = 0; r[5] = 0; r[6] = 0; r[7] = 8; r[14] = 0x80; r[15] = bad ? 1 : 0;
            if (send(fd, r, 16, MSG_NOSIGNAL) != 16) break;
        }
        close(fd);
        return NULL;
    }
    static void* cookieStub(void* p) {
        Conn* k = static_cast<Conn*>(p);
        FakeKombi* s = k->self;
        int fd = k->fd;
        delete k;
        const uint8_t cookie[16] = { 0xFF, 0xFF, 0x80, 0x00, 0, 0, 0, 8, 0xDE, 0xAD, 0xBE, 0xEF, 1, 1, 2, 0 };
        pthread_mutex_lock(&s->mu_);
        int ms = s->cookie_ms_;
        pthread_mutex_unlock(&s->mu_);
        while (!s->stopping()) {
            usleep(ms * 1000);
            if (send(fd, cookie, 16, MSG_NOSIGNAL) != 16) break;
        }
        return NULL;
    }

    int srv_;
    uint16_t port_;
    pthread_t acc_;
    pthread_mutex_t mu_;
    std::vector<Seen> seen_;
    std::vector<int> conns_;
    Reject reject_;               // guarded by mu_ (like everything below)
    size_t nth_;
    double until_;
    int cookie_ms_;
    bool stop_;
};

// ---- harness -------------------------------------------------------------------------------------------------------
struct TestLink {
    KombiClient& c;
    const Graph& g;
    Link link;
    TestLink(KombiClient& cl, const Graph& gr) : c(cl), g(gr) { link.send = &sendFn; link.cancel = &cancelFn; link.user = this; }
    static KombiClient::Result sendFn(void* u, const std::vector<Frame>& f, std::string* d) {
        return static_cast<TestLink*>(u)->c.send(f, d);
    }
    static bool cancelFn(void* u, std::string* d) {
        TestLink* t = static_cast<TestLink*>(u);
        return t->c.cancel(t->g, 0.0, 0.2, NULL, NULL, d);
    }
};

static std::vector<std::string> g_log;
static void logFn(const char* l, void*) { g_log.push_back(l); }
static int countLog(const char* prefix) {
    int n = 0;
    for (size_t i = 0; i < g_log.size(); ++i) if (!strncmp(g_log[i].c_str(), prefix, strlen(prefix))) ++n;
    return n;
}
static bool hasLog(const char* s) {
    for (size_t i = 0; i < g_log.size(); ++i) if (g_log[i] == s) return true;
    return false;
}

static Settings noApproach() { Settings s; s.approach_mi = 0; s.min_update_s = 0; return s; }

// One harness per test: graph, client, link, core.
struct Rig {
    FakeKombi k;
    Graph g;
    KombiClient c;
    TestLink l;
    Core core;
    explicit Rig(const Settings& s = noApproach())
        : g(0xC8, 255), c("127.0.0.1", k.port()), l(c, g), core(g, l.link, s, &logFn, NULL) {
        g_log.clear();
    }
    void feed(const NavMessages& m, double t) {
        NavView v;
        bool ok = m.guidanceActive() && viewFromMessages(m, v);
        core.step(ok ? &v : NULL, t);
    }
};

static std::string text0(const Bytes& b, int* unit) {   // ASCII text at the first BOM + the byte after its NUL
    for (size_t i = 18; i + 1 < b.size(); ++i) {
        if (b[i] != 0xFE || b[i + 1] != 0xFF) continue;
        size_t n = b[i - 1];
        std::string s;
        for (size_t k = i + 2; k < i + n - 2; k += 2) s += (char)b[k + 1];
        if (unit) *unit = i + n < b.size() ? b[i + n] : -1;
        return s;
    }
    return "<none>";
}

static NavMessages one(int type, const std::string& road, const Bytes& pos, int st = 1) {
    NavMessages m;
    Bytes s = status(st), ns = navState(type, road);
    m.store(0x8003, &s[0], s.size());
    m.store(0x8006, &ns[0], ns.size());
    if (!pos.empty()) m.store(0x8007, &pos[0], pos.size());
    return m;
}

static void waitCount(FakeKombi& k, size_t n, double timeout_s = 5.0) {
    double t0 = monoS();
    while (k.count() < n && monoS() - t0 < timeout_s) usleep(10000);
}

int main() {
    // demo drive: one full graph per maneuver, only whitelisted methods, ends with the factory cancel, no second one
    {
        Rig r;
        std::vector<Tick> drive = demoDrive(demoRoute());
        for (size_t i = 0; i < drive.size(); ++i) r.feed(drive[i].m, drive[i].t);
        r.core.step(NULL, drive.back().t + 100);
        assert(countLog("graph: 97 frame(s) OK") == 5);
        assert(countLog("graph:") == 5 && countLog("distance: 1 frame(s) OK") > 10);
        std::vector<Seen> s = r.k.seen();
        bool mi04 = false, ft = false;
        for (size_t i = 0; i < s.size(); ++i) {
            assert(methodAllowed(s[i].method));
            if (s[i].method == 0x000A && modelType(s[i].body) == 29 && s[i].body.size() > 40 && modelIndex(s[i].body) == 0) {
                int u;
                std::string t = text0(s[i].body, &u);
                if (t == "0.4" && u == 0x01) mi04 = true;
                if (u == 0x05) ft = true;
                assert(s[i].body[22] == 255);                      // red bar hidden
            }
        }
        assert(mi04 && ft);
        std::vector<Frame> c;
        r.g.cancel(c);
        for (size_t i = 0; i < 6; ++i) assert(s[s.size() - 6 + i].body == c[i].body);
        assert(!r.core.shown() && hasLog("CLEAR (guidance ended)"));
        size_t n = r.k.count();
        r.core.clear("exit");                                      // nothing shown any more: no second cancel
        assert(r.k.count() == n);
    }
    // stale handle / wrong car: rejected at the first frame -> stop + cancel
    {
        Rig r;
        r.k.rejectNth(1);
        std::vector<Tick> drive = demoDrive(demoRoute());
        r.feed(drive[1].m, 0.0);
        assert(r.core.stopped() == Core::kStoppedRejected && !r.core.shown());
        waitCount(r.k, 7);
        assert(r.k.count() == 1 + 6);
        r.feed(drive[2].m, 1.0);                                   // stopped: nothing more
        assert(r.k.count() == 7);
    }
    // rejected update after the graph -> stop + cancel
    {
        Rig r;
        r.k.rejectNth(98);
        r.feed(one(7, "TEST ST", curPos(800, "0.5", 5)), 0.0);
        r.feed(one(7, "TEST ST", curPos(790, "0.4", 5)), 5.0);
        assert(r.core.stopped() == Core::kStoppedRejected);
        assert(r.k.count() == 97 + 1 + 6);
    }
    // waits up to 3 s for a distance, then shows "-" (never the template's "600"); road capped at 40 characters
    {
        Rig r;
        NavMessages m = one(7, std::string(300, 'X'), Bytes());
        r.feed(m, 0.0);
        assert(r.k.count() == 0);
        r.feed(m, 3.5);
        std::vector<Seen> s = r.k.seen();
        assert(s.size() == 97);
        bool d = false, road = false;
        for (size_t i = 0; i < s.size(); ++i) {
            if (modelIndex(s[i].body) != 0) continue;
            if (s[i].method == 0x000A && modelType(s[i].body) == 29) { assert(text0(s[i].body, NULL) == "-"); d = true; }
            if (s[i].method == 0x0003 && modelType(s[i].body) == 3) { assert(text0(s[i].body, NULL) == std::string(40, 'X')); road = true; }
        }
        assert(d && road);
    }
    // a snapshot without a distance keeps the last one (no "-" flicker, nothing sent)
    {
        Rig r;
        r.feed(one(7, "TEST ST", curPos(800, "0.5", 5)), 0.0);
        size_t n = r.k.count();
        r.feed(one(7, "TEST ST", Bytes()), 2.0);
        assert(r.k.count() == n && r.core.shown());
    }
    // metric is shown as imperial: 250 m -> 0.2 mi
    {
        Rig r;
        r.feed(one(8, "RUE", curPos(250, "250", 1)), 0.0);
        std::vector<Seen> s = r.k.seen();
        bool found = false;
        for (size_t i = 0; i < s.size(); ++i)
            if (s[i].method == 0x000A && modelType(s[i].body) == 29 && modelIndex(s[i].body) == 0) {
                int u;
                assert(text0(s[i].body, &u) == "0.2" && u == 0x01);
                found = true;
            }
        assert(found);
    }
    // rerouting blip (status 3 for < 8 s) keeps the arrow; a real end clears it
    {
        Rig r;
        NavMessages on = one(7, "TEST ST", curPos(800, "0.5", 5)), off;
        Bytes s3 = status(3);
        off.store(0x8003, &s3[0], s3.size());
        r.feed(on, 0.0); r.feed(off, 1.0); r.feed(off, 5.0);
        assert(r.core.shown());
        r.feed(on, 6.0); r.feed(off, 7.0); r.feed(off, 16.0);
        assert(!r.core.shown() && hasLog("CLEAR (guidance ended)"));
    }
    // KOMBI magic cookies queued while idle are drained before the next request
    {
        Rig r;
        r.k.cookies(20);
        r.feed(one(7, "TEST ST", curPos(800, "0.5", 5)), 0.0);
        usleep(500000);                                             // ~25 cookies queue up
        r.feed(one(7, "TEST ST", curPos(700, "0.4", 5)), 5.0);
        assert(hasLog("distance: 1 frame(s) OK"));
    }
    // the KOMBI drops the connection while idle: one reconnect, the update still goes through
    {
        Rig r;
        r.feed(one(7, "TEST ST", curPos(800, "0.5", 5)), 0.0);
        r.k.dropAll();
        usleep(100000);
        r.feed(one(7, "TEST ST", curPos(700, "0.4", 5)), 5.0);
        assert(hasLog("distance: 1 frame(s) OK") && r.core.stopped() == Core::kRunning);
    }
    // the cancel retries through a blip
    {
        FakeKombi k;
        k.rejectUntil(monoS() + 0.3);
        Graph g;
        KombiClient c("127.0.0.1", k.port());
        assert(c.cancel(g, 3.0, 0.2));
        std::vector<Seen> s = k.seen();
        assert(s.size() > 6);
    }
    // nothing listening = a link failure, not a rejection
    {
        Graph g;
        KombiClient c("127.0.0.1", 9, 500);
        TestLink l(c, g);
        g_log.clear();
        Core core(g, l.link, noApproach(), &logFn, NULL);
        std::vector<Tick> drive = demoDrive(demoRoute());
        NavView v;
        assert(viewFromMessages(drive[1].m, v));
        core.step(&v, 0.0);
        assert(core.stopped() == Core::kStoppedLink && !core.shown());
    }
    // iDrive route: stop writing without a cancel, exit while yielded sends nothing, then a fresh full graph
    {
        Rig r;
        r.feed(one(7, "TEST ST", curPos(800, "0.5", 5)), 0.0);
        assert(r.k.count() == 97);
        r.core.yieldToFactory();
        assert(!r.core.shown() && r.k.count() == 97);
        r.core.clear("exit");
        assert(r.k.count() == 97);
        r.feed(one(7, "TEST ST", curPos(700, "0.4", 5)), 3.0);
        assert(r.k.count() == 97 + 97);
    }
    // approach: a far maneuver is hidden until within the limit; distance text <= 3 characters
    {
        Settings s;
        s.approach_mi = 2.0;
        s.min_update_s = 0;
        Rig r(s);
        RouteStep far[] = { {7, "FAR ST", 4500, -1}, {8, "NEAR ST", 400, -1}, {5, "NEXT AVE", 5200, -1} };
        std::vector<Tick> drive = demoDrive(std::vector<RouteStep>(far, far + 3), 100.0);
        for (size_t i = 0; i < drive.size(); ++i) r.feed(drive[i].m, drive[i].t);
        std::vector<std::string> shows;
        for (size_t i = 0; i < g_log.size(); ++i)
            if (!strncmp(g_log[i].c_str(), "SHOW ", 5)) shows.push_back(g_log[i]);
        assert(shows.size() == 3);
        assert(shows[0].find("'FAR ST'") != std::string::npos && shows[1].find("'NEAR ST'") != std::string::npos &&
               shows[2].find("'NEXT AVE'") != std::string::npos);
        assert(countLog("APPROACH") == 1 && hasLog("CLEAR (approach)"));
        std::vector<Seen> seen = r.k.seen();
        for (size_t i = 0; i < seen.size(); ++i)
            if (seen[i].method == 0x000A && modelType(seen[i].body) == 29 && modelIndex(seen[i].body) == 0)
                assert(text0(seen[i].body, NULL).size() <= 3);
        // the first frames sent: FAR ST already inside 2 mi
        assert(shows[0].find("| 1.9 mi") != std::string::npos || shows[0].find("| 2.0 mi") != std::string::npos);
    }
    // approach off: the far maneuver shows at once
    {
        Rig r;
        RouteStep far[] = { {7, "FAR ST", 4500, -1} };
        std::vector<Tick> drive = demoDrive(std::vector<RouteStep>(far, far + 1), 100.0);
        r.feed(drive[1].m, 0.0);
        assert(r.core.shown() && countLog("SHOW ") == 1 && countLog("APPROACH") == 0);
    }
    // hysteresis applies to the same maneuver only
    {
        Settings s;
        s.approach_mi = 2.0;
        s.min_update_s = 0;
        Rig r(s);
        int64_t m19 = (int64_t)(1.9 * 1609.344), m21 = (int64_t)(2.1 * 1609.344);
        r.feed(one(7, "X", curPos(m21, "2.1", 5)), 0.0);
        assert(!r.core.shown() && r.k.count() == 0);                  // 2.1 > 2.0, nothing shown yet: hold
        r.feed(one(7, "X", curPos(m19, "1.9", 5)), 1.0);
        assert(r.core.shown());
        r.feed(one(7, "X", curPos(m21, "2.1", 5)), 2.0);              // same maneuver drifting to 2.1 (< 2.3): kept
        assert(r.core.shown());
        r.feed(one(8, "Y", curPos(m21, "2.1", 5)), 3.0);              // a NEW maneuver at 2.1: hidden
        assert(!r.core.shown() && hasLog("CLEAR (approach)"));
    }
    // unknown distance fails open (no approach hold); shown with "-" after the 3 s wait
    {
        Settings s;
        s.approach_mi = 2.0;
        Rig r(s);
        r.feed(one(7, "X", Bytes()), 0.0);
        assert(r.k.count() == 0);
        r.feed(one(7, "X", Bytes()), 3.5);
        assert(r.core.shown() && r.k.count() == 97);
    }
    // iDrive-route watch: backlog ignored, live start, a disable of a route never shown keeps the yield
    {
        FactoryRouteWatch w(0.5);
        w.line("old MARKER RouteGuidanceStarted 20003 1000", 10.0);
        assert(!w.active());
        w.line("live MARKER RouteGuidanceStarted 20003 1000", 11.0);
        assert(w.active());
        w.line("MapViewer: API call --> disable route: routeId 2182", 12.0);
        assert(w.active());
    }
    // car sequence 2026-10-31: START, disable of the OLD preview route 2180, 2182 visible ... cancel = disable 2182
    {
        FactoryRouteWatch w(0.5);
        w.line("backlog", 0.0);
        w.line("pid 229432: MARKER RouteGuidanceStarted", 0.8);
        w.line("pid 299087: MARKER MapViewer: API call --> disable route: routeId 2180", 0.8);
        w.line("pid 299087: MARKER NaviRouteVisibleInMap sourceHandle = 2182", 0.8);
        assert(w.active());
        w.line("pid 299087: MARKER NaviRouteVisibleInMap sourceHandle = 2182", 2.3);
        w.line("pid 229432: MARKER attach EMPTY on guidance viewer:GUIDING LABELS for man id:0", 2.3);
        w.line("pid 299087: MARKER MapViewer: API call --> disable route: routeId 21820", 2.3);   // not 2182
        assert(w.active());
        w.line("pid 299087: MARKER MapViewer: API call --> disable route: routeId 2182", 2.3);
        assert(!w.active());
        w.line("pid 1: MARKER RouteGuidanceStarted", 3.0);
        assert(w.active());
        w.release();                                                  // a new Android Auto route takes over
        assert(!w.active());
    }
    // the daemon runtime end to end: menu flag on, messages in, session end and unticking both clear, exit clears
    {
        FakeKombi k;
        char port[16], dir[] = "/tmp/kombi_nav_testXXXXXX";
        assert(mkdtemp(dir));
        std::string flag = std::string(dir) + "/kombi-nav", manual = std::string(dir) + "/manual",
                    shown = std::string(dir) + "/shown";
        KombiNav::kFlagFile = flag.c_str();
        KombiNav::kManualFile = manual.c_str();
        KombiNav::kShownFile = shown.c_str();
        snprintf(port, sizeof(port), "%u", (unsigned)k.port());
        unsetenv("NBT_KOMBI_NAV");
        setenv("NBT_KOMBI_NAV_HOST", "127.0.0.1", 1);
        setenv("NBT_KOMBI_NAV_PORT", port, 1);
        setenv("NBT_KOMBI_NAV_YIELD", "0", 1);
        setenv("NBT_KOMBI_NAV_APPROACH_MI", "0", 1);
        assert(!KombiNav::settingOn());                                // off by default
        FILE* f = fopen(flag.c_str(), "w");                            // the iDrive checkbox
        assert(f);
        fclose(f);
        assert(KombiNav::settingOn());
        {
            KombiNav nav;
            nav.start();
            Bytes s1 = status(1), st = navState(7, "TEST ST"), cp = curPos(800, "0.5", 5);
            KombiNav::navStatusCallback(0x8003, &s1[0], s1.size(), &nav);
            KombiNav::navStatusCallback(0x8006, &st[0], st.size(), &nav);
            KombiNav::navStatusCallback(0x8007, &cp[0], cp.size(), &nav);
            KombiNav::navStatusCallback(0x8001, &s1[0], s1.size(), &nav);    // a message kombi_nav doesn't use
            waitCount(k, 97);
            assert(k.count() == 97 && access(shown.c_str(), F_OK) == 0);   // marker while an arrow may be up
            Bytes cp2 = curPos(700, "0.4", 5);
            KombiNav::navStatusCallback(0x8007, &cp2[0], cp2.size(), &nav);
            waitCount(k, 98);
            assert(k.count() == 98);
            nav.onSessionEnded();                                          // phone gone: cleared at once
            waitCount(k, 104);
            usleep(400000);
            assert(k.count() == 104 && access(shown.c_str(), F_OK) != 0);
            KombiNav::navStatusCallback(0x8003, &s1[0], s1.size(), &nav);  // next session, new route
            KombiNav::navStatusCallback(0x8006, &st[0], st.size(), &nav);
            KombiNav::navStatusCallback(0x8007, &cp[0], cp.size(), &nav);
            waitCount(k, 104 + 97);
            assert(k.count() == 104 + 97);
            unlink(flag.c_str());                                          // unticked: cleared within ~1 s
            waitCount(k, 104 + 97 + 6, 3.0);
            assert(k.count() == 104 + 97 + 6);
            nav.stop();                                                    // nothing up: no cancel at exit
            assert(k.count() == 104 + 97 + 6);
        }
        size_t base = 104 + 97 + 6;
        f = fopen(manual.c_str(), "w");                                    // the hand-made flag counts too
        assert(f);
        fclose(f);
        {
            KombiNav nav2;                                                 // exit with an arrow up: cancelled
            nav2.start();
            Bytes s1 = status(1), st = navState(8, "MAIN ST"), cp = curPos(300, "0.2", 5);
            KombiNav::navStatusCallback(0x8003, &s1[0], s1.size(), &nav2);
            KombiNav::navStatusCallback(0x8006, &st[0], st.size(), &nav2);
            KombiNav::navStatusCallback(0x8007, &cp[0], cp.size(), &nav2);
            waitCount(k, base + 97);
            nav2.stop();
            assert(k.count() == base + 97 + 6 && access(shown.c_str(), F_OK) != 0);
        }
        base += 97 + 6;
        f = fopen(shown.c_str(), "w");                                     // marker left by a killed run
        assert(f);
        fclose(f);
        unlink(manual.c_str());
        {
            KombiNav nav3;                                                 // start: one cancel, marker removed
            nav3.start();
            waitCount(k, base + 6);
            nav3.stop();
            assert(k.count() == base + 6 && access(shown.c_str(), F_OK) != 0);
        }
        std::vector<Seen> s = k.seen();
        for (size_t i = 0; i < s.size(); ++i) assert(methodAllowed(s[i].method));
        rmdir(dir);
    }
    // iDrive-route yield through a real child process: a fake `sloginfo -w` on PATH logs a route start after its
    // backlog; kombi_nav stops writing WITHOUT a cancel, a new Android Auto route takes over again
    {
        FakeKombi k;
        char port[16], dir[] = "/tmp/kombi_nav_slogXXXXXX";
        assert(mkdtemp(dir));
        std::string flag = std::string(dir) + "/kombi-nav", shown = std::string(dir) + "/shown",
                    slog = std::string(dir) + "/sloginfo", path = std::string(dir) + ":" + getenv("PATH");
        FILE* f = fopen(slog.c_str(), "w");
        assert(f);
        fprintf(f, "#!/bin/sh\n[ \"$1\" = -w ] || exit 3\necho 'old backlog RouteGuidanceStarted'\nsleep 4\n"
                   "echo 'pid 1: MARKER RouteGuidanceStarted'\n"
                   "echo 'pid 2: MARKER NaviRouteVisibleInMap sourceHandle = 2182'\nexec sleep 60\n");
        fclose(f);
        chmod(slog.c_str(), 0755);
        f = fopen(flag.c_str(), "w");
        fclose(f);
        KombiNav::kFlagFile = flag.c_str();
        KombiNav::kManualFile = "/nonexistent/kombi-nav";
        KombiNav::kShownFile = shown.c_str();
        snprintf(port, sizeof(port), "%u", (unsigned)k.port());
        setenv("NBT_KOMBI_NAV_PORT", port, 1);
        setenv("NBT_KOMBI_NAV_YIELD", "1", 1);
        std::string oldpath = getenv("PATH");
        setenv("PATH", path.c_str(), 1);
        {
            KombiNav nav;
            nav.start();
            Bytes s1 = status(1), s2 = status(2), st = navState(7, "TEST ST"), cp = curPos(800, "0.5", 5);
            KombiNav::navStatusCallback(0x8003, &s1[0], s1.size(), &nav);
            KombiNav::navStatusCallback(0x8006, &st[0], st.size(), &nav);
            KombiNav::navStatusCallback(0x8007, &cp[0], cp.size(), &nav);
            waitCount(k, 97);
            assert(k.count() == 97);
            usleep(5000000);                                               // the route start is logged at ~4 s
            Bytes cp2 = curPos(700, "0.4", 5);
            KombiNav::navStatusCallback(0x8007, &cp2[0], cp2.size(), &nav);
            usleep(800000);
            assert(k.count() == 97);                                       // yielded: no update, no cancel
            KombiNav::navStatusCallback(0x8003, &s2[0], s2.size(), &nav);  // AA route ends ...
            usleep(600000);
            KombiNav::navStatusCallback(0x8003, &s1[0], s1.size(), &nav);  // ... and a new one starts: released
            waitCount(k, 97 + 97);
            assert(k.count() == 97 + 97);
            nav.stop();                                                    // exit clears, sloginfo is stopped
            assert(k.count() == 97 + 97 + 6);
        }
        setenv("PATH", oldpath.c_str(), 1);
        unlink(slog.c_str());
        unlink(flag.c_str());
        unlink(shown.c_str());
        rmdir(dir);
    }
    printf("test_kombi_nav OK\n");
    return 0;
}
