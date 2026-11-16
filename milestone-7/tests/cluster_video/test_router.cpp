#include "cluster/video/router.hpp"
#include "cluster/video/credits.hpp"
#include <assert.h>
#include <stdio.h>
#include <vector>
using namespace aa_cluster;
static unsigned setup_calls, focus_calls, last_channel, last_epoch, last_queued_size;
static bool projected, decoder_available = true, queue_full = false, decoder_needs_resync = false;
static uint64_t queued_timestamp;
static std::vector<uint8_t> queued_bytes;
static uint32_t clock_ms;
static uint32_t current_time() {
    return clock_ms;
}
static void setup(void *, unsigned channel) {
    ++setup_calls;
    last_channel = channel;
}
static void focus(void *, unsigned channel, bool has_focus, bool) {
    ++focus_calls;
    last_channel = channel;
    projected = has_focus;
}
static bool decoder_ready() {
    return decoder_available;
}
static bool take_resync_request() {
    bool requested = decoder_needs_resync;
    decoder_needs_resync = false;
    return requested;
}
static unsigned advance_epoch(bool active) {
    last_epoch = ((last_epoch + 2) & ~1u) | active;
    return last_epoch;
}
static bool enqueue_frame(unsigned epoch, const uint8_t *config, unsigned config_size,
                          const uint8_t *payload, unsigned payload_size, uint64_t timestamp) {
    assert(epoch == last_epoch);
    if (queue_full) {
        return false;
    }
    last_queued_size = config_size + payload_size;
    queued_timestamp = timestamp;
    queued_bytes.assign(config, config + config_size);
    queued_bytes.insert(queued_bytes.end(), payload, payload + payload_size);
    return true;
}
static void test_credit_window() {
    Credits credits = {};
    for (unsigned i = 0; i < QueueSize; ++i) {
        assert(credits.publish(i < 3 ? 7 : 9));
    }
    assert(!credits.publish(9));
    assert(credits.take(9) == QueueSize - 3);
    assert(!credits.take(9));
    assert(credits.publish(9));
    assert(!credits.take(11)); // old stream's late worker completion is discarded
    assert(credits.publish(11) && credits.take(11) == 1);
}

int main() {
    test_credit_window();

    Router router = {};
    router.enabled = router.allowed = true;
    router.hooks = {setup,         focus,         decoder_ready, take_resync_request,
                    advance_epoch, enqueue_frame, current_time};
    const uint8_t setup_message[] = {0x80, 0}, start_message[] = {0x80, 1, 8, 7},
                  stop_message[] = {0x80, 2}, unfocus_message[] = {0x80, 7, 0x10, 2};
    const uint8_t codec_config[] = {0, 1, 0, 0, 0, 1, 0x67, 1, 0, 0, 0, 1, 0x68, 2};
    // The timestamp deliberately contains a start-code pattern. It isn't H264.
    uint8_t frame[] = {0, 0, 0, 0, 1, 0x65, 0, 0, 0, 2, 0, 0, 0, 1, 0x65, 99};
    // Setup and video on the main channel cannot acquire cluster ownership.
    assert(!router.receive(0, 6, setup_message, sizeof(setup_message)) && setup_calls == 0);
    assert(router.receive(0, 2, setup_message, sizeof(setup_message)) && setup_calls == 1 &&
           last_channel == 2 && projected);
    assert(!router.receive(0, 2, start_message, sizeof(start_message)));
    assert(!router.receive(0, 2, codec_config, sizeof(codec_config)));
    // An IDR is required before predicted slices can enter the decoder.
    frame[14] = 0x41;
    router.receive(0, 2, frame, sizeof(frame));
    assert(!last_queued_size);
    frame[14] = 0x65;
    router.receive(0, 2, frame, sizeof(frame));
    assert(last_queued_size == sizeof(codec_config) - 2 + 6);
    assert(queued_timestamp == be64(frame + 2) && queued_bytes[4] == 0x67 &&
           queued_bytes.back() == 99);
    unsigned previous_epoch = last_epoch;
    router.receive(0, 6, frame, sizeof(frame));
    assert(last_epoch == previous_epoch);
    // Overflow invalidates the predictive chain and requires a fresh IDR+config.
    queue_full = true;
    router.receive(0, 2, frame, sizeof(frame));
    assert(router.waiting_idr && last_epoch != previous_epoch);
    queue_full = false;
    last_queued_size = 0;
    frame[14] = 0x41;
    router.receive(0, 2, frame, sizeof(frame));
    assert(!last_queued_size);
    clock_ms += 500;
    frame[14] = 0x65;
    router.receive(0, 2, frame, sizeof(frame));
    assert(last_queued_size == 18);
    // Overflow again before the 10 s retry throttle expires. Main/control
    // traffic must eventually restart recovery even with no incoming IDR.
    clock_ms = 600;
    queue_full = true;
    router.receive(0, 2, frame, sizeof(frame));
    queue_full = false;
    assert(router.recovery_needed && !router.recovery_pending && router.waiting_idr);
    clock_ms = 10000;
    router.receive(0, 6, start_message, sizeof(start_message));
    assert(router.recovery_pending && !projected);
    clock_ms = 10500;
    router.receive(0, 2, frame, sizeof(frame));
    assert(projected && !router.waiting_idr);
    // Phone focus/stop messages retire the active stream before reopening.
    assert(router.receive(0, 2, unfocus_message, sizeof(unfocus_message)) && !projected &&
           !(last_epoch & 1));
    router.receive(0, 2, frame, sizeof(frame));
    assert(router.waiting_idr);
    router.receive(0, 2, stop_message, sizeof(stop_message));
    router.end();
    assert(!(last_epoch & 1) && !router.opened);
    decoder_available = false;
    router.receive(0, 2, setup_message, sizeof(setup_message));
    assert(!projected);
    decoder_available = true;
    router.receive(0, 6, start_message, sizeof(start_message));
    assert(projected && last_channel == 2);
    // A settings change is applied by the idle tick: no incoming video, no
    // reconnect, no main-channel focus changes. Late phone packets stay ACKable.
    router.allow(0, false);
    assert(!projected && !(last_epoch & 1));
    previous_epoch = last_epoch;
    router.receive(0, 2, start_message, sizeof(start_message));
    router.receive(0, 2, frame, sizeof(frame));
    assert(!router.deferred && !(last_epoch & 1));
    router.receive(0, 2, unfocus_message, sizeof(unfocus_message));
    router.allow(0, true);
    assert(projected && router.waiting_idr);
    router.receive(0, 2, start_message, sizeof(start_message));
    router.receive(0, 2, codec_config, sizeof(codec_config));
    frame[14] = 0x41;
    router.receive(0, 2, frame, sizeof(frame));
    assert(!router.deferred);
    frame[14] = 0x65;
    router.receive(0, 2, frame, sizeof(frame));
    assert(router.deferred);
    router.allow(0, false);
    assert(!projected && !(last_epoch & 1));
    decoder_available = false;
    router.allow(0, true);
    assert(!projected);
    decoder_available = true;
    router.tick(0);
    assert(projected && router.waiting_idr);
    router.end();
    puts("AA_ROUTER_TEST_OK main isolation, focus, framing, IDR recovery, lifecycle");
}
