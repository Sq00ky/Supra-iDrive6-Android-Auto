// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#pragma once
// Wall-clock time for the radio log lines: the daemon log has no timestamps, and the tester marks events with the car's
// `date`.
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

namespace nbt {

// "HH:MM:SS" and "HH:MM:SS.mmm" of the local time, the clock of the car's `date`: printf("%s", WallTime().hms).
struct WallTime {
    char hms[9];
    char hmsMs[13];
    WallTime() {
        struct timeval tv;
        struct tm tm;
        if (gettimeofday(&tv, NULL) != 0 || !localtime_r(&tv.tv_sec, &tm) ||
            strftime(hms, sizeof(hms), "%H:%M:%S", &tm) == 0) {
            strcpy(hms, "--:--:--");
            strcpy(hmsMs, "--:--:--.---");
            return;
        }
        snprintf(hmsMs, sizeof(hmsMs), "%s.%03d", hms, (int)(tv.tv_usec / 1000));
    }
};

}  // namespace nbt
