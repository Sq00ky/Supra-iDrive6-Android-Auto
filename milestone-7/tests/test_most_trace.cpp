// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test: the NBT_MOST_LOG filter (first lines, changes only, the line cap, the type table), and Trace on the client.
#include "most/trace.hpp"
#include "most/client.hpp"
#include "fake_dsi_client.hpp"
#include <assert.h>
#include <stdio.h>
#include <string.h>

using namespace nbt;
using namespace nbt::most;

static View msg(uint8_t fblock, uint8_t inst, uint16_t fkt, uint8_t op, const char* text) {
    View v;
    memset(&v, 0, sizeof(v));
    v.varTag = 2; v.fblock = fblock; v.inst = inst; v.fkt = fkt; v.op = op;
    v.data = reinterpret_cast<const uint8_t*>(text);
    v.len = strlen(text);
    return v;
}

static void testFilter() {
    typedef TraceFilter F;
    static F f;

    // The first few of a type, whatever they say, then only changes.
    for (int i = 0; i < F::kFirstLines; ++i) assert(f.feed(msg(0x05, 0, 0x620, 0xC, "same")) == F::kLog);
    assert(f.feed(msg(0x05, 0, 0x620, 0xC, "same")) == F::kSkip);
    assert(f.feed(msg(0x05, 0, 0x620, 0xC, "same")) == F::kSkip);
    assert(f.feed(msg(0x05, 0, 0x620, 0xC, "diff")) == F::kLog);
    assert(f.feed(msg(0x05, 0, 0x620, 0xC, "diff")) == F::kSkip);
    assert(f.feed(msg(0x05, 0, 0x620, 0xC, "diffs")) == F::kLog);    // longer
    assert(f.feed(msg(0x05, 0, 0x620, 0xC, "")) == F::kLog);         // empty

    // Each of fblock, inst, fkt and op makes a type of its own.
    assert(f.feed(msg(0x06, 0, 0x620, 0xC, "same")) == F::kLog);
    assert(f.feed(msg(0x05, 1, 0x620, 0xC, "same")) == F::kLog);
    assert(f.feed(msg(0x05, 0, 0x621, 0xC, "same")) == F::kLog);
    assert(f.feed(msg(0x05, 0, 0x620, 0x1, "same")) == F::kLog);

    // The cap: a signal that changes all the time stops at its line limit and says so once. Other types carry on.
    char text[16];
    int logged = 0, finals = 0;
    for (int i = 0; i < F::kLinesPerType * 3; ++i) {
        snprintf(text, sizeof(text), "%d", i);
        F::Decision d = f.feed(msg(0x52, 0, 0x440, 0xC, text));
        if (d == F::kLog) ++logged;
        else if (d == F::kLogFinal) ++finals;
        else assert(d == F::kSkip);
    }
    assert(logged == F::kLinesPerType - 1 && finals == 1);
    assert(f.feed(msg(0x52, 0, 0x441, 0xC, "x")) == F::kLog);

    // The type table is bounded, and types already in it keep working.
    static F g;
    int fblock = 0, added = 0;
    for (int i = 0; i < F::kMaxTypes; ++i, ++added) {
        assert(g.feed(msg((uint8_t)fblock, (uint8_t)(i & 0xff), 0x100, 0xC, "x")) == F::kLog);
        if ((i & 0xff) == 0xff) ++fblock;
    }
    assert(added == F::kMaxTypes);
    assert(g.feed(msg(0xFF, 0xFF, 0xFFF, 0xF, "x")) == F::kTableFull);
    assert(g.feed(msg(0, 0, 0x100, 0xC, "y")) == F::kLog);
}

static void noRoute(const View&, void*) {}

static unsigned g_tapped = 0;
static MsgClass g_tappedClass = kNormal;
static void countTap(const View& m, MsgClass c, void*) {
    assert(m.rxMs != 0);
    ++g_tapped;
    g_tappedClass = c;
}
static unsigned g_infos = 0;
static uint32_t g_infoId = 0;
static void countInfo(uint32_t id, const uint8_t*, uint32_t, void*) {
    ++g_infos;
    g_infoId = id;
}

static std::vector<uint8_t> frame(uint32_t varVal) {
    Msg m;
    m.txType = 1; m.varTag = 2; m.varVal = varVal; m.dst = 0x2022; m.src = 0x2021;
    m.fblock = 0x05; m.inst = 0; m.fkt = 0x620; m.opType = 0xC; m.hdrTag = 0;
    const char* p = "abc";
    m.data.assign(p, p + 3);
    return buildMessage(m);
}

// The tap sees every message with its class, routed or not, and counts as a consumer.
static void testTap() {
    fake_dsi::reset();
    Client c;
    assert(c.setTap(&countTap, NULL));
    assert(c.setInfoTap(&countInfo, NULL));
    assert(!c.setTap(&countTap, NULL));                     // one of each
    assert(!c.setInfoTap(&countInfo, NULL));
    assert(c.start("X"));
    assert(fake_dsi::opened());                             // no route, but it starts
    fake_dsi::attach();
    assert(c.stats().msgs == 0);

    std::vector<uint8_t> f = frame(0);
    fake_dsi::data(kInfoMessage, &f[0], (uint32_t)f.size());
    assert(g_tapped == 1 && g_tappedClass == kNormal);
    f = frame(5);
    fake_dsi::data(kInfoMessage, &f[0], (uint32_t)f.size());
    assert(g_tapped == 2 && g_tappedClass == kTxStatus);    // dropped from routing, still seen here
    f = frame(19);
    fake_dsi::data(kInfoMessage, &f[0], (uint32_t)f.size());
    assert(g_tapped == 3 && g_tappedClass == kSegFirst);
    assert(g_infos == 0);                                   // messages are not information events

    const uint8_t reg[8] = { 1, 0, 0, 0, 0x21, 0x20, 0x00, 0x52 };
    fake_dsi::data(kAttrRegistry, reg, sizeof(reg));
    assert(g_infos == 1 && g_infoId == kAttrRegistry);

    fake_dsi::reset();
    Client started;
    assert(started.addRoute(0xE5, kAny, kAny, kAny, &noRoute, NULL));
    assert(started.start("X"));
    assert(!started.setTap(&countTap, NULL));               // before start() only
    assert(!started.setInfoTap(&countInfo, NULL));
}

// The real logger on the client: this checks the paths run and stay quiet about the client's own behaviour.
static void testTrace() {
    fake_dsi::reset();
    Client c;
    Trace t;
    assert(t.attach(c));
    Trace second;
    assert(!second.attach(c));                              // the client has its tap
    assert(c.start("X"));
    fake_dsi::attach();
    std::vector<uint8_t> resp;
    putLe32(resp, 93);
    putLe16(resp, 0x205D);
    fake_dsi::data(kInfoClientResponse, &resp[0], (uint32_t)resp.size());
    assert(c.state() == kUp);
    assert(fake_dsi::calls.back().kind == fake_dsi::Call::kNotify);    // no keys of its own: request 1 is skipped

    for (int i = 0; i < 6; ++i) {
        std::vector<uint8_t> f = frame(0);
        fake_dsi::data(kInfoMessage, &f[0], (uint32_t)f.size());
    }
    assert(c.stats().msgs == 6 && c.stats().routed == 0);   // it observes and does not route

    const uint8_t reg[8] = { 1, 0, 0, 0, 0x21, 0x20, 0x00, 0x52 };
    fake_dsi::data(kAttrRegistry, reg, sizeof(reg));
    fake_dsi::data(kAttrRegistry, reg, sizeof(reg));        // unchanged: one dump
    fake_dsi::data(kAttrRegistry, reg, 3);                  // does not decode
    const uint8_t inst[3] = { 0x00, 0x52, 0x01 };
    fake_dsi::data(kInfoChangeInstId, inst, sizeof(inst));
    fake_dsi::data(kInfoChangeInstId, inst, 2);             // too short: ignored
}

int main() {
    testFilter();
    testTap();
    testTrace();
    printf("test_most_trace OK\n");
    return 0;
}
