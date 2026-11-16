#pragma once

#include "protocol.hpp"
#include "atomic.hpp"
#include <string.h>

namespace aa_cluster {

// SPSC: the protocol thread publishes complete access units; the socket worker
// releases them. Small P-frames share the same 4 MiB budget as large IDRs.
class EncodedQueue {
public:
    struct Item {
        unsigned epoch, len, offset;
        uint64_t pts;
    };

private:
    Item items_[QueueSize];
    uint8_t bytes_[QueueBytes];
    // Monotonic unsigned counters make wraparound subtraction work for both
    // rings. Only the producer writes head_/produced_; only the consumer writes
    // tail_/consumed_. An item stays valid until the consumer calls pop().
    unsigned head_, tail_, produced_, consumed_;

    void copy(unsigned offset, const uint8_t* source, unsigned size) {
        if (!size) {
            return;
        }
        offset %= QueueBytes;
        unsigned first = QueueBytes - offset;
        if (first > size) {
            first = size;
        }
        memcpy(bytes_ + offset, source, first);
        if (size > first) {
            memcpy(bytes_, source + first, size - first);
        }
    }

public:
    EncodedQueue() : head_(0), tail_(0), produced_(0), consumed_(0) {}

    bool push(unsigned epoch, const uint8_t* config, unsigned config_size, const uint8_t* frame,
              unsigned frame_size, uint64_t pts) {
        if (frame_size > MaxPayload || config_size > MaxPayload - frame_size) {
            return false;
        }
        const unsigned head = atomic_load(&head_);
        const unsigned produced = atomic_load(&produced_);
        const unsigned size = config_size + frame_size;
        if (!size || head - atomic_load(&tail_) >= QueueSize ||
            size > QueueBytes - (produced - atomic_load(&consumed_))) {
            return false;
        }
        copy(produced, config, config_size);
        copy(produced + config_size, frame, frame_size);
        items_[head % QueueSize] = {epoch, size, produced % QueueBytes, pts};
        atomic_store(&produced_, produced + size);
        // Publish the item only after both byte spans and its metadata exist.
        atomic_store(&head_, head + 1);
        return true;
    }

    const Item* front() const {
        const unsigned tail = atomic_load(&tail_);
        return tail == atomic_load(&head_) ? 0 : &items_[tail % QueueSize];
    }

    const uint8_t* data(unsigned offset) const {
        return bytes_ + offset;
    }

    // The caller must hold a nonempty front() item. Reclaim its bytes before
    // making its metadata slot available to the producer.
    void pop() {
        const unsigned tail = atomic_load(&tail_);
        atomic_store(&consumed_, atomic_load(&consumed_) + items_[tail % QueueSize].len);
        atomic_store(&tail_, tail + 1);
    }

    void discard() {
        while (front()) {
            pop();
        }
    }
};

} // namespace aa_cluster
