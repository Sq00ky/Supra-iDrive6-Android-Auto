// The production capture worker and Session run against a delayed Screen peer.
// Real threads and fragmented socket input expose lock/ownership mistakes that
// an instantaneous decoder mock cannot reproduce. Also runs in the QNX SDK VM.
#include "hardware_peer.hpp"
#include "cluster/video/hardware_capture.hpp"
#include <pthread.h>
#include <sys/socket.h>
#include <poll.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>

static const uint8_t access_unit[] = {0,0,0,1,0x67,1,2,0,0,1,0x68,3,4,0,0,0,1,0x65,5,6};
static uint64_t now_us() {
    timespec t = {}; assert(!clock_gettime(CLOCK_MONOTONIC, &t));
    return uint64_t(t.tv_sec) * 1000000 + t.tv_nsec / 1000;
}
static timespec deadline() {
    timespec t = {}; assert(!clock_gettime(CLOCK_REALTIME, &t)); t.tv_sec += 8; return t;
}

struct SlowPeer : HardwarePeer {
    pthread_mutex_t gate;
    pthread_cond_t changed;
    unsigned entered, permits, completed, stopped;
    bool in_read, fail_read;
    SlowPeer() : entered(0), permits(0), completed(0), stopped(0), in_read(false), fail_read(false) {
        assert(!pthread_mutex_init(&gate, 0)); assert(!pthread_cond_init(&changed, 0));
        allocation[0] = 896; allocation[1] = 576; origin[0] = 32; origin[1] = 24;
    }
    ~SlowPeer() { pthread_cond_destroy(&changed); pthread_mutex_destroy(&gate); }
    uint64_t now_us() { return ::now_us(); }
    void sleep_ms(unsigned ms) { usleep(ms * 1000); }
    int send_packet(int n, unsigned type, const void* data, unsigned size, const uint64_t* pts) {
        assert(n == 7);
        pthread_mutex_lock(&gate);
        packets.push_back(type);
        if (type == 3) {
            assert(pts && *pts > 0 && size >= sizeof(access_unit));
            // Every complete access unit arrives once, in order and unmodified.
            assert(!memcmp(data, access_unit, sizeof(access_unit)));
            if (size > sizeof(access_unit)) {
                const uint8_t* bytes = static_cast<const uint8_t*>(data);
                assert(aa_cluster::be32(bytes + size - 4) == input_count);
                for (unsigned i = sizeof(access_unit); i < size - 4; ++i)
                    assert(bytes[i] == uint8_t(input_count));
            }
            ++input_count; output_event = true;
        }
        pthread_mutex_unlock(&gate);
        return 0;
    }
    int screen_get_event(void* context, void* event, uint64_t timeout) {
        pthread_mutex_lock(&gate);
        const int result = HardwarePeer::screen_get_event(context, event, timeout);
        pthread_mutex_unlock(&gate);
        return result;
    }
    int read_picture(void* window, void*) {
        assert(window == child && capture_usage == 14);
        pthread_mutex_lock(&gate);
        in_read = true; ++entered; pthread_cond_broadcast(&changed);
        const timespec until = deadline();
        while (!permits) assert(!pthread_cond_timedwait(&changed, &gate, &until));
        --permits;
        const unsigned picture = input_count;
        const bool fail = fail_read;
        pthread_mutex_unlock(&gate);
        // Simulate the measured car readback latency; input must also continue
        // throughout this delay, not merely while the condition releases a lock.
        usleep(350000);
        if (!fail) {
            for (unsigned y = 0; y < P::ContentHeight; ++y)
                for (unsigned x = 0; x < P::FittedWidth; ++x) {
                    uint8_t* p = &pixels[y * capture_stride + x * 4];
                    p[0] = x % 256; p[1] = y % 256; p[2] = picture; p[3] = 255;
                }
        }
        pthread_mutex_lock(&gate);
        in_read = false; ++completed; pthread_cond_broadcast(&changed);
        pthread_mutex_unlock(&gate);
        return fail ? -1 : 0;
    }
    int mmr_stop(void* context) {
        pthread_mutex_lock(&gate); assert(!in_read); ++stopped; pthread_mutex_unlock(&gate);
        return HardwarePeer::mmr_stop(context);
    }
    void wait_read(unsigned count) {
        pthread_mutex_lock(&gate); const timespec until = deadline();
        while (entered < count) assert(!pthread_cond_timedwait(&changed, &gate, &until));
        pthread_mutex_unlock(&gate);
    }
    void release(unsigned count = 1, bool fail = false) {
        pthread_mutex_lock(&gate); permits += count; fail_read = fail;
        pthread_cond_broadcast(&changed); pthread_mutex_unlock(&gate);
    }
    void assert_inputs(unsigned count) {
        pthread_mutex_lock(&gate); assert(input_count == count); pthread_mutex_unlock(&gate);
    }
};

typedef aa_cluster::hardware::AsyncSession<SlowPeer> Decoder;
struct Writer { int fd; unsigned count, bytes; };
static void* write_fragments(void* opaque) {
    Writer& writer = *static_cast<Writer*>(opaque);
    std::vector<uint8_t> payload(writer.bytes, 0);
    memcpy(&payload[0], access_unit, sizeof(access_unit));
    for (unsigned sequence = 1; sequence <= writer.count; ++sequence) {
        memset(&payload[sizeof(access_unit)], uint8_t(sequence), payload.size() - sizeof(access_unit));
        aa_cluster::put32(&payload[payload.size() - 4], sequence);
        uint8_t header[aa_cluster::HeaderSize];
        aa_cluster::header(header, aa_cluster::Video, 1, writer.bytes, sequence);
        for (unsigned part = 0; part < 2; ++part) {
            const uint8_t* data = part ? &payload[0] : header;
            const unsigned size = part ? writer.bytes : sizeof(header);
            for (unsigned sent = 0; sent < size;) {
                unsigned chunk = size - sent; if (chunk > 997) chunk = 997;
                int n = send(writer.fd, data + sent, chunk, 0);
                assert(n > 0); sent += unsigned(n);
            }
        }
    }
    return 0;
}
// Same ordering as Player::read: poll the decoder before every socket fragment.
static void receive(Decoder& decoder, int fd, uint8_t* bytes, unsigned size, uint8_t* picture) {
    unsigned done = 0;
    while (done < size) {
        assert(decoder.poll(picture) == 0); // capture remains deliberately blocked
        pollfd ready = {fd, POLLIN, 0};
        assert(poll(&ready, 1, 1000) == 1 && (ready.revents & POLLIN));
        unsigned chunk = size - done; if (chunk > 613) chunk = 613;
        int n = recv(fd, bytes + done, chunk, 0); assert(n > 0); done += unsigned(n);
    }
}
static void* close_session(void* opaque) { static_cast<Decoder*>(opaque)->close(); return 0; }

int main() {
    alarm(25); // A blocking regression must fail, never hang the suite/SDK VM.
    typedef HardwarePeer::P P;
    std::vector<uint8_t> picture(P::ContentWidth * P::ContentHeight * 4);
    SlowPeer peer; Decoder decoder(peer);
    assert(decoder.open());
    assert(decoder.decode(access_unit, sizeof(access_unit), 0, &picture[0], true) == 0);
    peer.wait_read(1); // hold readback until ALL input has crossed the decoder boundary

    int sockets[2]; assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, sockets));
    int small = 4096; assert(!setsockopt(sockets[0], SOL_SOCKET, SO_SNDBUF, &small, sizeof(small)));
    Writer writer = {sockets[0], 24, 87039};
    pthread_t thread; assert(!pthread_create(&thread, 0, write_fragments, &writer));
    const uint64_t began = now_us();
    std::vector<uint8_t> payload(writer.bytes);
    for (unsigned i = 0; i < writer.count; ++i) {
        uint8_t header[aa_cluster::HeaderSize];
        receive(decoder, sockets[1], header, sizeof(header), &picture[0]);
        assert(aa_cluster::valid_header(header) && aa_cluster::be32(header + 12) == writer.bytes);
        receive(decoder, sockets[1], &payload[0], payload.size(), &picture[0]);
        assert(decoder.decode(&payload[0], payload.size(), i, &picture[0], true) == 0);
    }
    assert(!pthread_join(thread, 0));
    const uint64_t elapsed = now_us() - began;
    assert(elapsed < 3000000); // original local-transport idle deadline
    peer.assert_inputs(25);
    printf("PASS fragmented input: %u bytes, 24 reference frames in %llu us while readback blocked\n",
           writer.count * writer.bytes, (unsigned long long)elapsed);
    close(sockets[0]); close(sockets[1]);

    peer.release();
    peer.wait_read(2); // first result was published; worker is blocked again
    assert(decoder.poll(&picture[0], false) == 0); // retain while presentation is suppressed
    assert(decoder.poll(&picture[0]) == 1);
    assert(picture[(P::PaddingX + 1) * 4 + 2] == 25);
    assert(decoder.poll(&picture[0]) == 0); // consumed only once

    // New pictures replace old pending ones; no growing picture queue.
    assert(decoder.decode(access_unit, sizeof(access_unit), 0, &picture[0], true) == 0);
    peer.release(); peer.wait_read(3);
    assert(decoder.decode(access_unit, sizeof(access_unit), 0, &picture[0], true) == 0);
    peer.release(); peer.wait_read(4);
    assert(decoder.poll(&picture[0]) == 1);
    assert(picture[(P::PaddingX + 1) * 4 + 2] == 27);

    // Shutdown must join an in-flight readback before destroying its objects.
    assert(!pthread_create(&thread, 0, close_session, &decoder));
    usleep(50000);
    pthread_mutex_lock(&peer.gate); assert(peer.in_read && peer.stopped == 0); pthread_mutex_unlock(&peer.gate);
    peer.release(); assert(!pthread_join(thread, 0));
    assert(peer.objects.empty());
    assert(decoder.poll(&picture[0]) == 0);
    assert(decoder.open());
    assert(decoder.poll(&picture[0]) == 0); // no stale pixels across reset
    assert(decoder.decode(access_unit, sizeof(access_unit), 0, &picture[0], true) == 0);
    peer.wait_read(5); peer.release(); peer.wait_read(6);
    assert(decoder.poll(&picture[0]) == 1);
    peer.release(1, true);
    const uint64_t until = now_us() + 2000000;
    int result = 0;
    while (!result && now_us() < until) { result = decoder.poll(&picture[0]); usleep(1000); }
    assert(result == -1); // worker failure reaches the input owner for normal recovery
    decoder.close(); decoder.close(); assert(peer.objects.empty());
    alarm(0);
    puts("PASS slow capture, latest picture, quiet input, reset, failure propagation and joined cleanup");
}
