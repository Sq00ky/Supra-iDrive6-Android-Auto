// Real-kernel regression for the native adapter, not a TimerTimeout mock.
// Build/run in the isolated QNX SDK VM; no Screen service or car is needed.
#include "cluster/video/native/renderer_api.hpp"
#include <sys/neutrino.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>
#include <assert.h>

static int channel, connection, read_calls;
static void* responder(void*) {
    char bytes[128];
    for (;;) {
        int id = MsgReceive(channel, bytes, sizeof(bytes), 0);
        if (id <= 0) continue;
        usleep(2000); // Ensure the client's reply wait can actually block.
        MsgReply(id, 0, 0, 0); // A deliberately timed-out client may be gone.
    }
    return 0;
}
static int readback(void*, void*, int count, const int* rect, int flags) {
    assert(count == 0 && !rect && !flags);
    ++read_calls;
    char request = 1;
    return MsgSend(connection, &request, 1, 0, 0);
}
static int failed_readback(void*, void*, int, const int*, int) {
    ++read_calls;
    errno = EIO; // No IPC consumed the armed timeout on this failure path.
    return -1;
}
static void subsequent_ipc() {
    char request = 2;
    int result = MsgSend(connection, &request, 1, 0, 0);
    if (result) fprintf(stderr, "FOLLOWUP_IPC_FAILED errno=%d\n", errno);
    assert(result == 0);
}
static void arm_previous_timeout() {
    uint64_t delay = 5000000000ULL;
    assert(TimerTimeout(CLOCK_MONOTONIC, _NTO_TIMEOUT_SEND | _NTO_TIMEOUT_REPLY,
                        0, &delay, 0) >= 0);
}
int main() {
    channel = ChannelCreate(0); assert(channel >= 0);
    connection = ConnectAttach(0, 0, channel, _NTO_SIDE_CHANNEL, 0); assert(connection >= 0);
    pthread_t thread; assert(pthread_create(&thread, 0, responder, 0) == 0);

    // Reproduce the old cancellation against QNX itself. A null duration with
    // SEND/REPLY means immediate timeout, not cancellation.
    assert(TimerTimeout(CLOCK_MONOTONIC, _NTO_TIMEOUT_SEND | _NTO_TIMEOUT_REPLY,
                        0, 0, 0) >= 0);
    char request = 0;
    int result = MsgSend(connection, &request, 1, 0, 0);
    const int old_error = errno;
    assert(TimerTimeout(CLOCK_MONOTONIC, 0, 0, 0, 0) >= 0);
    assert(result == -1 && old_error == ETIMEDOUT);
    printf("OLD_CANCELLATION_REPRODUCED errno=%d ETIMEDOUT=%d\n", old_error, ETIMEDOUT);

    aa_cluster::hardware::RendererApi api;
    api.screen_read_window = readback;
    for (unsigned i = 0; i < 20; ++i) {
        if (i) arm_previous_timeout();
        assert(api.read_picture(0, 0) == 0);
        subsequent_ipc();
    }
    assert(read_calls == 20); // Positive previous flags must not skip readback.

    api.screen_read_window = failed_readback;
    assert(api.read_picture(0, 0) == -1 && errno == EIO);
    subsequent_ipc(); // Failure before any IPC must also leave no armed timeout.

    arm_previous_timeout();
    uint32_t payload = 1;
    assert(api.send_packet(connection, 5, &payload, sizeof(payload), 0) == 0);
    subsequent_ipc();
    arm_previous_timeout();
    assert(api.send_packet(-1, 5, &payload, sizeof(payload), 0) < 0);
    subsequent_ipc();

    assert(pthread_cancel(thread) == 0);
    assert(pthread_join(thread, 0) == 0);
    ConnectDetach(connection); ChannelDestroy(channel);
    puts("PASS native hardware timeout cancellation, previous flags, error preservation and following IPC");
}
