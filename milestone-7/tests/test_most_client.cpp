// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test: most::Client against the fake DsiClient (tests/fake_dsi_client.cpp): start-up, the exact request order,
// refusal, the empty key list, detach, re-attach, link state, message classes and counters.
#include "most/client.hpp"
#include "fake_dsi_client.hpp"
#include <assert.h>
#include <stdio.h>
#include <string.h>

using namespace nbt;
using namespace nbt::most;

// ---- helpers ------------------------------------------------------------------------------------------------------

static View g_last;
static unsigned g_handled = 0;
static std::vector<uint8_t> g_lastData;
static void handler(const View& m, void*) {
    g_last = m;
    g_lastData.assign(m.data, m.data + m.len);
    ++g_handled;
}
static void resetHandler() { g_handled = 0; g_lastData.clear(); }

static std::vector<LinkState> g_links;
static void* g_linkUser = NULL;
static void onLink(LinkState s, void* user) {
    g_links.push_back(s);
    g_linkUser = user;
}
static bool linksAre(LinkState a, int n = 1, LinkState b = kUp, LinkState c = kUp) {
    if (g_links.size() != (size_t)n) return false;
    return g_links[0] == a && (n < 2 || g_links[1] == b) && (n < 3 || g_links[2] == c);
}

static std::vector<uint8_t> frame(uint32_t tx, uint32_t varTag, uint32_t varVal, uint8_t fblock, uint8_t inst, uint16_t fkt,
                                  uint8_t op, const char* payload = "") {
    Msg m;
    m.txType = tx; m.varTag = varTag; m.varVal = varVal; m.dst = 0x2022; m.src = 0x2021;
    m.fblock = fblock; m.inst = inst; m.fkt = fkt; m.opType = op; m.hdrTag = 33;
    m.data.assign(payload, payload + strlen(payload));
    return buildMessage(m);
}

static void sendMsg(const std::vector<uint8_t>& f) { fake_dsi::data(kInfoMessage, &f[0], (uint32_t)f.size()); }

static void respond(uint32_t client, uint16_t dev) {
    std::vector<uint8_t> p;
    putLe32(p, client);
    putLe16(p, dev);
    fake_dsi::data(kInfoClientResponse, &p[0], (uint32_t)p.size());
}

static void comm(uint32_t v) {
    std::vector<uint8_t> p;
    putLe32(p, v);
    fake_dsi::data(kAttrCommState, &p[0], (uint32_t)p.size());
}

static size_t countRequests(uint32_t id) {
    size_t n = 0;
    for (size_t i = 0; i < fake_dsi::calls.size(); ++i)
        if (fake_dsi::calls[i].kind == fake_dsi::Call::kRequest && fake_dsi::calls[i].id == id) ++n;
    return n;
}

// ---- tests --------------------------------------------------------------------------------------------------------

static void testNoConsumer() {
    fake_dsi::reset();
    Client c;
    assert(c.start("X"));                      // nothing to do is not a failure
    assert(!fake_dsi::opened() && fake_dsi::calls.empty());
    assert(c.state() == kDetached);
    c.logHealth();                             // and it stays silent, with no crash
}

static void testOpenFails() {
    fake_dsi::reset();
    fake_dsi::openResult = false;
    Client c;
    assert(c.addRoute(0xE5, kAny, kAny, kAny, &handler, NULL));
    assert(!c.start("X"));
    assert(c.state() == kDetached);
    c.logHealth();
}

static void testRequestOrder() {
    fake_dsi::reset();
    Client c;
    assert(c.addRoute(0xE5, kAny, kAny, kAny, &handler, NULL));
    assert(c.addDevCtrl(0xE503) && c.addDevCtrl(0x1502) && c.addDevCtrl(0xE503));    // the repeat is sent once
    assert(c.start("NBTAAD_MOST"));
    assert(fake_dsi::openedIface == "DSYSMOSTServer" && fake_dsi::openedRole == "MOSTSubSystem");
    assert(fake_dsi::openedMajor == 4 && fake_dsi::openedMinor == 5);
    assert(fake_dsi::calls.size() == 1 && fake_dsi::calls[0].kind == fake_dsi::Call::kOpen);

    // Before start() only.
    assert(!c.addRoute(0x52, 0, 0x440, kAny, &handler, NULL));
    assert(!c.addDevCtrl(0x5200));
    assert(!c.start("NBTAAD_MOST"));

    fake_dsi::attach();
    assert(c.state() == kRegistering);
    // Name first, then the ten subscriptions in the HMI's order.
    const size_t kAfterAttach = 1 + 1 + kClientSubscriptionCount;
    assert(fake_dsi::calls.size() == kAfterAttach);
    assert(fake_dsi::calls[1].kind == fake_dsi::Call::kRequest && fake_dsi::calls[1].id == kReqRegisterClient);
    std::vector<uint8_t> name;
    DsiClient::putString(name, "NBTAAD_MOST");
    assert(fake_dsi::calls[1].payload == name);
    for (size_t i = 0; i < (size_t)kClientSubscriptionCount; ++i) {
        assert(fake_dsi::calls[2 + i].kind == fake_dsi::Call::kNotify);
        assert(fake_dsi::calls[2 + i].id == kClientSubscriptions[i]);
    }

    // Comm state and the keys follow the response.
    respond(93, 0x205D);
    assert(c.state() == kUp);
    assert(fake_dsi::calls.size() == kAfterAttach + 2);
    assert(fake_dsi::calls[kAfterAttach].kind == fake_dsi::Call::kNotify && fake_dsi::calls[kAfterAttach].id == kAttrCommState);
    const fake_dsi::Call& k = fake_dsi::calls[kAfterAttach + 1];
    assert(k.kind == fake_dsi::Call::kRequest && k.id == kReqRegisterDevCtrls);
    std::vector<uint16_t> keys;
    keys.push_back(0xE503);
    keys.push_back(0x1502);
    assert(k.payload == buildRegisterDevCtrls(93, keys));

    // A second response does not register the keys again.
    respond(93, 0x205D);
    assert(countRequests(kReqRegisterDevCtrls) == 1);
    c.logHealth();
}

static void testRefused() {
    fake_dsi::reset();
    Client c;
    assert(c.addRoute(0xE5, kAny, kAny, kAny, &handler, NULL));
    assert(c.addDevCtrl(0xE503));
    assert(c.start("X"));
    fake_dsi::attach();
    respond(kClientRefused, 0);
    assert(c.state() == kRefused);
    assert(countRequests(kReqRegisterDevCtrls) == 0);
    c.logHealth();
}

static void testEmptyKeys() {
    fake_dsi::reset();
    Client c;
    assert(c.addRoute(0x52, 0, 0x440, kOpStatus, &handler, NULL));
    assert(c.start("X"));
    fake_dsi::attach();
    respond(94, 0x205E);
    assert(c.state() == kUp);
    assert(countRequests(kReqRegisterDevCtrls) == 0);     // the server logs an error for an empty list
}

static void testReattach() {
    fake_dsi::reset();
    Client c;
    assert(c.addRoute(0xE5, kAny, kAny, kAny, &handler, NULL));
    assert(c.addDevCtrl(0xE503));
    assert(c.start("X"));
    fake_dsi::attach();
    respond(93, 0x205D);
    assert(countRequests(kReqRegisterClient) == 1 && countRequests(kReqRegisterDevCtrls) == 1);

    // The link goes away. No send is attempted.
    size_t before = fake_dsi::calls.size();
    fake_dsi::detach();
    assert(c.state() == kDetached && fake_dsi::calls.size() == before);

    // New registration, new ids; routes and keys persist.
    fake_dsi::attach();
    assert(c.state() == kRegistering);
    assert(countRequests(kReqRegisterClient) == 2);
    respond(94, 0x205E);
    assert(c.state() == kUp);
    assert(countRequests(kReqRegisterDevCtrls) == 2);
    std::vector<uint16_t> keys(1, 0xE503);
    assert(fake_dsi::calls.back().payload == buildRegisterDevCtrls(94, keys));

    resetHandler();
    sendMsg(frame(0, 2, 0, 0xE5, 3, 0xC46, 0xC, "abc"));
    assert(g_handled == 1);
}

static void testMessages() {
    fake_dsi::reset();
    Client c;
    assert(c.addRoute(0xE5, kAny, kAny, kAny, &handler, NULL));
    assert(c.start("X"));
    fake_dsi::attach();
    respond(93, 0x205D);
    resetHandler();

    // The handler sees the header, the payload and a receive time.
    sendMsg(frame(0, 2, 0, 0xE5, 3, 0xC46, 0xC, "abc"));
    assert(g_handled == 1 && g_last.fblock == 0xE5 && g_last.inst == 3 && g_last.fkt == 0xC46 && g_last.op == 0xC);
    assert(g_last.src == 0x2021 && g_last.dst == 0x2022 && g_last.hdrTag == 33);
    assert(g_lastData.size() == 3 && !memcmp(&g_lastData[0], "abc", 3));
    assert(g_last.rxMs != 0);

    // Not routed: counted, not delivered.
    sendMsg(frame(0, 2, 0, 0x05, 0, 0x620, 0xC));
    assert(g_handled == 1);

    // A frame that does not parse counts as bad, and nothing is delivered.
    std::vector<uint8_t> bad = frame(0, 2, 0, 0xE5, 3, 0xC46, 0xC, "abc");
    fake_dsi::data(kInfoMessage, &bad[0], 20);
    assert(g_handled == 1);

    // Segments reach a route only when joined, once, as a normal message.
    resetHandler();
    sendMsg(frame(1, 2, 19, 0xE5, 3, 0xC46, 0xC, "s1"));
    sendMsg(frame(1, 2, 20, 0xE5, 3, 0xC46, 0xC, "s2"));
    assert(g_handled == 0);
    sendMsg(frame(1, 2, 21, 0xE5, 3, 0xC46, 0xC, "s3"));
    assert(g_handled == 1 && g_lastData.size() == 6 && !memcmp(&g_lastData[0], "s1s2s3", 6));
    assert(g_last.varVal == 0 && g_last.fkt == 0xC46 && g_last.rxMs != 0);

    // Transmit status never reaches a route. The noinfo class does: it is real traffic on the car.
    resetHandler();
    sendMsg(frame(0, 2, 5, 0xE5, 3, 0xC46, 0xC, "tx"));
    assert(g_handled == 0);
    sendMsg(frame(0, 1, 0, 0xE5, 3, 0xC46, 0xC, "no"));
    assert(g_handled == 1 && g_lastData.size() == 2 && !memcmp(&g_lastData[0], "no", 2));

    // A segment no route wants is not buffered: its last segment joins nothing, and is not counted as lost.
    resetHandler();
    sendMsg(frame(1, 2, 19, 0x05, 0, 0x620, 0xC, "a"));
    sendMsg(frame(1, 2, 21, 0x05, 0, 0x620, 0xC, "b"));
    assert(g_handled == 0);

    // A last segment with no first, on a route, is lost.
    sendMsg(frame(1, 2, 21, 0xE5, 3, 0xC47, 0xC, "z"));
    assert(g_handled == 0);

    ClientStats s = c.stats();
    assert(s.msgs == 10 && s.normal == 2 && s.seg == 6 && s.txStatus == 1 && s.noInfo == 1 && s.bad == 1);
    assert(s.routed == 3);                // the normal message, the joined one, the noinfo one
    assert(s.routedNonNormal == 6);       // three segments, one status, one noinfo, the lost segment
    assert(s.joined == 1 && s.segLost == 1);

    // The attributes do not count as messages, and comm state is logged only.
    comm(1);
    comm(1);
    comm(2);
    assert(c.stats().msgs == 10 && c.state() == kUp);

    // Odd or short DSI events are ignored.
    fake_dsi::data(kAttrCommState, "x", 1);
    fake_dsi::data(kInfoClientResponse, "x", 1);
    fake_dsi::data(kAttrRegistry, "xxxx", 4);
    fake_dsi::data(kInfoMessage, NULL, 0);
    assert(c.state() == kUp);
    c.logHealth();
    c.logHealth();
}

static void testLinkState() {
    fake_dsi::reset();
    g_links.clear();
    Client c;
    int token = 0;
    assert(c.addRoute(0xE5, kAny, kAny, kAny, &handler, NULL));
    assert(c.addLinkListener(&onLink, &token));
    assert(!c.addLinkListener(NULL, NULL));
    assert(c.start("X"));
    assert(!c.addLinkListener(&onLink, &token));       // before start() only
    assert(c.state() == kDetached && g_links.empty());

    fake_dsi::attach();
    assert(linksAre(kRegistering) && g_linkUser == &token);
    g_links.clear();
    respond(93, 0x205D);
    assert(linksAre(kUp));
    g_links.clear();

    // The car reports comm state 1 while messages flow: until OK has been seen, it says nothing against the link.
    comm(1);
    comm(0);
    comm(1);
    assert(c.state() == kUp && g_links.empty());

    // OK seen, and now gone: tell the consumers, once. Messages keep routing all the same.
    comm(2);
    assert(c.state() == kUp && g_links.empty());
    comm(1);
    assert(c.state() == kCommNotOk && linksAre(kCommNotOk));
    comm(0);
    assert(linksAre(kCommNotOk));
    resetHandler();
    sendMsg(frame(0, 2, 0, 0xE5, 3, 0xC46, 0xC, "abc"));
    assert(g_handled == 1);
    c.logHealth();
    g_links.clear();
    comm(2);
    assert(c.state() == kUp && linksAre(kUp));
    g_links.clear();

    // Detach from any state, then a fresh start: OK has to be seen again.
    comm(1);
    assert(linksAre(kCommNotOk));
    g_links.clear();
    fake_dsi::detach();
    assert(c.state() == kDetached && linksAre(kDetached));
    resetHandler();
    sendMsg(frame(0, 2, 0, 0xE5, 3, 0xC46, 0xC, "abc"));   // routing does not depend on the link
    assert(g_handled == 1);
    c.logHealth();
    g_links.clear();
    fake_dsi::detach();                                     // a second detach changes nothing
    assert(g_links.empty());

    fake_dsi::attach();
    respond(94, 0x205E);
    assert(linksAre(kRegistering, 2, kUp));
    g_links.clear();
    comm(1);
    assert(c.state() == kUp && g_links.empty());

    // Attach again with no detach in between: the link is not up until the new answer.
    fake_dsi::attach();
    assert(linksAre(kRegistering));
    g_links.clear();

    // Refused, then detached.
    respond(kClientRefused, 0);
    assert(linksAre(kRefused));
    g_links.clear();
    fake_dsi::detach();
    assert(linksAre(kDetached));

    // The listener limit.
    Client d;
    for (int i = 0; i < Client::kMaxLinkListeners; ++i) assert(d.addLinkListener(&onLink, NULL));
    assert(!d.addLinkListener(&onLink, NULL));
}

int main() {
    testNoConsumer();
    testOpenFails();
    testRequestOrder();
    testRefused();
    testEmptyKeys();
    testReattach();
    testLinkState();
    testMessages();
    printf("test_most_client OK\n");
    return 0;
}
