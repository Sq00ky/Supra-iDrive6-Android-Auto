// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "status_hint.hpp"
#include <stdio.h>
static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)
#define SAME(a, b) CHECK(!strcmp((a), (b)))
int main() {
    using namespace nbt;
    const int unknown = -1;
    // hints
    SAME(statusHint(0, 1000, 0, 0, unknown), "-");           // device list not in yet: no advice
    SAME(statusHint(4, 1000, 0, 0, unknown), "-");           // a few refusals are normal
    SAME(statusHint(5, 1000, 0, 0, unknown), "coding");
    SAME(statusHint(5, 1000, 990, 100, 3), "coding");        // coding wins over the others
    SAME(statusHint(5, 1000, 0, 0, 0), "coding");
    SAME(statusHint(0, 1000, 990, 0, unknown), "nojoin");
    SAME(statusHint(0, 1000, 1000 - kHintNoJoinS, 0, unknown), "-");   // expired
    SAME(statusHint(0, 1000, 990, 100, 3), "nojoin");
    SAME(statusHint(0, 1000, 0, 1000 - kHintNoBtS, 2), "bt");
    SAME(statusHint(0, 1000, 0, 1000 - kHintNoBtS + 1, 2), "-"); // not long enough
    SAME(statusHint(0, 1000, 0, 0, 3), "-");                 // phone connected, not waiting for the link
    SAME(statusHint(0, 1000, 0, 0, 0), "nophone");           // no phone connected over Bluetooth
    SAME(statusHint(0, 1000, 0, 100, 1), "nophone");         // paired and in range, not connected
    // texts
    CHECK(waitingKind(true, "wfd", "nophone", true, true) == kWaitUsb);
    CHECK(waitingKind(false, "wfd", "coding", true, true) == kWaitCoding);
    CHECK(waitingKind(false, "cp", "nojoin", true, true) == kWaitNoJoin);
    CHECK(waitingKind(false, "cp", "bt", true, true) == kWaitNoBt);
    CHECK(waitingKind(false, "cp", "nophone", true, true) == kWaitNoPhone);   // not "Waiting: CarPlay Wi-Fi"
    CHECK(waitingKind(false, "wfd", "-", true, true) == kWaitWfdNet);
    CHECK(waitingKind(false, "cp", "-", true, false) == kWaitCarplay);
    CHECK(waitingKind(false, "", "", true, false) == kWaitCarplay);     // older daemon: old chain
    CHECK(waitingKind(false, "", "", false, true) == kWaitEnableWfd);
    CHECK(waitingKind(false, "-", "-", false, false) == kWaitUsbOnly);
    printf("test_status_hint: %s\n", fails ? "FAILED" : "all passed");
    return fails ? 1 : 0;
}
