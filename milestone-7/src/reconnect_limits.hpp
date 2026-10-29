// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef NBT_RECONNECT_LIMITS_HPP
#define NBT_RECONNECT_LIMITS_HPP
#include <string.h>
namespace nbt {
// Each failure has already exhausted the wireless setup/TCP grace period.
// Daemon's minimum matches the fast app profile. The conservative app waits
// longer before requesting recovery; both retain the same TCP safety guards.
enum { kReconnectFailedSetups = 2, kReconnectMissingSppSeconds = 3,
       kReconnectQuietSeconds = 3, kReconnectPowerSettleSeconds = 10 };

struct ReconnectTiming {
    explicit ReconnectTiming(bool slow = false)
        : conservative(slow), autostartSeconds(slow ? 15 : 3),
          missingSppSeconds(slow ? 10 : kReconnectMissingSppSeconds),
          quietSeconds(slow ? 10 : kReconnectQuietSeconds) {}

    // Absent profile = fast; malformed/unknown content = conservative.
    static ReconnectTiming fromProfileName(const char* name) {
        return ReconnectTiming(name && strcmp(name, "fast") != 0);
    }

    bool conservative;
    unsigned autostartSeconds, missingSppSeconds, quietSeconds;
};

inline unsigned reconnectBoundedSeconds(unsigned seconds) {
    return seconds < 3 ? 3 : (seconds > 10 ? 10 : seconds);
}
}
#endif
