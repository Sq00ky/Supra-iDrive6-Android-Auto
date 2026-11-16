// Real AASession parsing/reassembly/ACK dispatch, scripted phone, stub TLS.
#include "aa_session.hpp"
#include "cluster/video/router.hpp"
#include "cluster/video/bridge.hpp"
#include "cluster/video/credits.hpp"
#include <assert.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>
#include <poll.h>
#include <stdio.h>
#include <vector>
using namespace aa_cluster;
using Bytes = std::vector<uint8_t>;
static Router router;
static unsigned stream_epoch, queued_frames, main_frames, main_configs, last_queued_size;
static ClusterAck credit_callback;
static Credits credits;
extern "C" void idriver_cluster_configure_acks(ClusterAck cb) {
    credit_callback = cb;
}
extern "C" unsigned idriver_cluster_ack_window() {
    return router.enabled ? 4 : 0;
}
extern "C" void idriver_cluster_drain_acks(void *session) {
    unsigned completed_frames = credits.take(stream_epoch);
    while (completed_frames--) {
        credit_callback(session, 2);
    }
}
extern "C" void idriver_cluster_tick(void *session) {
    router.tick(session);
}
static bool decoder_ready() {
    return true;
}
static bool decoder_needs_resync() {
    return false;
}
static uint32_t current_time() {
    return 0;
}
static unsigned advance_epoch(bool active) {
    stream_epoch = ((stream_epoch + 2) & ~1u) | active;
    return stream_epoch;
}
static bool enqueue_frame(unsigned, const uint8_t *config, unsigned config_size,
                          const uint8_t *payload, unsigned size, uint64_t timestamp) {
    assert(config_size == 6 && config[4] == 0x67 && size == 6 && payload[4] == 0x65 &&
           timestamp == 0x16500000002ULL);
    last_queued_size = config_size + size;
    __sync_fetch_and_add(&queued_frames, 1);
    return true;
}
extern "C" void idriver_cluster_init(ClusterSetup setup_callback, ClusterFocus focus_callback) {
    router.hooks = {setup_callback, focus_callback, decoder_ready, decoder_needs_resync,
                    advance_epoch,  enqueue_frame,  current_time};
    router.enabled = router.allowed = true;
}
extern "C" int idriver_cluster_receive(void *session, unsigned channel, const uint8_t *payload,
                                       size_t size) {
    return router.receive(session, channel, payload, size) ? 1 : (router.deferred ? 2 : 0);
}
extern "C" void idriver_cluster_end() {
    router.end();
}
extern "C" const uint8_t *idriver_cluster_manifest(const uint8_t *payload, size_t) {
    return payload;
}
static void read_exactly(int descriptor, void *out, size_t size) {
    size_t at = 0;
    while (at < size) {
        ssize_t r = read(descriptor, (char *)out + at, size - at);
        assert(r > 0);
        at += r;
    }
}
static void send_packet(int descriptor, unsigned channel, const Bytes &payload, unsigned flags = 11,
                        unsigned total = 0) {
    uint8_t header_bytes[8] = {uint8_t(channel),
                               uint8_t(flags),
                               uint8_t(payload.size() >> 8),
                               uint8_t(payload.size()),
                               0,
                               0,
                               0,
                               0};
    put32(header_bytes + 4, total);
    unsigned size = (flags & 3) == 1 ? 8 : 4;
    assert(write(descriptor, header_bytes, size) == size);
    assert(write(descriptor, payload.data(), payload.size()) == ssize_t(payload.size()));
}
static Bytes read_reply(int descriptor, unsigned channel) {
    uint8_t header_bytes[4];
    read_exactly(descriptor, header_bytes, 4);
    assert(header_bytes[0] == channel);
    Bytes message(header_bytes[2] * 256 + header_bytes[3]);
    read_exactly(descriptor, message.data(), message.size());
    return message;
}
static void expect_ack(int descriptor, unsigned channel, unsigned session) {
    Bytes message = read_reply(descriptor, channel);
    assert(message.size() >= 6 && message[0] == 0x80 && message[1] == 4 &&
           field(message.data() + 2, message.size() - 2, 1, 99) == session &&
           field(message.data() + 2, message.size() - 2, 2, 99) == 1);
}
static void main_frame(uint32_t, const uint8_t *, size_t, uint64_t, void *) {
    __sync_fetch_and_add(&main_frames, 1);
}
static void main_config(const uint8_t *, size_t, void *) {
    __sync_fetch_and_add(&main_configs, 1);
}
int main() {
    alarm(15);
    signal(SIGPIPE, SIG_IGN);
    int phone_sockets[2];
    assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, phone_sockets));
    nbt::AASession session;
    session.setVideoFrameCallback(main_frame);
    session.setCodecConfigCallback(main_config);
    assert(session.start(phone_sockets[0]));
    uint8_t version[12];
    read_exactly(phone_sockets[1], version, 12);
    for (unsigned channel : {2u, 6u}) {
        send_packet(phone_sockets[1], channel, {0x80, 0});
        Bytes setup = read_reply(phone_sockets[1], channel),
              focus = read_reply(phone_sockets[1], channel);
        assert(setup[0] == 0x80 && setup[1] == 3);
        assert(focus[0] == 0x80 && focus[1] == 8 && focus[3] == 1);
        assert(field(setup.data() + 2, setup.size() - 2, 2, 0) == (channel == 2 ? 4u : 10u));
        send_packet(phone_sockets[1], channel, {0x80, 1, 8, uint8_t(channel == 2 ? 7 : 9)});
    }
    Bytes config = {0, 1, 0, 0, 0, 1, 0x67, 42};
    send_packet(phone_sockets[1], 2, config);
    expect_ack(phone_sockets[1], 2, 7);
    send_packet(phone_sockets[1], 6, config);
    expect_ack(phone_sockets[1], 6, 9);
    Bytes frame = {0, 0, 0, 0, 1, 0x65, 0, 0, 0, 2, 0, 0, 0, 1, 0x65, 99};
    // Split inside the timestamp; no consumer may see an incomplete access unit.
    send_packet(phone_sockets[1], 2, Bytes(frame.begin(), frame.begin() + 7), 9, frame.size());
    send_packet(phone_sockets[1], 2, Bytes(frame.begin() + 7, frame.end()), 10);
    struct pollfd waiting = {phone_sockets[1], POLLIN, 0};
    assert(poll(&waiting, 1, 100) == 0);
    // The cluster consumer is deliberately slow: main video must still be
    // dispatched and acknowledged while the cluster has no returned credit.
    send_packet(phone_sockets[1], 6, frame);
    expect_ack(phone_sockets[1], 6, 9);
    assert(credits.publish(stream_epoch));
    expect_ack(phone_sockets[1], 2, 7);
    assert(__sync_fetch_and_add(&queued_frames, 0) == 1 &&
           __sync_fetch_and_add(&main_frames, 0) == 1 &&
           __sync_fetch_and_add(&main_configs, 0) == 1);
    // A late completion from the previous cluster stream must not ACK the
    // replacement phone session, even though both use the same channel.
    unsigned old_epoch = stream_epoch;
    send_packet(phone_sockets[1], 2, {0x80, 1, 8, 11});
    send_packet(phone_sockets[1], 2, config);
    expect_ack(phone_sockets[1], 2, 11);
    assert(credits.publish(old_epoch));
    waiting.revents = 0;
    assert(poll(&waiting, 1, 100) == 0);
    send_packet(phone_sockets[1], 2, frame);
    send_packet(phone_sockets[1], 6, frame);
    expect_ack(phone_sockets[1], 6, 9);
    assert(credits.publish(stream_epoch));
    expect_ack(phone_sockets[1], 2, 11);
    assert(__sync_fetch_and_add(&queued_frames, 0) == 2);
    send_packet(phone_sockets[1], 2, {0x80, 7, 0x10, 2});
    Bytes focus = read_reply(phone_sockets[1], 2);
    assert(focus[3] == 2);
    send_packet(phone_sockets[1], 2, {0x80, 2});
    session.stop();
    close(phone_sockets[1]);
    assert(!(stream_epoch & 1) && !router.opened && last_queued_size == 12);
    puts("AA_SOURCE_SESSION_OK independent streams, fragmentation, deferred cluster credits, main "
         "isolation, session-specific ACKs, focus and teardown");
}
