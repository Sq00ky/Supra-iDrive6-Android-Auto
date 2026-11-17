// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test: restart after a Settings change (rhmiapp/settings_watch.hpp).
#include "../rhmiapp/settings_watch.hpp"

#include <stdio.h>
#include <string.h>

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

static const char* g_set[8];
static size_t g_nset = 0;
static bool fakeSet(const char* n) {
    for (size_t i = 0; i < g_nset; ++i) if (!strcmp(g_set[i], n)) return true;
    return false;
}

int main() {
    const char* names[] = {"autostart", "wifi-direct", "native-audio", "phone-gps"};
    const size_t n = 4;

    g_nset = 0; g_set[g_nset++] = "autostart";
    std::string a = nbt::settingsSnapshot(names, n, fakeSet);
    CHECK(a == "1000");

    // nothing touched
    CHECK(nbt::settingsDiff(names, n, a, nbt::settingsSnapshot(names, n, fakeSet)).empty());

    // ticked and unticked again on the same visit: no change
    g_set[g_nset++] = "wifi-direct"; --g_nset;
    CHECK(nbt::settingsDiff(names, n, a, nbt::settingsSnapshot(names, n, fakeSet)).empty());

    // one on, one off
    g_nset = 0; g_set[g_nset++] = "wifi-direct";
    std::string b = nbt::settingsSnapshot(names, n, fakeSet);
    CHECK(b == "0100");
    CHECK(nbt::settingsDiff(names, n, a, b) == "autostart off, wifi-direct on");

    // length mismatch counts as a change
    CHECK(!nbt::settingsDiff(names, n, "10", b).empty());
    CHECK(nbt::settingsDiff(names, n, "", "").empty());

    // decision
    CHECK(nbt::settingsRestartDecide(false, true, true, false, false) == nbt::SR_NONE);
    CHECK(nbt::settingsRestartDecide(true, true, true, false, false) == nbt::SR_RESTART);
    CHECK(nbt::settingsRestartDecide(true, false, true, false, false) == nbt::SR_OFF);
    CHECK(nbt::settingsRestartDecide(true, true, false, false, false) == nbt::SR_NOT_RUNNING);
    CHECK(nbt::settingsRestartDecide(true, true, true, true, false) == nbt::SR_STARTED_SINCE);
    CHECK(nbt::settingsRestartDecide(true, true, true, false, true) == nbt::SR_BUSY);

    if (fails) { printf("%d check(s) failed\n", fails); return 1; }
    printf("test_settings_watch: all checks passed\n");
    return 0;
}
