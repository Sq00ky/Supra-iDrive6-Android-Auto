#pragma once

#include "atomic.hpp"
#include "protocol.hpp"

namespace aa_cluster {

// Single producer (socket worker), single consumer (AA session thread).
// Epochs prevent a late completion from acknowledging a replacement stream.
struct Credits {
    unsigned head, tail, epochs[QueueSize];

    bool publish(unsigned epoch) {
        const unsigned next = atomic_load(&head);
        if (next - atomic_load(&tail) >= QueueSize) {
            return false;
        }
        epochs[next % QueueSize] = epoch;
        atomic_store(&head, next + 1);
        return true;
    }

    unsigned take(unsigned current) {
        unsigned next = atomic_load(&tail);
        const unsigned end = atomic_load(&head);
        unsigned count = 0;
        // Consume stale epochs too: they must neither send ACKs nor occupy
        // slots needed by completions from the replacement stream.
        while (next != end) {
            if (epochs[next % QueueSize] == current) {
                ++count;
            }
            atomic_store(&tail, ++next);
        }
        return count;
    }
};

} // namespace aa_cluster
