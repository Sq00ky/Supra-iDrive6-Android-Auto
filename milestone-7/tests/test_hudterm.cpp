// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test: the HUDTerminal protocol (cluster/hudterm.hpp): payload bytes, shape checks, the limits a call is built
// under, text decoding, the registry host and the copy filter. Addresses and values are invented.
#include "cluster/hudterm.hpp"
#include <assert.h>
#include <stdio.h>
#include <string.h>

using namespace nbt;
using namespace nbt::hudterm;

static bool bytesAre(const std::vector<uint8_t>& v, const char* hex) {
    std::vector<uint8_t> want;
    for (const char* p = hex; *p;) {
        if (*p == ' ') { ++p; continue; }
        unsigned b = 0;
        sscanf(p, "%2x", &b);
        want.push_back((uint8_t)b);
        p += 2;
    }
    if (v != want) {
        printf("got:");
        for (size_t i = 0; i < v.size(); ++i) printf(" %02x", v[i]);
        printf("\nwant: %s\n", hex);
        return false;
    }
    return true;
}

static void testPayloads() {
    assert(bytesAre(setVisibility(1, true), "00 01 01"));
    assert(bytesAre(setVisibility(300, false), "01 2c 00"));
    assert(bytesAre(switchVisibility(5, 6), "00 05 00 06"));
    assert(bytesAre(removeAllItems(9), "00 09"));
    assert(bytesAre(getSize(9), "00 09"));
    assert(bytesAre(setFillColor(5, 4), "00 05 04"));
    assert(bytesAre(setLineColor(5, 2), "00 05 02"));
    assert(bytesAre(setLineWidth(5, 3), "00 05 03"));
    assert(bytesAre(setFillMode(5, 1), "00 05 01"));

    std::vector<Point> pts;
    Point a = { 10, -20 }, b = { 300, 4 };
    pts.push_back(a);
    pts.push_back(b);
    assert(bytesAre(addPolygon(5, 0, kPolygon, pts), "00 05 00 00 03 02 00 0a ff ec 01 2c 00 04"));
    assert(addPolygon(5, 0, kPolygon, std::vector<Point>()).empty());
    std::vector<Point> many(kMaxPoints, a);
    assert(addPolygon(5, 1, kPolyline, many).size() == 6 + 4 * (size_t)kMaxPoints);
    many.push_back(a);
    assert(addPolygon(5, 1, kPolyline, many).empty());

    assert(bytesAre(addCircle(7, 1, kPolygon, 50, -60, 20, 0, 3600),
                    "00 07 00 01 03 00 32 ff c4 00 14 00 00 00 00 00 00 0e 10"));
    assert(addCircle(7, 1, kPolygon, 50, 60, 20, -900, -7200).size() == 19);
    assert(addCircle(7, 1, kPolygon, 50, 60, 20, 0, kMaxAngle + 1).empty());
    assert(addCircle(7, 1, kPolygon, 50, 60, 20, -kMaxAngle - 1, 0).empty());
}

static void testText() {
    assert(bytesAre(setText(3, "12"), "00 03 00 00 31 00 32 00 00"));
    assert(bytesAre(setText(2, ""), "00 02 00 00 00"));                   // clears the text
    std::string s32(kMaxTextChars, 'A');
    assert(setText(2, s32.c_str()).size() == 3 + 2 * (size_t)kMaxTextChars + 2);
    assert(payloadShapeOk(kTextareaSetText, setText(2, s32.c_str())));
    std::string s33(kMaxTextChars + 1, 'A');
    assert(setText(2, s33.c_str()).empty());
    assert(setText(2, "caf\xc3\xa9").empty());                            // not ASCII
    assert(setText(2, "a\tb").empty());                                   // control character

    std::vector<uint8_t> t = setText(4, "mi");
    assert(decodeText(&t[0], t.size()) == "mi");
    assert(decodeText(&t[0], t.size() - 2) == "mi");                      // the cluster's answer: no terminator
    const uint8_t odd[] = { 0, 4, 0, 0, 0x41, 0 };
    assert(decodeText(odd, sizeof(odd)) == "");
    const uint8_t wide[] = { 0, 4, 0, 0x01, 0x00, 0, 0x42 };
    assert(decodeText(wide, sizeof(wide)) == "?B");
}

static void testShapes() {
    assert(payloadShapeOk(kSetVisibility, setVisibility(1, true)));
    std::vector<uint8_t> v = setVisibility(1, true);
    v[2] = 2;
    assert(!payloadShapeOk(kSetVisibility, v));
    assert(!payloadShapeOk(kIsVisible, removeAllItems(1)));               // no answer on the car: never sent
    assert(!payloadShapeOk(0x200, removeAllItems(1)));                    // CreateInstance
    assert(!payloadShapeOk(0x203, removeAllItems(1)));                    // ReleaseInstance
    assert(!payloadShapeOk(kRemoveAllItems, setVisibility(1, true)));

    std::vector<uint8_t> t = setText(3, "7");
    t.resize(t.size() - 2);
    assert(!payloadShapeOk(kTextareaSetText, t));                          // no terminator
    t = setText(3, "7");
    t[2] = 1;
    assert(!payloadShapeOk(kTextareaSetText, t));                          // another encoding

    std::vector<Point> pts(3);
    std::vector<uint8_t> p = addPolygon(5, 0, kPolygon, pts);
    assert(payloadShapeOk(kAddPolygon, p));
    p[5] = 4;
    assert(!payloadShapeOk(kAddPolygon, p));                               // count does not match the length
    p = addPolygon(5, 0, kPolygon, pts);
    p[4] = 1;
    assert(!payloadShapeOk(kAddPolygon, p));                               // type

    std::vector<uint8_t> c = addCircle(7, 0, kPolyline, 0, 0, 5, 0, 900);
    assert(payloadShapeOk(kAddCircle, c) && payloadShapeOk(kAddCircleDiameter, c));
    c[18] = 0xff;
    c[17] = 0xff;
    assert(!payloadShapeOk(kAddCircle, c));                                // end angle out of range
}

static void testBuildCall() {
    HandleSet hs;
    hs.allow(1);
    hs.allow(5);
    hs.allow(6);
    hs.allow(0);       // never allowed
    hs.allow(256);     // out of range
    assert(hs.has(1) && hs.has(5) && !hs.has(0) && !hs.has(2) && !hs.has(256));

    most::Msg m;
    m.fkt = 0xABC;
    assert(!buildCall(0x2033, Call(kSetVisibility, setVisibility(1, true)), 1, hs, m));   // not a ring node
    assert(!buildCall(0x0000, Call(kSetVisibility, setVisibility(1, true)), 1, hs, m));
    assert(!buildCall(0x0123, Call(kSetVisibility, setVisibility(2, true)), 1, hs, m));   // handle not allowed
    assert(!buildCall(0x0123, Call(kSwitchVisibility, switchVisibility(5, 7)), 1, hs, m));
    assert(!buildCall(0x0123, Call(kIsVisible, removeAllItems(1)), 1, hs, m));
    assert(m.fkt == 0xABC);                                                               // untouched

    assert(buildCall(0x0123, Call(kSwitchVisibility, switchVisibility(5, 6)), 42, hs, m));
    assert(m.dst == 0x0123 && m.src == 0 && m.fblock == 0x82 && m.inst == 0 && m.fkt == kSwitchVisibility);
    assert(m.opType == most::kOpStartResultAck && m.hdrTag == 42 && m.txType == 0 && m.varTag == 1 && m.varVal == 0);
    assert(bytesAre(m.data, "00 05 00 06"));
    assert(Call(kSwitchVisibility, m.data).handle() == 5);
    assert(Call().handle() == 0);
}

static void testTerminalHost() {
    std::vector<most::RegistryEntry> reg;
    assert(terminalHost(reg) == 0);
    most::RegistryEntry e;
    e.address = 0x0140; e.key = most::registryKey(0x52, 0x00); reg.push_back(e);
    assert(terminalHost(reg) == 0);
    e.address = 0x0123; e.key = most::registryKey(0x82, 0x00); reg.push_back(e);
    assert(terminalHost(reg) == 0x0123);
    reg[1].address = 0x2033;                                              // a head-unit app: never a send target
    assert(terminalHost(reg) == 0);
    reg[1].address = 0x0123;
    reg[1].key = most::registryKey(0x82, 0x01);                           // another instance
    assert(terminalHost(reg) == 0);
}

static void testRecentCalls() {
    RecentCalls r(1500);
    std::vector<uint8_t> t = setText(3, "9");
    t.resize(t.size() - 2);                                               // as the cluster answers it
    assert(!r.isCopy(kTextareaSetText, &t[0], t.size(), 1000));
    r.add(kTextareaSetText, 3, 1000);
    assert(r.isCopy(kTextareaSetText, &t[0], t.size(), 1000));
    assert(r.isCopy(kTextareaSetText, &t[0], t.size(), 2500));
    assert(!r.isCopy(kTextareaSetText, &t[0], t.size(), 2501));           // past the window
    assert(!r.isCopy(kTextareaSetText, &t[0], t.size(), 999));            // before the call
    std::vector<uint8_t> other = setText(4, "9");
    assert(!r.isCopy(kTextareaSetText, &other[0], other.size(), 1000));   // another handle
    assert(!r.isCopy(kSetVisibility, &t[0], t.size(), 1000));             // another function
    assert(!r.isCopy(kTextareaSetText, &t[0], 1, 1000));                  // too short to carry a handle
    r.clear();
    assert(!r.isCopy(kTextareaSetText, &t[0], t.size(), 1000));

    for (int i = 0; i < RecentCalls::kSlots; ++i) r.add(kAddPolygon, (uint16_t)(i + 1), 5000);
    std::vector<uint8_t> h1 = removeAllItems(1);
    assert(r.isCopy(kAddPolygon, &h1[0], 2, 5000));
    r.add(kAddPolygon, 200, 5000);                                         // the oldest slot is reused
    assert(!r.isCopy(kAddPolygon, &h1[0], 2, 5000));
}

int main() {
    testPayloads();
    testText();
    testShapes();
    testBuildCall();
    testTerminalHost();
    testRecentCalls();
    printf("test_hudterm: all passed\n");
    return 0;
}
