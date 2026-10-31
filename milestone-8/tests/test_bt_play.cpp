// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test for the NBT_BT_PLAY payloads and response decoding (g++ -DNBT_HOST_TEST).
#include "bt_play.hpp"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main() {
    std::vector<uint8_t> p;
    nbt::BtPlay::buildU32(p, 0);
    assert(p.size() == 4 && p[0] == 0 && p[1] == 0 && p[2] == 0 && p[3] == 0);   // requestActivate(PLAY)
    nbt::BtPlay::buildU32(p, 1);
    assert(p.size() == 4 && p[0] == 1 && p[3] == 0);                           // requestActivate(STOP)
    const uint8_t ok[] = { 0, 0, 0, 0 }, na[] = { 3, 0, 0, 0 };
    int v = 9;
    assert(nbt::BtPlay::responseText(false, 0x80000003u, ok, 4, &v) && v == 0);
    assert(nbt::BtPlay::responseText(true, 0x80000003u, na, 4, &v) && v == 3);
    assert(strstr(nbt::BtPlay::sgResultName(v), "NOT_ALLOWED"));
    assert(!nbt::BtPlay::responseText(false, 0x80000001u, ok, 4, &v));
    assert(nbt::BtPlay::responseText(false, 0x80000003u, NULL, 0, &v) && v == -1);
    nbt::BtPlay b;
    unsetenv("NBT_BT_PLAY");
    b.init();
    assert(!b.enabled() && b.delayMs() == 1500);
    setenv("NBT_BT_PLAY", "2", 1); setenv("NBT_BT_PLAY_DELAY", "800", 1);
    nbt::BtPlay c;
    c.init();
    assert(c.enabled() && c.delayMs() == 800);
    printf("test_bt_play OK\n");
    return 0;
}
