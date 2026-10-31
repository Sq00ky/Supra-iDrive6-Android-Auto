// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "most_msg.hpp"
#include <stdio.h>
#include <assert.h>
using namespace nbt;
int main() {
    MostMsg a;
    a.txType = 1; a.varTag = 2; a.varVal = 5; a.dst = 0x1001; a.src = 0x0401;
    a.fblock = 0xE5; a.inst = 0x10; a.fkt = 0xC46; a.opType = 0xC; a.hdrTag = 7;
    const uint8_t d[8] = { 0x01, 0x00, 0x2A, 0, 0, 0, 0, 0 };
    a.data.assign(d, d + 8);
    std::vector<uint8_t> w = buildMostMessage(a);
    assert(w.size() == 40);
    MostMsg b;
    assert(parseMostMessage(&w[0], w.size(), b));
    assert(b.fblock == 0xE5 && b.inst == 0x10 && b.fkt == 0xC46 && b.opType == 0xC && b.data.size() == 8);
    assert(b.dst == 0x1001 && b.src == 0x0401 && b.hdrTag == 7 && b.varTag == 2 && b.varVal == 5);
    assert(!parseMostMessage(&w[0], 20, b));
    std::vector<uint16_t> k; k.push_back(0xE510); k.push_back(0xE503); k.push_back(0x1502);
    std::vector<uint8_t> r = buildRegisterDevCtrls(3, k);
    assert(r.size() == 16 && r[0] == 3 && r[4] == 3 && r[8] == 0x10 && r[9] == 0xE5 && r[12] == 0x02 && r[13] == 0x15);
    printf("test_most_msg OK\n");
    return 0;
}
