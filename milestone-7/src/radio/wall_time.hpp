// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#pragma once
// Wall-clock time for the radio log lines: the daemon log has no timestamps, and the tester marks events with the car's
// `date`.
#include <string.h>
#include <time.h>

namespace nbt {

// "HH:MM:SS" of the local time, the clock of the car's `date`: printf("%s", WallTime().hms).
struct WallTime {
    char hms[9];
    WallTime() {
        const time_t t = time(NULL);
        struct tm tm;
        if (!localtime_r(&t, &tm) || strftime(hms, sizeof(hms), "%H:%M:%S", &tm) == 0) strcpy(hms, "--:--:--");
    }
};

}  // namespace nbt
