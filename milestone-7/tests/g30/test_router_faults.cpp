// Exercise failure transitions without a decoder, QNX or a vehicle.
#include "cluster/g30/router.hpp"
#include "cluster/g30/credits.hpp"
#include <assert.h>
#include <stdio.h>
#include <vector>
using namespace aa_cluster;

static bool available, projected;
static unsigned generation, queued, focus_calls;
static uint32_t clock_ms;
static void setup(void*, unsigned channel) { assert(channel == 2); }
static void focus(void*, unsigned channel, bool on, bool) {
    assert(channel == 2); projected = on; ++focus_calls;
}
static bool ready() { return available; }
static bool resync() { return false; }
static unsigned state(bool active) { return generation = ((generation + 2) & ~1u) | active; }
static uint32_t now() { return clock_ms; }
static bool enqueue(unsigned epoch, const uint8_t*, unsigned nc,
                    const uint8_t*, unsigned n, uint64_t) {
    assert(epoch == generation && (epoch & 1));
    assert(nc + n <= MaxPayload); ++queued; return true;
}
static const uint8_t setup_msg[] = {0x80, 0}, start_msg[] = {0x80, 1, 8, 7};
static const uint8_t config[] = {0, 1, 0, 0, 0, 1, 0x67, 1, 0, 0, 0, 1, 0x68, 2};
static const uint8_t idr[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0x65, 99};
static const uint8_t predicted[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 1, 0x41, 99};
static Router session() {
    available = true; projected = false; generation = queued = focus_calls = clock_ms = 0;
    Router r = {}; r.enabled = r.allowed = true;
    r.hooks = {setup, focus, ready, resync, state, enqueue, now};
    r.receive(0, 2, setup_msg, sizeof(setup_msg));
    r.receive(0, 2, start_msg, sizeof(start_msg));
    r.receive(0, 2, config, sizeof(config));
    return r;
}
int main() {
    Router r = session();
    r.receive(0, 2, idr, sizeof(idr)); assert(queued == 1 && r.deferred);
    unsigned old_epoch = generation;
    // Decoder EOF must withdraw only cluster focus; its late completion cannot
    // grant a credit to the replacement stream on the same AA channel.
    available = false; r.tick(0);
    assert(!projected && !(generation & 1));
    r.receive(0, 2, idr, sizeof(idr)); assert(queued == 1 && !r.deferred);
    assert(!r.receive(0, 6, idr, sizeof(idr)) && queued == 1);
    available = true; r.tick(0); assert(projected && r.waiting_idr);
    Credits credits = {}; assert(credits.publish(old_epoch));
    assert(credits.take(generation) == 0);
    r.receive(0, 2, predicted, sizeof(predicted)); assert(queued == 1);
    r.receive(0, 2, idr, sizeof(idr)); assert(queued == 2 && r.deferred);
    assert(credits.publish(generation) && credits.take(generation) == 1);
    r.end();

    r = session();
    for (unsigned i = 0; i < 32; ++i) {
        r.receive(0, 2, idr, sizeof(idr)); unsigned before = queued;
        old_epoch = generation;
        r.allow(0, false); r.allow(0, true); r.allow(0, false);
        assert(!projected && !(generation & 1));
        // A frame arriving after the final off is acknowledged by stock
        // dispatch, with no deferred credit or new decoder work.
        r.receive(0, 2, idr, sizeof(idr)); assert(!r.deferred && queued == before);
        unsigned calls = focus_calls;
        clock_ms += 10000; r.tick(0);
        assert(focus_calls == calls && !projected);
        r.allow(0, true); assert(projected && r.waiting_idr);
        assert(credits.publish(old_epoch) && credits.take(generation) == 0);
        r.receive(0, 2, predicted, sizeof(predicted)); assert(queued == before);
    }
    r.end();

    r = session();
    // Truncated AA timestamps/config and oversized payloads must not be queued.
    for (size_t n = 0; n <= 10; ++n) {
        r.receive(0, 2, idr, n); assert(!r.deferred && queued == 0);
    }
    r.receive(0, 2, 0, 100); assert(!r.deferred && queued == 0);
    std::vector<uint8_t> big(MaxPayload + 11, 0);
    r.receive(0, 2, big.data(), big.size()); assert(!r.deferred && queued == 0);
    big.assign(MaxConfig + 3, 0); big[1] = 1;
    r.receive(0, 2, big.data(), big.size()); assert(r.config_size == 0 && queued == 0);
    // A valid config/IDR pair still works after rejecting malformed input.
    r.receive(0, 2, config, sizeof(config));
    r.receive(0, 2, idr, sizeof(idr)); assert(queued == 1 && r.deferred);
    r.end();

    uint8_t h[HeaderSize];
    header(h, Video, 1, MaxPayload, 0); assert(valid_header(h));
    header(h, Video, 1, MaxPayload + 1, 0); assert(!valid_header(h));
    header(h, Video, 1, 0, 0); assert(!valid_header(h));
    header(h, Reset, 1, 1, 0); assert(!valid_header(h));
    header(h, Stop, 1, 0, 0); h[0] ^= 1; assert(!valid_header(h));
    header(h, 99, 1, 0, 0); assert(!valid_header(h));
    const uint8_t truncated_varint[] = {0x10, 0x80};
    const uint8_t truncated_bytes[] = {0x0a, 10, 1, 2};
    const uint8_t unterminated[] = {0x10, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80};
    assert(field(truncated_varint, sizeof(truncated_varint), 2, 99) == 99);
    assert(field(truncated_bytes, sizeof(truncated_bytes), 2, 99) == 99);
    assert(field(unterminated, sizeof(unterminated), 2, 99) == 99);
    puts("AA_ROUTER_FAULTS_OK disconnect, rapid toggle, stale credits, malformed input, recovery");
}
