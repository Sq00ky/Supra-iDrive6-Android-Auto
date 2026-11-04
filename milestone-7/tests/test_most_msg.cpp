// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "most/ids.hpp"
#include "most/msg.hpp"
#include <stdio.h>
#include <assert.h>
#include <string.h>
using namespace nbt::most;
int main() {
    Msg a;
    a.txType = 1; a.varTag = 2; a.varVal = 5; a.dst = 0x1001; a.src = 0x0401;
    a.fblock = 0xE5; a.inst = 0x10; a.fkt = 0xC46; a.opType = 0xC; a.hdrTag = 7;
    const uint8_t d[8] = { 0x01, 0x00, 0x2A, 0, 0, 0, 0, 0 };
    a.data.assign(d, d + 8);
    std::vector<uint8_t> w = buildMessage(a);
    assert(w.size() == 40);
    Msg b;
    assert(parseMessage(&w[0], w.size(), b));
    assert(b.fblock == 0xE5 && b.inst == 0x10 && b.fkt == 0xC46 && b.opType == 0xC && b.data.size() == 8);
    assert(b.dst == 0x1001 && b.src == 0x0401 && b.hdrTag == 7 && b.varTag == 2 && b.varVal == 5);
    assert(!parseMessage(&w[0], 20, b));
    std::vector<uint16_t> k; k.push_back(0xE510); k.push_back(0xE503); k.push_back(0x1502);
    std::vector<uint8_t> r = buildRegisterDevCtrls(3, k);
    assert(r.size() == 16 && r[0] == 3 && r[4] == 3 && r[8] == 0x10 && r[9] == 0xE5 && r[12] == 0x02 && r[13] == 0x15);
    // The view reads the same fields as the owning form, and does not copy the payload.
    View v;
    assert(parseView(&w[0], w.size(), v));
    assert(v.fblock == 0xE5 && v.inst == 0x10 && v.fkt == 0xC46 && v.op == 0xC && v.len == 8 && v.hdrTag == 7);
    assert(v.data == &w[32] && v.rxMs == 0);
    assert(!parseView(&w[0], 20, v) && !parseView(NULL, 40, v));
    assert(!parseView(&w[0], w.size() - 1, v));      // payload length runs past the buffer

    // Classes, by the vendor client's test. tx/var values from the car: 0 2/0 and 1 2/0 are normal, 0 1/0 is noinfo.
    v.txType = 0; v.varTag = 2; v.varVal = 0;  assert(classify(v) == kNormal);
    v.txType = 1; v.varTag = 2; v.varVal = 0;  assert(classify(v) == kNormal);
    v.txType = 0; v.varTag = 1; v.varVal = 0;  assert(classify(v) == kNoInfo);
    v.txType = 1; v.varTag = 2; v.varVal = 19; assert(classify(v) == kSegFirst);
    v.txType = 1; v.varTag = 2; v.varVal = 20; assert(classify(v) == kSegMiddle);
    v.txType = 1; v.varTag = 2; v.varVal = 21; assert(classify(v) == kSegLast);
    v.txType = 0; v.varTag = 2; v.varVal = 19; assert(classify(v) == kNormal);    // a segment needs txType 1
    v.txType = 1; v.varTag = 2; v.varVal = 22; assert(classify(v) == kNormal);
    v.txType = 1; v.varTag = 1; v.varVal = 19; assert(classify(v) == kNoInfo);    // varTag comes first
    v.txType = 0; v.varTag = 2; v.varVal = 3;  assert(classify(v) == kNormal);
    v.txType = 0; v.varTag = 2; v.varVal = 4;  assert(classify(v) == kTxStatus);
    v.txType = 1; v.varTag = 2; v.varVal = 9;  assert(classify(v) == kTxStatus);
    v.txType = 0; v.varTag = 2; v.varVal = 10; assert(classify(v) == kNormal);
    assert(!strcmp(className(kTxStatus), "txstatus") && !strcmp(className(kSegLast), "seg-last"));

    // Op names: a command up to 8, a reply above it, Status and Error as the car logs show them.
    assert(!strcmp(opName(kOpStartAck), "StartAck") && !strcmp(opName(kOpProcessingAck), "ProcessingAck"));
    assert(!strcmp(opName(kOpError), "Error") && !strcmp(opName(16), "?"));
    assert(kClientSubscriptionCount == 10 && kClientSubscriptions[0] == kAttrDeviceId && kClientSubscriptions[9] == kInfoCleanUp);

    printf("test_most_msg OK\n");
    return 0;
}
