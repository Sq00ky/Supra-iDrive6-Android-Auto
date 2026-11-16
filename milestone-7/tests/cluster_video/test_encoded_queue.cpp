#include "cluster/video/encoded_queue.hpp"
#include <assert.h>
#include <stdio.h>
#include <vector>
using namespace aa_cluster;
static EncodedQueue encoded_queue;
static void expect_front_frame(unsigned epoch, const std::vector<uint8_t> &bytes) {
    const EncodedQueue::Item *item = encoded_queue.front();
    assert(item && item->epoch == epoch && item->len == bytes.size());
    for (unsigned byte_offset = 0; byte_offset < item->len; ++byte_offset) {
        assert(encoded_queue.data((item->offset + byte_offset) % QueueBytes)[0] ==
               bytes[byte_offset]);
    }
    encoded_queue.pop();
}
int main() {
    std::vector<uint8_t> small_frame(256, 71), maximum_frame(MaxPayload, 0);
    for (unsigned item = 0; item < maximum_frame.size(); ++item) {
        maximum_frame[item] = uint8_t(item * 37);
    }
    for (unsigned item = 0; item < QueueSize; ++item) {
        assert(encoded_queue.push(item, 0, 0, small_frame.data(), small_frame.size(), item));
    }
    assert(!encoded_queue.push(1, 0, 0, small_frame.data(), small_frame.size(), 0));
    for (unsigned item = 0; item < QueueSize; ++item) {
        expect_front_frame(item, small_frame);
    }
    // Large frames are still capped at 4 MiB, including wrapped allocations.
    for (unsigned pass = 0; pass < 3; ++pass) {
        for (unsigned item = 0; item < 8; ++item) {
            assert(encoded_queue.push(item, maximum_frame.data(), 17, maximum_frame.data() + 17,
                                      maximum_frame.size() - 17, item));
        }
        assert(!encoded_queue.push(1, 0, 0, small_frame.data(), small_frame.size(), 0));
        for (unsigned item = 0; item < 8; ++item) {
            expect_front_frame(item, maximum_frame);
        }
    }
    assert(!encoded_queue.push(1, 0, 0, maximum_frame.data(), MaxPayload + 1, 0));
    assert(encoded_queue.push(9, 0, 0, small_frame.data(), small_frame.size(), 0));
    encoded_queue.discard();
    assert(!encoded_queue.front());
    puts("AA_ENCODED_QUEUE_OK burst=256 bytes=4194304 wrapping=1 capacity_reuse=1");
}
