// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test: the HUDTerminal transport (cluster/hud_terminal.hpp) against a scripted cluster. Core: request bytes,
// refusals, batches sent back to back and answered, copies ignored, the factory navigation taking over mid-batch and
// finishing, timeouts and error answers. HudTerminal: its send thread with a fake sender, and its wiring to a
// most::Client over the fake DsiClient. Addresses are invented: host 0123, us 2077, the factory navigation 2033.
#include "cluster/hud_terminal.hpp"
#include "fake_dsi_client.hpp"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

using namespace nbt;
using namespace nbt::hudterm;

static const uint16_t kHost = 0x0123, kSelf = 0x2077, kNav = 0x2033;
static const int kFrame = 1;

static Config config() {
    Config c;
    for (int h = 1; h <= 10; ++h) c.handles.allow(h);
    c.handles.allow(22);
    c.navFrame = kFrame;
    return c;
}

// A received message whose payload outlives the View.
struct Wire {
    std::vector<uint8_t> data;
    most::View v;
    Wire(uint16_t src, uint16_t dst, uint16_t fkt, uint8_t op, const std::vector<uint8_t>& p, uint8_t fblock = kFBlock)
        : data(p) {
        memset(&v, 0, sizeof(v));
        v.varTag = 2;
        v.src = src; v.dst = dst; v.fblock = fblock; v.inst = kInst; v.fkt = fkt; v.op = op;
        v.data = data.empty() ? NULL : &data[0];
        v.len = data.size();
    }
};

// The cluster's answer to a call, and the copy it sends to the navigation. A text's answer leaves out the 00 00.
static std::vector<uint8_t> answerPayload(const most::Msg& m) {
    std::vector<uint8_t> p = m.data;
    if (m.fkt == kTextareaSetText && p.size() >= 2) p.resize(p.size() - 2);
    return p;
}
static Wire answer(const most::Msg& m, uint8_t op = most::kOpResultAck) {
    return Wire(kHost, kSelf, m.fkt, op, answerPayload(m));
}
static Wire copyOf(const most::Msg& m) { return Wire(kHost, kNav, m.fkt, most::kOpResultAck, answerPayload(m)); }
static Wire navCall(uint16_t fkt, const std::vector<uint8_t>& p) { return Wire(kNav, kHost, fkt, most::kOpStartResultAck, p); }
static Wire navAnswer(uint16_t fkt, const std::vector<uint8_t>& p) { return Wire(kHost, kNav, fkt, most::kOpResultAck, p); }
// The cluster's answer to a navigation text: like ours, without the 00 00.
static Wire navTextAnswer(uint16_t h, const char* t) {
    std::vector<uint8_t> p = setText(h, t);
    p.resize(p.size() - 2);
    return navAnswer(kTextareaSetText, p);
}

static Core readyCore(const Config& c = config()) {
    Core k(c);
    k.setLink(true, kSelf, 0);
    k.setHost(kHost, 0);
    return k;
}

static Batch batchOf(int n) {
    Batch b("test");
    for (int i = 0; i < n; ++i) b.add(kSetFillColor, setFillColor((uint16_t)(5 + (i % 2)), (uint8_t)i));
    return b;
}

// Sends everything next() offers, answering each one with its copy. Returns how many were sent.
static int pumpAnswered(Core& k, uint32_t now) {
    int n = 0;
    most::Msg m;
    while (k.next(now, m)) {
        Wire a = answer(m), c = copyOf(m);
        k.onMessage(a.v, now);
        k.onMessage(c.v, now);
        ++n;
    }
    return n;
}

// ---- Core ----------------------------------------------------------------------------------------------------------

static void testStatusAndOffline() {
    Core k(config());
    Status s;
    assert(k.status() == kOffline && !k.takeStatusChange(s));
    assert(k.submit(batchOf(1), 0) == kNotReady);
    k.setHost(kHost, 0);
    assert(k.status() == kOffline);                     // no link yet
    k.setLink(true, 0x0105, 0);                         // a device address, not an app's: never a sender
    assert(k.status() == kOffline);
    k.setLink(true, kSelf, 0);
    assert(k.status() == kReady && k.takeStatusChange(s) && s == kReady && !k.takeStatusChange(s));
    k.setHost(0x2020, 0);                               // not a ring node: no host
    assert(k.status() == kOffline);
    k.setHost(kHost, 0);
    assert(k.status() == kReady);
    k.setLink(false, 0, 0);
    assert(k.status() == kOffline && k.takeStatusChange(s) && s == kOffline);
    most::Msg m;
    assert(!k.next(0, m));
}

static void testRequestBytes() {
    Core k = readyCore();
    Batch b("show");
    b.add(kSetVisibility, setVisibility(kFrame, true));
    b.add(kTextareaSetText, setText(3, "250"));
    assert(k.submit(b, 10) == kQueued && k.busy());
    most::Msg m1, m2, m3;
    assert(k.next(10, m1) && k.next(11, m2) && !k.next(12, m3));
    assert(m1.dst == kHost && m1.fblock == 0x82 && m1.inst == 0 && m1.fkt == kSetVisibility);
    assert(m1.opType == most::kOpStartResultAck && m1.data == setVisibility(kFrame, true));
    assert(m2.fkt == kTextareaSetText && m2.data == setText(3, "250"));
    assert(m2.hdrTag == m1.hdrTag + 1);
    TerminalStats st = k.stats();
    assert(st.calls == 2 && st.batches == 1);
}

static void testRefusals() {
    Core k = readyCore();
    Batch b("bad");
    assert(k.submit(b, 0) == kRefused);                                       // empty
    b.add(kSetVisibility, setVisibility(2, true));
    b.add(kSetVisibility, setVisibility(11, true));                           // handle 11 not in the profile
    assert(k.submit(b, 0) == kRefused);
    Batch sw("swap");
    sw.add(kSwitchVisibility, switchVisibility(5, 30));
    assert(k.submit(sw, 0) == kRefused);
    Batch iv("isvisible");
    iv.add(kIsVisible, removeAllItems(1));
    assert(k.submit(iv, 0) == kRefused);
    Batch shape("shape");
    std::vector<uint8_t> t = setText(3, "1");
    t.resize(t.size() - 2);
    shape.add(kTextareaSetText, t);
    assert(k.submit(shape, 0) == kRefused);
    assert(k.submit(batchOf(33), 0) == kRefused);                             // over maxBatch
    most::Msg m;
    assert(!k.next(0, m) && !k.busy() && k.stats().refused == 6 && k.stats().calls == 0);
    assert(k.status() == kReady);                                             // a refusal is not a failure
}

static void testBackToBackAndWindow() {
    Core k = readyCore();
    assert(k.submit(batchOf(5), 0) == kQueued);
    most::Msg m;
    int sent = 0;
    while (k.next(0, m)) ++sent;
    assert(sent == 5);                                  // all of it before any answer

    Config c = config();
    c.maxInFlight = 2;
    Core w = readyCore(c);
    assert(w.submit(batchOf(5), 0) == kQueued);
    most::Msg a, b, x;
    assert(w.next(0, a) && w.next(0, b) && !w.next(0, x));
    Wire ra = answer(a);
    w.onMessage(ra.v, 5);
    assert(w.next(5, x) && !w.next(5, x));
}

static void testAnswersAndQueue() {
    Core k = readyCore();
    Batch b("polys");
    std::vector<Point> pts(4);
    b.add(kAddPolygon, addPolygon(5, 0, kPolygon, pts));
    b.add(kAddPolygon, addPolygon(5, 1, kPolygon, pts));
    b.add(kTextareaSetText, setText(3, "1.2"));
    assert(k.submit(b, 0) == kQueued);
    most::Msg p0, p1, t;
    assert(k.next(0, p0) && k.next(0, p1) && k.next(0, t));

    assert(k.submit(batchOf(1), 1) == kQueued);         // waits for this batch
    Batch full = batchOf(2);
    full.full = true;
    assert(k.submit(full, 1) == kReplaced);             // a full batch: the newest state wins
    most::Msg x;
    assert(!k.next(1, x));

    Wire busy = answer(t, most::kOpProcessingAck);
    k.onMessage(busy.v, 2);                             // not an answer yet
    Wire at = answer(t);
    k.onMessage(at.v, 3);                               // out of order: matched by function and handle
    Wire stray = Wire(kHost, kSelf, kSetVisibility, most::kOpResultAck, setVisibility(9, true));
    k.onMessage(stray.v, 3);
    Wire a0 = answer(p0), a1 = answer(p1);
    k.onMessage(a0.v, 4);
    assert(!k.next(4, x));                              // one polygon still in flight
    k.onMessage(a1.v, 5);
    TerminalStats st = k.stats();
    assert(st.answers == 3 && st.strays == 1 && st.batchesDone == 1 && st.replaced == 1);

    int n = 0;                                          // the queued batch: the replacing one, two calls
    while (k.next(6, x)) ++n;
    assert(n == 2 && k.busy());
    assert(k.status() == kReady);
}

// A partial batch never wipes out a full one that is waiting: texts replace in place, the rest is appended.
static void testMerge() {
    Config c = config();
    c.maxBatch = 5;
    Core k = readyCore(c);
    assert(k.submit(batchOf(1), 0) == kQueued);         // in progress
    Batch show("maneuver", true);
    std::vector<Point> pts(3);
    show.add(kAddPolygon, addPolygon(5, 0, kPolygon, pts));
    show.add(kTextareaSetText, setText(3, "300"));
    show.add(kTextareaSetText, setText(4, "m"));
    assert(k.submit(show, 0) == kQueued);               // waiting
    Batch d1("distance");
    d1.add(kTextareaSetText, setText(3, "250"));
    assert(k.submit(d1, 1) == kMerged);
    Batch d2("distance");
    d2.add(kTextareaSetText, setText(3, "200"));
    d2.add(kTextareaSetText, setText(4, "ft"));
    assert(k.submit(d2, 2) == kMerged);
    Batch vis("frame");
    vis.add(kSetVisibility, setVisibility(kFrame, true));
    assert(k.submit(vis, 3) == kMerged);                // appended: 4 calls
    Batch two("two");
    two.add(kSetVisibility, setVisibility(2, true));
    two.add(kSetVisibility, setVisibility(3, true));
    assert(k.submit(two, 4) == kRefused);               // 6 > maxBatch: the waiting batch stays as it was
    assert(k.stats().merged == 3 && k.stats().refused == 1);

    most::Msg cur;                                      // the batch in progress, answered
    assert(k.next(5, cur));
    Wire a = answer(cur);
    k.onMessage(a.v, 5);
    most::Msg m[4], x;
    for (int i = 0; i < 4; ++i) assert(k.next(6, m[i]));
    assert(!k.next(6, x));
    assert(m[0].fkt == kAddPolygon);
    assert(m[1].fkt == kTextareaSetText && decodeText(&m[1].data[0], m[1].data.size()) == "200");
    assert(m[2].fkt == kTextareaSetText && decodeText(&m[2].data[0], m[2].data.size()) == "ft");
    assert(m[3].fkt == kSetVisibility);

    // Nothing waiting: a partial batch just waits behind the one in progress.
    Batch d3("distance");
    d3.add(kTextareaSetText, setText(3, "150"));
    assert(k.submit(d3, 7) == kQueued);
}

// The epoch moves whenever submitted work may be lost: dropped work, or the status leaving ready.
static void testEpoch() {
    Core k = readyCore();
    uint32_t e0 = k.epoch();
    assert(k.submit(batchOf(2), 0) == kQueued);
    assert(k.epoch() == e0);
    Wire nav = navCall(kSetVisibility, setVisibility(kFrame, true));
    k.onMessage(nav.v, 1);                              // the navigation: our batch is dropped
    assert(k.status() == kNavActive && k.epoch() != e0);
    uint32_t e1 = k.epoch();
    Wire off = navCall(kSetVisibility, setVisibility(kFrame, false));
    k.onMessage(off.v, 2);
    k.tick(2 + 5000);
    assert(k.status() == kReady && k.epoch() == e1);    // coming back changes nothing more
    k.setLink(false, 0, 5100);                          // nothing queued, but what we drew may be gone
    assert(k.epoch() != e1);
}

// The profile check: the navigation's text or swap on instances the profile does not list stops the terminal for good.
static void testProfileMismatch() {
    Config c = config();
    c.checkNav = true;
    for (int h = 2; h <= 4; ++h) c.navTexts.allow(h);
    c.navBuffers.allow(5);
    c.navBuffers.allow(6);
    {
        Core k = readyCore(c);
        Wire t = navCall(kTextareaSetText, setText(3, "400"));
        Wire s = navCall(kSwitchVisibility, switchVisibility(6, 5));
        Wire other = navCall(kRemoveAllItems, removeAllItems(9));
        Wire ans = navAnswer(kTextareaSetText, setText(9, ""));   // the cluster's answer: only calls are checked
        k.onMessage(t.v, 0);
        k.onMessage(s.v, 1);
        k.onMessage(other.v, 2);
        k.onMessage(ans.v, 3);
        assert(k.status() == kNavActive && !k.mismatch());
    }
    {
        Core k = readyCore(c);
        Wire t = navCall(kTextareaSetText, setText(9, "x"));
        k.onMessage(t.v, 0);
        assert(k.status() == kFailed && k.mismatch());
        k.reset(1);
        assert(k.status() == kFailed);
        k.setLink(false, 0, 2);
        k.setLink(true, kSelf, 3);
        assert(k.status() == kFailed);
        k.setHost(0x0124, 4);
        assert(k.status() == kFailed && k.submit(batchOf(1), 5) == kNotReady);
    }
    {
        Core k = readyCore(c);
        Wire s = navCall(kSwitchVisibility, switchVisibility(6, 7));
        k.onMessage(s.v, 0);
        assert(k.status() == kFailed && k.mismatch());
    }
    {
        Core k = readyCore();                           // the check off: anything goes
        Wire t = navCall(kTextareaSetText, setText(9, "x"));
        k.onMessage(t.v, 0);
        assert(k.status() == kNavActive && !k.mismatch());
    }
}

static void testCopiesIgnored() {
    Core k = readyCore();
    Batch b("text");
    b.add(kTextareaSetText, setText(3, "40"));
    b.add(kRemoveAllItems, removeAllItems(6));
    assert(k.submit(b, 100) == kQueued);
    most::Msg t, r;
    assert(k.next(100, t) && k.next(100, r));
    Wire ct = copyOf(t), cr = copyOf(r);
    k.onMessage(ct.v, 100);                             // the copy of a text answer has no terminator
    k.onMessage(cr.v, 101);
    assert(k.status() == kReady && k.stats().copies == 2 && k.stats().navMsgs == 0);
    Wire at = answer(t), ar = answer(r);
    k.onMessage(at.v, 102);
    k.onMessage(ar.v, 102);
    assert(k.stats().batchesDone == 1);

    Wire late = copyOf(r);
    k.onMessage(late.v, 100 + 1501);                    // past the window: that is the navigation
    assert(k.status() == kNavActive && k.stats().navMsgs == 1);
}

static void testOwnAndForeignIgnored() {
    Core k = readyCore();
    Wire own(kSelf, kHost, kSetVisibility, most::kOpStartResultAck, setVisibility(kFrame, true));
    Wire other(kHost, kNav, kSetVisibility, most::kOpResultAck, setVisibility(kFrame, true), 0x52);
    Wire elsewhere(0x0140, kNav, kSetVisibility, most::kOpResultAck, setVisibility(kFrame, true));
    k.onMessage(own.v, 0);
    k.onMessage(other.v, 0);
    k.onMessage(elsewhere.v, 0);
    assert(k.status() == kReady && k.stats().navMsgs == 0);
}

static void testNavTakeoverMidBatch() {
    Config c = config();
    c.maxInFlight = 3;
    Core k = readyCore(c);
    assert(k.submit(batchOf(6), 0) == kQueued);
    assert(k.submit(batchOf(2), 0) == kQueued);
    most::Msg m[3], x;
    assert(k.next(0, m[0]) && k.next(0, m[1]) && k.next(0, m[2]) && !k.next(0, x));
    Wire a0 = answer(m[0]);
    k.onMessage(a0.v, 1);

    Wire nav = navCall(kSetVisibility, setVisibility(kFrame, true));
    k.onMessage(nav.v, 2);
    Status s;
    assert(k.status() == kNavActive && k.takeStatusChange(s) && s == kNavActive);
    assert(!k.next(2, x) && !k.busy() && k.stats().batchesDropped == 2);
    assert(k.submit(batchOf(1), 3) == kNotReady);

    Wire a1 = answer(m[1]), a2 = answer(m[2]);
    k.onMessage(a1.v, 4);                               // answers to the dropped calls: strays, nothing more
    k.onMessage(a2.v, 4);
    assert(k.stats().strays == 2 && k.status() == kNavActive);
    k.tick(100000);                                     // quiet, but the frame is on: still the navigation's
    assert(k.status() == kNavActive);
}

static void testNavFinishes() {
    Core k = readyCore();
    Wire start = navAnswer(kSetVisibility, setVisibility(kFrame, true));
    k.onMessage(start.v, 1000);
    assert(k.status() == kNavActive);
    k.tick(60000);
    assert(k.status() == kNavActive);                   // a parked route is quiet too
    assert(k.msUntilDeadline(60000) == 0xFFFFFFFFu);

    Wire off = navCall(kSetVisibility, setVisibility(kFrame, false));
    k.onMessage(off.v, 61000);
    assert(k.msUntilDeadline(61000) == 5000);
    Wire clear = navCall(kRemoveAllItems, removeAllItems(5));
    k.onMessage(clear.v, 64000);                         // the navigation clearing up restarts the quiet time
    k.tick(68000);
    assert(k.status() == kNavActive && k.msUntilDeadline(68000) == 1000);
    Wire otherOff = navCall(kSetVisibility, setVisibility(22, false));
    k.onMessage(otherOff.v, 68500);                      // another instance does not change the frame state
    k.tick(73499);
    assert(k.status() == kNavActive);
    k.tick(73500);
    Status s;
    assert(k.status() == kReady && k.takeStatusChange(s) && s == kReady);

    Wire again = navCall(kSetVisibility, setVisibility(kFrame, false));
    k.onMessage(again.v, 80000);                          // a frame-off on its own (e.g. at start-up) ends after quiet
    assert(k.status() == kNavActive);
    Wire on = navCall(kSetVisibility, setVisibility(kFrame, true));
    k.onMessage(on.v, 81000);                             // then on again: a route
    k.tick(200000);
    assert(k.status() == kNavActive);
}

// Navigation traffic that never draws (a set-up, a status, clearing up) yields only until the terminal is quiet; a
// drawing seen without its frame-on (a route already running at start-up) holds until the frame goes off.
static void testNavWithoutRoute() {
    Core k = readyCore();
    Wire setup = navAnswer(kRemoveAllItems, removeAllItems(7));
    k.onMessage(setup.v, 1000);
    assert(k.status() == kNavActive && k.msUntilDeadline(1000) == 5000);
    Wire empty = navTextAnswer(3, "");
    k.onMessage(empty.v, 2000);                               // an empty text is not drawing
    Wire other = navAnswer(kSetVisibility, setVisibility(22, true));
    k.onMessage(other.v, 2500);                               // nor another instance switched on
    k.tick(7499);
    assert(k.status() == kNavActive);
    k.tick(7500);
    Status s;
    assert(k.status() == kReady && k.takeStatusChange(s) && s == kReady);

    Wire dist = navTextAnswer(3, "400");
    k.onMessage(dist.v, 10000);                               // a route's text: on screen, frame-on never seen
    k.tick(200000);
    assert(k.status() == kNavActive && k.msUntilDeadline(200000) == 0xFFFFFFFFu);
    Wire off = navAnswer(kSetVisibility, setVisibility(kFrame, false));
    k.onMessage(off.v, 201000);
    Wire clear5 = navAnswer(kRemoveAllItems, removeAllItems(5));
    k.onMessage(clear5.v, 204000);                            // the navigation clearing up: quiet starts again
    k.onMessage(empty.v, 204000);
    k.tick(208999);
    assert(k.status() == kNavActive);
    k.tick(209000);
    assert(k.status() == kReady);

    Wire poly = navAnswer(kAddPolygon, addPolygon(6, 0, kPolygon, std::vector<Point>(3)));
    k.onMessage(poly.v, 300000);                              // drawing alone is a route too
    k.tick(400000);
    assert(k.status() == kNavActive);
}

// An error answer to a call the terminal dropped when the navigation took over is not a failure, within replyMs.
static void testLateErrorAfterTakeover() {
    std::vector<uint8_t> err;
    putBe16(err, 5);
    err.push_back(0x07);                                      // invented error code
    for (int late = 0; late <= 1; ++late) {
        Core k = readyCore();
        assert(k.submit(batchOf(2), 0) == kQueued);
        most::Msg m0, m1;
        assert(k.next(0, m0) && k.next(0, m1));
        Wire nav = navAnswer(kSetVisibility, setVisibility(kFrame, true));
        k.onMessage(nav.v, 10);
        assert(k.status() == kNavActive && k.stats().batchesDropped == 1);
        Wire e(kHost, kSelf, m0.fkt, most::kOpErrorAck, err);
        k.onMessage(e.v, late ? 10 + 1500 : 300);
        if (!late) {
            assert(k.status() == kNavActive && k.stats().errors == 0 && k.stats().lateErrors == 1);
            k.onMessage(e.v, 310);                            // the same answer again: matches nothing any more
            assert(k.status() == kFailed && k.stats().errors == 1);
        } else {
            assert(k.status() == kFailed && k.stats().errors == 1 && k.stats().lateErrors == 0);
        }
    }

    Core k = readyCore();                                     // a success to a dropped call: a stray, as before
    assert(k.submit(batchOf(1), 0) == kQueued);
    most::Msg m;
    assert(k.next(0, m));
    Wire nav = navAnswer(kSetVisibility, setVisibility(kFrame, true));
    k.onMessage(nav.v, 10);
    Wire a = answer(m);
    k.onMessage(a.v, 20);
    assert(k.stats().strays == 1 && k.status() == kNavActive);
    Wire off = navAnswer(kSetVisibility, setVisibility(kFrame, false));
    k.onMessage(off.v, 30);
    k.tick(5030);
    assert(k.status() == kReady);
}

static void testTimeoutAndReset() {
    Core k = readyCore();
    assert(k.submit(batchOf(2), 0) == kQueued);
    most::Msg a, b;
    assert(k.next(0, a) && k.next(10, b));
    assert(k.msUntilDeadline(0) == 1500);
    Wire ra = answer(a);
    k.onMessage(ra.v, 200);
    k.tick(1509);
    assert(k.status() == kReady && k.msUntilDeadline(1509) == 1);
    k.tick(1510);
    assert(k.status() == kFailed && k.stats().timeouts == 1 && !k.busy());
    assert(k.submit(batchOf(1), 1600) == kNotReady);
    k.reset(1700);
    assert(k.status() == kReady);
    assert(pumpAnswered(k, 1800) == 0);
    assert(k.submit(batchOf(1), 1800) == kQueued && pumpAnswered(k, 1800) == 1 && !k.busy());

    // A link that comes back clears it too, and so does a new host.
    assert(k.submit(batchOf(1), 2000) == kQueued);
    most::Msg c;
    assert(k.next(2000, c));
    k.tick(4000);
    assert(k.status() == kFailed);
    k.setLink(false, 0, 4100);
    k.setLink(true, kSelf, 4200);
    assert(k.status() == kReady);
    k.sendFailed(4300);
    assert(k.status() == kFailed);
    k.setHost(0x0124, 4400);
    assert(k.status() == kReady && k.host() == 0x0124);
}

static void testErrorAnswers() {
    Core k = readyCore();
    assert(k.submit(batchOf(1), 0) == kQueued);
    most::Msg m;
    assert(k.next(0, m));
    std::vector<uint8_t> err;
    putBe16(err, 5);
    err.push_back(0x07);                                 // invented error code
    Wire e(kHost, kSelf, m.fkt, most::kOpErrorAck, err);
    k.onMessage(e.v, 5);
    assert(k.status() == kFailed && k.stats().errors == 1);

    Core k2 = readyCore();
    assert(k2.submit(batchOf(1), 0) == kQueued);
    assert(k2.next(0, m));
    std::vector<uint8_t> code(1, 0x02);
    Wire e2(kHost, kSelf, m.fkt, most::kOpError, code);  // no handle: matched on the function
    k2.onMessage(e2.v, 5);
    assert(k2.status() == kFailed && k2.stats().errors == 1 && k2.stats().strays == 0);
}

static void testLinkDropKeepsNav() {
    Core k = readyCore();
    Wire nav = navCall(kSetVisibility, setVisibility(kFrame, true));
    k.onMessage(nav.v, 0);
    k.setLink(false, 0, 10);
    k.setLink(true, kSelf, 20);
    assert(k.status() == kNavActive);                    // still the navigation's
    k.setHost(0x0124, 30);
    assert(k.status() == kReady);                        // another cluster: start over
}

static void testLateAnswerAfterLinkDrop() {
    Core k = readyCore();
    assert(k.submit(batchOf(1), 0) == kQueued);
    most::Msg m;
    assert(k.next(0, m));
    k.setLink(false, 0, 10);
    Wire a = answer(m);
    k.onMessage(a.v, 20);                                // to our old address: not the navigation
    k.setLink(true, kSelf, 30);
    assert(k.status() == kReady && k.stats().navMsgs == 0);
}

// ---- HudTerminal, threaded -----------------------------------------------------------------------------------------

struct FakeCluster {
    pthread_mutex_t lock;
    pthread_cond_t cond;
    std::vector<most::Msg> sent;
    bool refuse;
    FakeCluster() : refuse(false) { pthread_mutex_init(&lock, NULL); pthread_cond_init(&cond, NULL); }
    bool waitSent(size_t n) {
        for (int i = 0; i < 200; ++i) {
            pthread_mutex_lock(&lock);
            bool ok = sent.size() >= n;
            pthread_mutex_unlock(&lock);
            if (ok) return true;
            usleep(10 * 1000);
        }
        return false;
    }
    most::Msg at(size_t i) {
        pthread_mutex_lock(&lock);
        most::Msg m = sent[i];
        pthread_mutex_unlock(&lock);
        return m;
    }
};

static bool fakeSend(const most::Msg& m, void* user) {
    FakeCluster* f = static_cast<FakeCluster*>(user);
    pthread_mutex_lock(&f->lock);
    if (!f->refuse) f->sent.push_back(m);
    bool ok = !f->refuse;
    pthread_mutex_unlock(&f->lock);
    return ok;
}

struct StatusLog {
    pthread_mutex_t lock;
    std::vector<Status> seen;
    StatusLog() { pthread_mutex_init(&lock, NULL); }
    bool waitFor(Status s) {
        for (int i = 0; i < 200; ++i) {
            pthread_mutex_lock(&lock);
            bool ok = !seen.empty() && seen.back() == s;
            pthread_mutex_unlock(&lock);
            if (ok) return true;
            usleep(10 * 1000);
        }
        return false;
    }
};

static void onStatus(Status s, void* user) {
    StatusLog* l = static_cast<StatusLog*>(user);
    pthread_mutex_lock(&l->lock);
    l->seen.push_back(s);
    pthread_mutex_unlock(&l->lock);
}

static std::vector<most::RegistryEntry> registry(uint16_t host) {
    std::vector<most::RegistryEntry> reg;
    most::RegistryEntry e;
    e.address = host;
    e.key = most::registryKey(0x82, 0x00);
    reg.push_back(e);
    return reg;
}

static void testThreaded() {
    FakeCluster fc;
    StatusLog log;
    Config c = config();
    c.replyMs = 300;
    HudTerminal t(c);
    t.setSender(&fakeSend, &fc);
    t.setStatusListener(&onStatus, &log);
    assert(t.start());
    assert(t.submit(batchOf(1)) == kNotReady);
    t.onLink(true, kSelf);
    t.onRegistry(registry(kHost));
    assert(log.waitFor(kReady) && t.status() == kReady);

    assert(t.submit(batchOf(3)) == kQueued);
    assert(fc.waitSent(3));
    assert(!t.waitIdle(50));                             // nothing answered yet: times out
    for (size_t i = 0; i < 3; ++i) {
        most::Msg m = fc.at(i);
        Wire a = answer(m), cp = copyOf(m);
        t.onMessage(a.v);
        t.onMessage(cp.v);
    }
    assert(t.waitIdle(1000));
    assert(t.stats().batchesDone == 1 && t.stats().copies == 3 && t.status() == kReady);

    Wire nav = navCall(kSetVisibility, setVisibility(kFrame, true));
    t.onMessage(nav.v);
    assert(log.waitFor(kNavActive));
    Wire off = navCall(kSetVisibility, setVisibility(kFrame, false));
    t.onMessage(off.v);
    t.stop();

    // No answer: the send thread's own timer fails it.
    HudTerminal t2(c);
    StatusLog log2;
    FakeCluster fc2;
    t2.setSender(&fakeSend, &fc2);
    t2.setStatusListener(&onStatus, &log2);
    assert(t2.start());
    t2.onLink(true, kSelf);
    t2.onRegistry(registry(kHost));
    assert(log2.waitFor(kReady));
    assert(t2.submit(batchOf(1)) == kQueued);
    assert(fc2.waitSent(1));
    assert(!t2.waitIdle(5000));                          // returns when the status leaves ready, not after 5 s
    assert(log2.waitFor(kFailed) && t2.stats().timeouts == 1);
    t2.reset();
    assert(log2.waitFor(kReady));
    pthread_mutex_lock(&fc2.lock);
    fc2.refuse = true;                                   // the client did not send it
    pthread_mutex_unlock(&fc2.lock);
    assert(t2.submit(batchOf(1)) == kQueued);
    assert(log2.waitFor(kFailed));
    t2.logHealth();
    t2.stop();
}

// ---- HudTerminal on a most::Client --------------------------------------------------------------------------------

static void deliver(const most::Msg& m) {
    std::vector<uint8_t> f = most::buildMessage(m);
    fake_dsi::data(most::kInfoMessage, &f[0], (uint32_t)f.size());
}

static void testOnClient() {
    fake_dsi::reset();
    most::Client client;
    HudTerminal t(config());
    assert(t.attach(client));
    assert(client.start("T"));
    fake_dsi::attach();
    std::vector<uint8_t> resp;
    most::putLe32(resp, 77);
    most::putLe16(resp, kSelf);
    fake_dsi::data(most::kInfoClientResponse, &resp[0], (uint32_t)resp.size());
    assert(t.status() == kOffline);                      // no registry yet
    std::vector<uint8_t> reg;
    most::putLe32(reg, 2);
    most::putLe16(reg, 0x0140); most::putLe16(reg, most::registryKey(0x52, 0));
    most::putLe16(reg, kHost); most::putLe16(reg, most::registryKey(0x82, 0));
    fake_dsi::data(most::kAttrRegistry, &reg[0], (uint32_t)reg.size());
    assert(t.status() == kReady);

    // A call as the terminal builds it, through the client's guarded send: our address is filled in.
    most::Msg m;
    assert(buildCall(kHost, Call(kSetVisibility, setVisibility(kFrame, true)), 9, config().handles, m));
    assert(client.send(m));
    const fake_dsi::Call& req = fake_dsi::calls.back();
    assert(req.kind == fake_dsi::Call::kRequest && req.id == most::kReqProcessMessage);
    assert(most::rdLe32(&req.payload[0]) == 77);
    most::View v;
    assert(most::parseView(&req.payload[4], req.payload.size() - 4, v));
    assert(v.src == kSelf && v.dst == kHost && v.fblock == 0x82 && v.inst == 0 && v.fkt == kSetVisibility);
    assert(v.op == most::kOpStartResultAck && v.len == 3 && v.data[0] == 0 && v.data[1] == kFrame && v.data[2] == 1);

    // The route: the navigation's traffic reaches the terminal.
    most::Msg nav;
    nav.txType = 0; nav.varTag = 2; nav.varVal = 0; nav.dst = kNav; nav.src = kHost; nav.fblock = 0x82; nav.inst = 0;
    nav.fkt = kRemoveAllItems; nav.opType = most::kOpResultAck; nav.hdrTag = 0;
    nav.data = removeAllItems(7);
    deliver(nav);
    assert(t.status() == kNavActive);

    fake_dsi::detach();
    assert(t.status() == kOffline);
}

// Attached without sending (the shadow mode): the terminal watches, and the client refuses any call to 82/00.
static void testWatchOnly() {
    fake_dsi::reset();
    most::Client client;
    HudTerminal t(config());
    assert(t.attach(client, false));
    assert(client.start("T"));
    fake_dsi::attach();
    std::vector<uint8_t> resp;
    most::putLe32(resp, 77);
    most::putLe16(resp, kSelf);
    fake_dsi::data(most::kInfoClientResponse, &resp[0], (uint32_t)resp.size());
    std::vector<uint8_t> reg;
    most::putLe32(reg, 1);
    most::putLe16(reg, kHost); most::putLe16(reg, most::registryKey(0x82, 0));
    fake_dsi::data(most::kAttrRegistry, &reg[0], (uint32_t)reg.size());
    assert(t.status() == kReady);

    size_t before = fake_dsi::calls.size();
    most::Msg m;
    assert(buildCall(kHost, Call(kSetVisibility, setVisibility(kFrame, true)), 9, config().handles, m));
    assert(!client.send(m));
    assert(fake_dsi::calls.size() == before);

    most::Msg nav;
    nav.txType = 0; nav.varTag = 2; nav.varVal = 0; nav.dst = kHost; nav.src = kNav; nav.fblock = 0x82; nav.inst = 0;
    nav.fkt = kSetVisibility; nav.opType = most::kOpStartResultAck; nav.hdrTag = 0;
    nav.data = setVisibility(kFrame, true);
    deliver(nav);
    assert(t.status() == kNavActive);
    fake_dsi::detach();
}

int main() {
    testStatusAndOffline();
    testRequestBytes();
    testRefusals();
    testBackToBackAndWindow();
    testAnswersAndQueue();
    testMerge();
    testEpoch();
    testProfileMismatch();
    testCopiesIgnored();
    testOwnAndForeignIgnored();
    testNavTakeoverMidBatch();
    testNavFinishes();
    testNavWithoutRoute();
    testLateErrorAfterTakeover();
    testTimeoutAndReset();
    testErrorAnswers();
    testLinkDropKeepsNav();
    testLateAnswerAfterLinkDrop();
    testThreaded();
    testOnClient();
    testWatchOnly();
    printf("test_hud_terminal: all passed\n");
    return 0;
}
