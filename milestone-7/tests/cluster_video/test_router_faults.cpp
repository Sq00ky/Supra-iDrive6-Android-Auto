// Exercise failure transitions without a decoder, QNX or a vehicle.
#include "cluster/video/router.hpp"
#include "cluster/video/credits.hpp"
#include <assert.h>
#include <stdio.h>
#include <vector>
using namespace aa_cluster;

static bool decoder_available, projected;
static unsigned stream_epoch, queued_frames, focus_calls;
static uint32_t clock_ms;
static void setup(void *, unsigned channel) {
    assert(channel == 2);
}
static void focus(void *, unsigned channel, bool on, bool) {
    assert(channel == 2);
    projected = on;
    ++focus_calls;
}
static bool decoder_ready() {
    return decoder_available;
}
static bool decoder_needs_resync() {
    return false;
}
static unsigned advance_epoch(bool active) {
    return stream_epoch = ((stream_epoch + 2) & ~1u) | active;
}
static uint32_t current_time() {
    return clock_ms;
}
static bool enqueue(unsigned epoch, const uint8_t *, unsigned nc, const uint8_t *, unsigned n,
                    uint64_t) {
    assert(epoch == stream_epoch && (epoch & 1));
    assert(nc + n <= MaxPayload);
    ++queued_frames;
    return true;
}
static const uint8_t setup_msg[] = {0x80, 0}, start_msg[] = {0x80, 1, 8, 7};
static const uint8_t config[] = {0, 1, 0, 0, 0, 1, 0x67, 1, 0, 0, 0, 1, 0x68, 2};
static const uint8_t idr[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0x65, 99};
static const uint8_t predicted[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 1, 0x41, 99};
static Router start_session() {
    decoder_available = true;
    projected = false;
    stream_epoch = queued_frames = focus_calls = clock_ms = 0;
    Router router = {};
    router.enabled = router.allowed = true;
    router.hooks = {setup,         focus,   decoder_ready, decoder_needs_resync,
                    advance_epoch, enqueue, current_time};
    router.receive(0, 2, setup_msg, sizeof(setup_msg));
    router.receive(0, 2, start_msg, sizeof(start_msg));
    router.receive(0, 2, config, sizeof(config));
    return router;
}
static void test_decoder_disconnect_and_stale_credits(Credits &credits) {
    Router router = start_session();
    router.receive(0, 2, idr, sizeof(idr));
    assert(queued_frames == 1 && router.deferred);
    unsigned old_epoch = stream_epoch;
    // Decoder EOF must withdraw only cluster focus; its late completion cannot
    // grant a credit to the replacement stream on the same AA channel.
    decoder_available = false;
    router.tick(0);
    assert(!projected && !(stream_epoch & 1));
    router.receive(0, 2, idr, sizeof(idr));
    assert(queued_frames == 1 && !router.deferred);
    assert(!router.receive(0, 6, idr, sizeof(idr)) && queued_frames == 1);
    const uint8_t focus_release[] = {0x80, 7, 0x10, 2};
    router.receive(0, 2, focus_release, sizeof(focus_release));
    assert(!router.requested && router.resume_on_ready);
    decoder_available = true;
    router.tick(0);
    assert(projected && router.waiting_idr && router.requested);
    assert(credits.publish(old_epoch));
    assert(credits.take(stream_epoch) == 0);
    router.receive(0, 2, predicted, sizeof(predicted));
    assert(queued_frames == 1);
    router.receive(0, 2, idr, sizeof(idr));
    assert(queued_frames == 2 && router.deferred);
    assert(credits.publish(stream_epoch) && credits.take(stream_epoch) == 1);
    router.end();
}

static void test_rapid_setting_changes(Credits &credits) {
    Router router = start_session();
    unsigned old_epoch;
    for (unsigned i = 0; i < 32; ++i) {
        router.receive(0, 2, idr, sizeof(idr));
        unsigned previous_queued_frames = queued_frames;
        old_epoch = stream_epoch;
        router.allow(0, false);
        router.allow(0, true);
        router.allow(0, false);
        assert(!projected && !(stream_epoch & 1));
        // A frame arriving after the final off is acknowledged by stock
        // dispatch, with no deferred credit or new decoder work.
        router.receive(0, 2, idr, sizeof(idr));
        assert(!router.deferred && queued_frames == previous_queued_frames);
        unsigned previous_focus_calls = focus_calls;
        clock_ms += 10000;
        router.tick(0);
        assert(focus_calls == previous_focus_calls && !projected);
        router.allow(0, true);
        assert(projected && router.waiting_idr);
        assert(credits.publish(old_epoch) && credits.take(stream_epoch) == 0);
        router.receive(0, 2, predicted, sizeof(predicted));
        assert(queued_frames == previous_queued_frames);
    }
    router.end();
}

static void test_malformed_frames_and_recovery() {
    Router router = start_session();
    // Truncated AA timestamps/config and oversized payloads must not be queued.
    for (size_t n = 0; n <= 10; ++n) {
        router.receive(0, 2, idr, n);
        assert(!router.deferred && queued_frames == 0);
    }
    router.receive(0, 2, 0, 100);
    assert(!router.deferred && queued_frames == 0);
    std::vector<uint8_t> oversized_payload(MaxPayload + 11, 0);
    router.receive(0, 2, oversized_payload.data(), oversized_payload.size());
    assert(!router.deferred && queued_frames == 0);
    oversized_payload.assign(MaxConfig + 3, 0);
    oversized_payload[1] = 1;
    router.receive(0, 2, oversized_payload.data(), oversized_payload.size());
    assert(router.config_size == 0 && queued_frames == 0);
    // A valid config/IDR pair still works after rejecting malformed input.
    router.receive(0, 2, config, sizeof(config));
    router.receive(0, 2, idr, sizeof(idr));
    assert(queued_frames == 1 && router.deferred);
    router.end();
}

static void test_protocol_bounds() {
    uint8_t wire_header[HeaderSize];
    header(wire_header, Video, 1, MaxPayload, 0);
    assert(valid_header(wire_header));
    header(wire_header, Video, 1, MaxPayload + 1, 0);
    assert(!valid_header(wire_header));
    header(wire_header, Video, 1, 0, 0);
    assert(!valid_header(wire_header));
    header(wire_header, Reset, 1, 1, 0);
    assert(!valid_header(wire_header));
    header(wire_header, Stop, 1, 0, 0);
    wire_header[0] ^= 1;
    assert(!valid_header(wire_header));
    header(wire_header, 99, 1, 0, 0);
    assert(!valid_header(wire_header));
    const uint8_t truncated_varint[] = {0x10, 0x80};
    const uint8_t truncated_bytes[] = {0x0a, 10, 1, 2};
    const uint8_t unterminated[] = {0x10, 0x80, 0x80, 0x80, 0x80, 0x80,
                                    0x80, 0x80, 0x80, 0x80, 0x80};
    assert(field(truncated_varint, sizeof(truncated_varint), 2, 99) == 99);
    assert(field(truncated_bytes, sizeof(truncated_bytes), 2, 99) == 99);
    assert(field(unterminated, sizeof(unterminated), 2, 99) == 99);
}

int main() {
    Credits credits = {};
    test_decoder_disconnect_and_stale_credits(credits);
    test_rapid_setting_changes(credits);
    test_malformed_frames_and_recovery();
    test_protocol_bounds();
    puts("AA_ROUTER_FAULTS_OK disconnect, rapid toggle, stale credits, malformed input, recovery");
}
