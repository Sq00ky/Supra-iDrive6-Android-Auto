// Actual car Control class against a deterministic peer; no real networking.
#define socket fake_socket
#define connect fake_connect
#define bind fake_bind
#define getsockname fake_getsockname
#define getsockopt fake_getsockopt
#define fcntl fake_fcntl
#define send fake_send
#define sendto fake_sendto
#define recv fake_recv
#define close fake_close
#define poll fake_poll
#define usleep fake_usleep
#define clock_gettime fake_clock
#include "cluster/video/native/control.hpp"

static unsigned next_fd = 10, reply_sizes[16], reply_offsets[16], rpc_calls, closed_descriptors,
                resource_writes;
static unsigned packet_mismatches;
static uint8_t replies[16][64];
extern "C" int *__get_errno_ptr() {
    static int error;
    return &error;
}
extern "C" int fake_clock(int, PipClock *time) {
    time->tv_sec = time->tv_nsec = 0;
    return 0;
}
extern "C" int fake_usleep(unsigned) {
    return 0;
}
extern "C" int fake_socket(int, int, int) {
    return next_fd++;
}
extern "C" int fake_connect(int, const void *, unsigned) {
    return 0;
}
extern "C" int fake_bind(int, const void *, unsigned) {
    return 0;
}
extern "C" int fake_getsockopt(int, int, int, void *, unsigned *) {
    return 0;
}
extern "C" int fake_fcntl(int, int, ...) {
    return 0;
}
extern "C" int fake_getsockname(int, void *request, unsigned *) {
    ((Address *)request)->port = 0x3412;
    return 0;
}
extern "C" int fake_close(int) {
    ++closed_descriptors;
    return 0;
}
extern "C" int fake_poll(PollFD *request, unsigned, int) {
    request->revents = request->events;
    return 1;
}
static uint8_t *prepare_reply(int fd, unsigned size) {
    for (unsigned i = 0; i < 64; ++i) {
        replies[fd][i] = 0;
    }
    reply_sizes[fd] = size;
    reply_offsets[fd] = 0;
    return replies[fd];
}
extern "C" int fake_sendto(int, const void *data, unsigned size, int, const void *, unsigned) {
    if (((const uint8_t *)data)[35]) {
        uint8_t *reply = prepare_reply(10, 29);
        put32(reply, 0xb0098001);
        put32(reply + 4, 21);
        reply[12] = reply[13] = 1;
        reply[14] = 2;
        put16(reply + 24, 620);
        put16(reply + 26, 313);
        reply[28] = 1;
    }
    return size;
}
extern "C" int fake_send(int fd, const void *data, unsigned size, int) {
    const uint8_t *request = (const uint8_t *)data;
    if (fd == 10) {
        if (size != 18 || request[16] != 1 || request[17] != 0) {
            ++packet_mismatches;
        }
        return size;
    }
    if (fd != 12 || size < 16) {
        ++packet_mismatches;
        return -1;
    }
    ++rpc_calls;
    unsigned method = get16(request + 2);
    if (method == 3 && size == 44 && request[24] == 2) {
        // Literal 28-byte body captured from the factory, with handle 200.
        const uint8_t expected[] = {0, 0, 0, 200, 2, 0, 0, 0, 2, 0,  0, 0,   11, 0,
                                    0, 0, 3, 2,   0, 0, 0, 2, 3, 32, 1, 224, 0,  1};
        for (unsigned i = 0; i < sizeof(expected); ++i) {
            if (request[16 + i] != expected[i]) {
                ++packet_mismatches;
            }
        }
        ++resource_writes;
    }
    uint8_t *reply = prepare_reply(fd, method == 1 ? 24 : 16);
    for (unsigned i = 0; i < 16; ++i) {
        reply[i] = request[i];
    }
    put32(reply + 4, method == 1 ? 16 : 8);
    reply[14] = 0x80;
    if (method == 1) {
        put32(reply + 16, 200);
    }
    return size;
}
extern "C" int fake_recv(int fd, void *data, unsigned size, int) {
    if (size > reply_sizes[fd] - reply_offsets[fd]) {
        return 0;
    }
    for (unsigned i = 0; i < size; ++i) {
        ((uint8_t *)data)[i] = replies[fd][reply_offsets[fd]++];
    }
    return size;
}
int main() {
    end_time = 65000;
    Control control = {-1, -1, -1, 0, 1, 0, 0};
    bool started = control.start(), closed_cleanly = control.close();
    on_signal(0);
    bool passed = started && closed_cleanly && control.stopped() && !packet_mismatches &&
                  resource_writes == 1 && rpc_calls == 107 && closed_descriptors == 3;
    printf(
        "%s physical resource matches factory packet, sink stays 620x313, cleanup acknowledged\n",
        passed ? "PASS" : "FAIL");
    return passed ? 0 : 1;
}
