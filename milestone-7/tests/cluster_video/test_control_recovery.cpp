// Exercise late/fragmented native Guidance replies, including cancellation.
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

static unsigned clock_ms, incoming_size, incoming_offset, rpc_calls, closed_descriptors,
    reply_ready_at_ms, fragment_boundary;
static unsigned methods[16], request_handles[16];
static uint8_t incoming[1024];
static bool missing_reply, wrong_transaction, error_reply, fail_partial_request,
    interrupt_partial_reply;
static void check(bool ok, const char *label) {
    if (!ok) {
        printf("FAIL %s\n", label);
        __builtin_trap();
    }
}
extern "C" int *__get_errno_ptr() {
    static int error;
    return &error;
}
extern "C" int fake_clock(int, PipClock *time) {
    time->tv_sec = clock_ms / 1000;
    time->tv_nsec = clock_ms % 1000 * 1000000;
    return 0;
}
extern "C" int fake_usleep(unsigned microseconds) {
    clock_ms += microseconds / 1000;
    return 0;
}
extern "C" int fake_socket(int, int, int) {
    return 10;
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
extern "C" int fake_getsockname(int, void *, unsigned *) {
    return 0;
}
extern "C" int fake_close(int) {
    ++closed_descriptors;
    return 0;
}
extern "C" int fake_sendto(int, const void *, unsigned size, int, const void *, unsigned) {
    return size;
}
extern "C" int fake_poll(PollFD *request, unsigned, int timeout) {
    request->revents = 0;
    if ((request->events & PollRead) &&
        (incoming_offset == incoming_size || missing_reply ||
         (incoming_offset >= fragment_boundary && clock_ms < reply_ready_at_ms))) {
        clock_ms += unsigned(timeout);
        return 0;
    }
    request->revents = request->events;
    return 1;
}
extern "C" int fake_send(int, const void *data, unsigned size, int) {
    if (fail_partial_request) {
        if (!rpc_calls++) {
            return 8;
        }
        *__get_errno_ptr() = 5;
        return -1;
    }
    check(rpc_calls < 16 && size >= 16, "bounded complete request");
    const uint8_t *request = (const uint8_t *)data;
    unsigned method = get16(request + 2);
    request_handles[rpc_calls] = size >= 20 ? get32(request + 16) : 0;
    methods[rpc_calls++] = method;
    unsigned reply_size = method == 1 ? 24 : 16;
    check(incoming_size + reply_size <= sizeof(incoming), "bounded replies");
    uint8_t *reply = incoming + incoming_size;
    incoming_size += reply_size;
    for (unsigned i = 0; i < 16; ++i) {
        reply[i] = request[i];
    }
    put32(reply + 4, reply_size - 8);
    reply[14] = 0x80;
    reply[15] = error_reply ? 1 : 0;
    if (wrong_transaction) {
        put16(reply + 10, get16(request + 10) + 1);
    }
    if (method == 1) {
        put32(reply + 16, 200);
    }
    return size;
}
extern "C" int fake_recv(int, void *data, unsigned size, int) {
    if (size > incoming_size - incoming_offset) {
        size = incoming_size - incoming_offset;
    }
    if (incoming_offset < fragment_boundary && size > fragment_boundary - incoming_offset) {
        size = fragment_boundary - incoming_offset;
    }
    for (unsigned i = 0; i < size; ++i) {
        ((uint8_t *)data)[i] = incoming[incoming_offset++];
    }
    if (interrupt_partial_reply && incoming_offset == fragment_boundary) {
        on_signal(15);
    }
    return size;
}
static Control reset_peer() {
    clock_ms = incoming_size = incoming_offset = rpc_calls = closed_descriptors =
        reply_ready_at_ms = 0;
    fragment_boundary = 8;
    missing_reply = wrong_transaction = error_reply = fail_partial_request =
        interrupt_partial_reply = false;
    interrupted = 0;
    service_mode = true;
    end_time = 10000;
    return Control{10, 11, 12, 1234, 2, 200, 0};
}
static bool guidance_request(Control &control, unsigned method = 14) {
    uint8_t body[4] = {0, 0, 0, 200};
    return control.guidance_call(12, method, body, 4, 1, method == 1 ? &control.handle : 0);
}
static void expect_cancelled(Control &control) {
    check(control.close(), "late reply drained and cleanup acknowledged");
    const unsigned expected[] = {9, 9, 9, 12, 4, 9};
    check(rpc_calls == 7 && closed_descriptors == 3,
          "six cancellation RPCs and all descriptors closed");
    for (unsigned i = 0; i < 6; ++i) {
        check(methods[i + 1] == expected[i], "factory cancellation order unchanged");
    }
    check(incoming_offset == incoming_size, "every reply consumed");
}
static void test_delayed_header_and_handle() {
    Control control = reset_peer();
    reply_ready_at_ms = 2200;
    check(!guidance_request(control) && control.reply_used == 8 && control.pending_reply,
          "timeout retains partial header");
    expect_cancelled(control);
    control = reset_peer();
    reply_ready_at_ms = 2200;
    fragment_boundary = 19;
    control.handle = 0;
    check(!guidance_request(control, 1) && control.reply_used == 19,
          "timeout retains partial create-handle body");
    expect_cancelled(control);
    check(control.handle == 0 && request_handles[1] == 200, "late-created handle recovered, cancelled and cleared");
}

static void test_signal_during_reply() {
    Control control = reset_peer();
    interrupt_partial_reply = true;
    check(!guidance_request(control) && interrupted, "signal interrupts partial reply");
    expect_cancelled(control);
}

static void test_missing_and_mismatched_replies() {
    Control control = reset_peer();
    missing_reply = true;
    check(!guidance_request(control) && !control.close(), "missing reply remains failure");
    check(!control.close(), "failed cleanup stays failed on repeated close");
    check(clock_ms <= 3000 && rpc_calls == 1 && closed_descriptors == 3,
          "missing peer cannot hang cleanup or receive another request");
    control = reset_peer();
    wrong_transaction = true;
    check(!guidance_request(control) && !control.close() && rpc_calls == 1,
          "unrelated reply fails closed");
}

static void test_negative_reply() {
    Control control = reset_peer();
    error_reply = true;
    check(!guidance_request(control), "negative reply reported");
    error_reply = false;
    expect_cancelled(control);
}

static void test_partial_request() {
    Control control = reset_peer();
    fail_partial_request = true;
    check(!guidance_request(control) && control.broken_stream && !control.close(),
          "partial request never followed by malformed cancellation");
}

int main() {
    test_delayed_header_and_handle();
    test_signal_during_reply();
    test_missing_and_mismatched_replies();
    test_negative_reply();
    test_partial_request();
    printf("AA_CONTROL_RECOVERY_OK delayed header/body, late handle, signal, missing peer, "
           "mismatch, negative reply, partial send\n");
}
