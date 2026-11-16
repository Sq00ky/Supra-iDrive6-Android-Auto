#pragma once
#include <stdint.h>

namespace aa_cluster {

// Runtime failures retry at 1, 2, 4, 8, 16, then 30 seconds. A stable helper
// resets the backoff. Configuration/ownership failures require intervention.
struct Recovery {
    unsigned failures;
    uint32_t retry_at, started_at;
    bool waiting, blocked;
    Recovery() : failures(0), retry_at(0), started_at(0), waiting(false), blocked(false) {}

    bool ready(uint32_t now) const {
        return !blocked && (!waiting || int32_t(now - retry_at) >= 0);
    }
    void started(uint32_t now) { started_at = now; waiting = false; }
    void healthy(uint32_t now) {
        if (uint32_t(now - started_at) >= 30000) failures = 0;
    }
    unsigned failed(uint32_t now, bool recoverable) {
        blocked = !recoverable;
        if (blocked) return 0;
        unsigned delay = failures < 5 ? (1000u << failures) : 30000u;
        if (failures < 5) ++failures;
        retry_at = now + delay;
        waiting = true;
        return delay;
    }
};

// A large access unit may take time while still making progress. An idle
// deadline detects a stalled peer; the total deadline bounds trickle traffic.
struct IoDeadline {
    uint32_t began, progressed;
    unsigned idle_ms;
    IoDeadline(uint32_t now, unsigned idle) : began(now), progressed(now), idle_ms(idle) {}
    void progress(uint32_t now) { progressed = now; }
    bool expired(uint32_t now) const {
        return uint32_t(now - progressed) >= idle_ms ||
               uint32_t(now - began) >= idle_ms * 5u;
    }
};
} // namespace aa_cluster
