// Run the player loop and decoder session against deterministic renderer, Screen and socket peers.
#include <string.h>
#define socket fake_socket
#define connect fake_connect
#define bind fake_bind
#define getsockname fake_getsockname
#define getsockopt fake_getsockopt
#define fcntl fake_fcntl
#define send fake_send
#define sendto fake_sendto
#define recv fake_recv
// Bind the socket stub locally so decoder and Screen close() methods keep their names.
extern "C" { static int close(int); }
#define poll fake_poll
#define usleep fake_usleep
#define clock_gettime fake_clock
#define listen fake_listen
#define accept fake_accept
#define signal fake_signal
#include "cluster/video/player.cpp"
#define screen_post_window underlying_post_window
#include "fake_screen.hpp"
#undef screen_post_window

static void check(bool yes, const char *label) {
    if (!yes) {
        printf("FAIL %s\n", label);
        exit(1);
    }
}
namespace peer {
enum Role { Unassigned, Server, Client, Sink, Discovery, Guidance };
struct Socket {
    Role role;
    bool open;
    unsigned stream_size, stream_offset;
    uint8_t reply[64];
};
static Socket sockets[32];
static unsigned next_descriptor, clock_ms, subscription_count, cancellation_count, guidance_calls,
    failing_guidance_call, guidance_delay_ms;
static unsigned methods[1024], handles[1024];
static bool handle_created, stall_stream, stream_hangup, failure_injected, interrupt_during_post;
static bool initial_hud;
static uint8_t stream[aa_cluster::MaxPayload + 1024];
static unsigned stream_size, stream_offset;
static int allocate(Role role) {
    check(next_descriptor < 32, "bounded socket fixture");
    int fd = next_descriptor++;
    sockets[fd] = {};
    sockets[fd].open = true;
    sockets[fd].role = role;
    return fd;
}
static void reset() {
    memset(sockets, 0, sizeof(sockets));
    next_descriptor = 10;
    clock_ms = subscription_count = cancellation_count = guidance_calls = failing_guidance_call =
        guidance_delay_ms = stream_size = stream_offset = 0;
    handle_created = stall_stream = stream_hangup = failure_injected = interrupt_during_post =
        false;
    *__get_errno_ptr() = 0;
    interrupted = 0;
    service_mode = false;
    discovery_session = 1;
    discovery_reboot = true;
    screen_peer::reset();
    initial_hud = false;
}
static void append(const uint8_t *data, unsigned n) {
    check(stream_size + n <= sizeof(stream), "bounded input fixture");
    memcpy(stream + stream_size, data, n);
    stream_size += n;
}
static void packet(unsigned kind, unsigned epoch, const uint8_t *data = 0, unsigned n = 0) {
    uint8_t wire_header[aa_cluster::HeaderSize];
    aa_cluster::header(wire_header, kind, epoch, n, 0);
    append(wire_header, sizeof(wire_header));
    if (n) {
        append(data, n);
    }
}
static void expect_cleaned(bool with_handle, unsigned expected_posts, unsigned sessions = 1) {
    check(unsigned(screen_peer::posts) == expected_posts, "expected frame presentation count");
    check(!screen_peer::contexts && !screen_peer::windows, "no owned Screen objects remain");
    for (unsigned i = 10; i < next_descriptor; ++i) {
        check(!sockets[i].open, "every socket closed");
    }
    check(cancellation_count == (subscription_count ? sessions : 0u),
          "discovery subscription released, including renewals");
    check(handle_created == with_handle, "expected Guidance ownership");
    if (with_handle) {
        // Literal cancellation sequence; no calls from the model's start path
        // may substitute for cleanup after acquiring handle 200.
        const unsigned expected[] = {9, 9, 9, 12, 4, 9};
        check(guidance_calls >= 7, "Guidance cancellation calls made");
        for (unsigned i = 0; i < 6; ++i) {
            check(methods[guidance_calls - 6 + i] == expected[i], "Guidance cancellation order");
            check(handles[guidance_calls - 6 + i] == 200, "release the acquired handle");
        }
    }
}
} // namespace peer
extern "C" int *__get_errno_ptr() {
    static int error;
    return &error;
}
extern "C" int screen_post_window(void *window, void *buffer, int count, const int *damage,
                                  int flags) {
    if (peer::interrupt_during_post) {
        on_signal(15);
        *__get_errno_ptr() = 4;
        return -1;
    }
    return underlying_post_window(window, buffer, count, damage, flags);
}
extern "C" int fake_clock(int, PipClock *p) {
    p->tv_sec = peer::clock_ms / 1000;
    p->tv_nsec = (peer::clock_ms % 1000) * 1000000;
    return 0;
}
extern "C" int fake_usleep(unsigned n) {
    peer::clock_ms += n / 1000;
    return 0;
}
extern "C" void (*fake_signal(int, void (*handler)(int)))(int) {
    return handler;
}
extern "C" int fake_socket(int, int type, int) {
    return peer::allocate(type == 2 ? peer::Discovery : peer::Unassigned);
}
extern "C" int fake_connect(int fd, const void *address, unsigned) {
    unsigned p = ((const Address *)address)->port;
    p = (p >> 8) | ((p & 255) << 8);
    check(p == 30503 || p == 30502, "only expected cluster services");
    peer::sockets[fd].role = p == 30503 ? peer::Sink : peer::Guidance;
    return 0;
}
extern "C" int fake_bind(int fd, const void *address, unsigned) {
    if (((const Address *)address)->ip == 0x0100007f) {
        peer::sockets[fd].role = peer::Server;
    }
    return 0;
}
extern "C" int fake_listen(int, int) {
    return 0;
}
extern "C" int fake_accept(int, void *, unsigned *) {
    return peer::allocate(peer::Client);
}
extern "C" int fake_getsockname(int, void *p, unsigned *) {
    ((Address *)p)->port = 0x3412;
    return 0;
}
extern "C" int fake_getsockopt(int, int, int, void *, unsigned *) {
    return 0;
}
extern "C" int fake_fcntl(int, int, ...) {
    return 0;
}
extern "C" int close(int fd) {
    check(fd >= 10 && unsigned(fd) < peer::next_descriptor && peer::sockets[fd].open,
          "close only a live owned descriptor");
    peer::sockets[fd].open = false;
    return 0;
}
extern "C" int fake_poll(PollFD *p, unsigned count, int timeout) {
    check(count == 1, "one descriptor per poll");
    peer::Socket &s = peer::sockets[p->fd];
    p->revents = 0;
    check(s.open, "poll live descriptor");
    if (s.role == peer::Client && peer::stream_offset == peer::stream_size) {
        if (peer::stream_hangup) {
            p->revents = PollErrorOrHangup;
            return 1;
        }
        if (peer::stall_stream) {
            peer::clock_ms += timeout;
            return 0;
        }
        p->revents = p->events;
        return 1; // recv reports EOF
    }
    if (s.role == peer::Server || s.role == peer::Client || (p->events & PollWrite) ||
        s.stream_offset < s.stream_size) {
        p->revents = p->events;
        return 1;
    }
    peer::clock_ms += timeout;
    return 0;
}
extern "C" int fake_sendto(int, const void *data, unsigned n, int, const void *, unsigned) {
    const uint8_t *p = (const uint8_t *)data;
    check(n == 56, "SOME/IP subscription shape");
    if (!p[35]) {
        ++peer::cancellation_count;
        return n;
    }
    ++peer::subscription_count;
    for (unsigned i = 10; i < peer::next_descriptor; ++i) {
        if (peer::sockets[i].role == peer::Sink) {
            peer::Socket &s = peer::sockets[i];
            s.stream_size = 29;
            s.stream_offset = 0;
            memset(s.reply, 0, sizeof(s.reply));
            aa_cluster::put32(s.reply, 0xb0098001);
            aa_cluster::put32(s.reply + 4, 21);
            s.reply[12] = s.reply[13] = 1;
            s.reply[14] = 2;
            put16(s.reply + 24, peer::initial_hud ? 277 : 620);
            put16(s.reply + 26, peer::initial_hud ? 160 : 313);
            s.reply[28] = peer::initial_hud ? 0 : 1;
        }
    }
    return n;
}
extern "C" int fake_send(int fd, const void *data, unsigned n, int) {
    peer::Socket &s = peer::sockets[fd];
    const uint8_t *p = (const uint8_t *)data;
    if (s.role == peer::Client) {
        check(n == 8, "local player hello");
        return n;
    }
    if (s.role == peer::Sink) {
        check(n == 18 && p[16] <= 1, "sink acknowledgement");
        return n;
    }
    check(s.role == peer::Guidance && n >= 16 && peer::guidance_calls < 1024, "Guidance call shape");
    unsigned method = get16(p + 2), index = peer::guidance_calls++;
    peer::clock_ms += peer::guidance_delay_ms;
    peer::methods[index] = method;
    peer::handles[index] = n >= 20 ? get32(p + 16) : 0;
    s.stream_size = method == 1 ? 24 : 16;
    s.stream_offset = 0;
    memset(s.reply, 0, sizeof(s.reply));
    memcpy(s.reply, p, 16);
    aa_cluster::put32(s.reply + 4, s.stream_size - 8);
    s.reply[14] = 0x80;
    if (method == 1) {
        aa_cluster::put32(s.reply + 16, 200);
        peer::handle_created = true;
    }
    if (peer::failing_guidance_call && peer::guidance_calls == peer::failing_guidance_call) {
        s.reply[15] = 1;
        peer::failure_injected = true;
    }
    return n;
}
extern "C" int fake_recv(int fd, void *data, unsigned n, int) {
    peer::Socket &s = peer::sockets[fd];
    if (s.role == peer::Client) {
        unsigned left = peer::stream_size - peer::stream_offset;
        if (n > left) {
            n = left;
        }
        if (n > 137) {
            n = 137; // fragment headers and compressed access units
        }
        memcpy(data, peer::stream + peer::stream_offset, n);
        peer::stream_offset += n;
        return n;
    }
    if (n > s.stream_size - s.stream_offset) {
        n = s.stream_size - s.stream_offset;
    }
    memcpy(data, s.reply + s.stream_offset, n);
    s.stream_offset += n;
    return n;
}

static uint8_t first_frame[aa_cluster::MaxPayload];
static unsigned first_frame_size;
static void prepare_one_frame() {
    peer::reset();
    peer::packet(Reset, 3);
    peer::packet(Video, 3, first_frame, first_frame_size);
}
static void load_first_frame() {
    FILE *fixture = fopen("fixture.acp", "rb");
    check(fixture != 0, "generated H264 fixture");
    uint8_t wire_header[HeaderSize];
    check(fread(wire_header, 1, sizeof(wire_header), fixture) == sizeof(wire_header),
          "fixture reset");
    check(fread(wire_header, 1, sizeof(wire_header), fixture) == sizeof(wire_header) &&
              be32(wire_header + 4) == Video,
          "fixture video");
    first_frame_size = be32(wire_header + 12);
    check(first_frame_size <= MaxPayload, "fixture frame bound");
    check(fread(first_frame, 1, first_frame_size, fixture) == first_frame_size, "fixture frame");
    fclose(fixture);
}

static void test_service_and_trial_deadlines() {
    // More than ten seconds in total, but every individual reply is prompt.
    // A service must not inherit the old shared ten-second trial deadline.
    prepare_one_frame();
    service_mode = true;
    peer::guidance_delay_ms = 120;
    check(demo_main() == 0, "service startup survives slow total Guidance setup");
    peer::expect_cleaned(true, 1);
    check(peer::clock_ms > 10000 && peer::clock_ms < 20000, "slow startup reproduced");

    prepare_one_frame();
    service_mode = true;
    peer::interrupt_during_post = true;
    check(demo_main() == 0, "SIGTERM during Screen post is orderly cancellation");
    peer::expect_cleaned(true, 0);
    prepare_one_frame();
    peer::stall_stream = true;
    check(demo_main() == 0, "standalone trial deadline still ends idle playback");
    peer::expect_cleaned(true, 1);
    check(peer::clock_ms >= 65000 && peer::clock_ms < 66000, "standalone trial remains bounded");
}

static void test_reactivation_on_one_connection() {
    prepare_one_frame();
    service_mode = true;
    peer::guidance_delay_ms = 120;
    peer::packet(Stop, 4);
    peer::packet(Reset, 5);
    peer::packet(Video, 5, first_frame, first_frame_size);
    peer::packet(Stop, 6);
    check(demo_main() == 0, "two slow map activations share one player connection");
    peer::expect_cleaned(true, 2, 2);
}

static void test_end_of_stream() {
    prepare_one_frame();
    check(demo_main() == 0, "EOF after an active map");
    peer::expect_cleaned(true, 1);
    prepare_one_frame();
    peer::stream_hangup = true;
    check(demo_main() == 0, "socket hangup after an active map");
    peer::expect_cleaned(true, 1);
}

static void test_partial_stream_reads() {
    uint8_t wire_header[HeaderSize];
    prepare_one_frame();
    header(wire_header, Video, 3, 16, 0);
    peer::append(wire_header, 7);
    check(demo_main() == 0, "EOF inside header");
    peer::expect_cleaned(true, 1);
    prepare_one_frame();
    header(wire_header, Video, 3, 16, 0);
    peer::append(wire_header, sizeof(wire_header));
    peer::append(first_frame, 3);
    check(demo_main() != 0, "EOF inside payload");
    peer::expect_cleaned(true, 1);
    prepare_one_frame();
    header(wire_header, Video, 3, 16, 0);
    peer::append(wire_header, sizeof(wire_header));
    peer::append(first_frame, 3);
    peer::stall_stream = true;
    check(demo_main() != 0, "stalled partial payload times out");
    peer::expect_cleaned(true, 1);
    check(peer::clock_ms < 10000, "partial read has a bounded deadline");

    // The car log ended after exactly 36,832 bytes of a 42,919-byte
    // access unit. No incomplete unit may reach the decoder on recovery.
    prepare_one_frame();
    header(wire_header, Video, 3, 42919, 0);
    peer::append(wire_header, sizeof(wire_header));
    static const uint8_t truncated[36832] = {};
    peer::append(truncated, sizeof(truncated));
    peer::stall_stream = true;
    check(demo_main() != 0, "captured partial access unit fails within deadline");
    peer::expect_cleaned(true, 1);
    check(peer::clock_ms < 10000, "captured dropout cleanup is bounded");
}

static void test_invalid_stream_data() {
    uint8_t wire_header[HeaderSize];
    prepare_one_frame();
    header(wire_header, Video, 3, MaxPayload + 1, 0);
    peer::append(wire_header, sizeof(wire_header));
    check(demo_main() != 0, "oversized payload rejected before reading");
    peer::expect_cleaned(true, 1);
    prepare_one_frame();
    peer::packet(Video, 99, first_frame, first_frame_size);
    check(demo_main() != 0, "stale stream generation rejected");
    peer::expect_cleaned(true, 1);
    const uint8_t broken_slice[] = {0, 0, 0, 1, 0x65, 0xff, 0xff, 0xff};
    prepare_one_frame();
    peer::packet(Reset, 4);
    peer::packet(Video, 4, broken_slice, sizeof(broken_slice));
    check(demo_main() != 0, "new renderer session requires SPS and PPS with its IDR");
    peer::expect_cleaned(true, 1);
}

static void test_partial_setup_cleanup() {
    // These errors happen after Screen objects exist and, for the RPC failures,
    // after Guidance has allocated its handle. No frame may reach the output.
    const unsigned failures[] = {2, 8};
    for (unsigned i = 0; i < 2; ++i) {
        prepare_one_frame();
        peer::failing_guidance_call = failures[i];
        check(demo_main() != 0 && peer::failure_injected, "partial Guidance setup fails");
        peer::expect_cleaned(true, 0);
    }
    prepare_one_frame();
    screen_peer::fail_buffers = true;
    check(demo_main() != 0, "Screen allocation fails");
    peer::expect_cleaned(false, 0);
    prepare_one_frame();
    screen_peer::fail_post = true;
    check(demo_main() != 0, "Screen presentation fails");
    peer::expect_cleaned(true, 0);
}

static void test_restart_after_failures() {
    // Every failure above must permit a clean subsequent session.
    prepare_one_frame();
    peer::packet(Stop, 4);
    check(demo_main() == 0, "fresh session succeeds after failures");
    peer::expect_cleaned(true, 1);
}

static void notify_sink(Player& player, bool hud) {
    peer::Socket& socket = peer::sockets[player.control.control];
    check(socket.role == peer::Sink && socket.open, "sink subscription retained");
    socket.stream_size = 29;
    socket.stream_offset = 0;
    memset(socket.reply, 0, sizeof(socket.reply));
    aa_cluster::put32(socket.reply, 0xb0098001);
    aa_cluster::put32(socket.reply + 4, 21);
    socket.reply[12] = socket.reply[13] = 1;
    socket.reply[14] = 2;
    put16(socket.reply + 24, hud ? 277 : 620);
    put16(socket.reply + 26, hud ? 160 : 313);
    socket.reply[28] = hud ? 0 : 1;
}

static void test_hud_pause_resume() {
    prepare_one_frame();
    service_mode = true;
    Player* player = new Player;
    uint8_t h[HeaderSize];
    header(h, Reset, 3, 0, 0);
    check(player->packet(h, 0, false), "decoder reset");
    header(h, Video, 3, first_frame_size, 0);
    check(player->packet(h, first_frame, false) && player->pump(), "initial map");
    const int sink = player->control.control;
    for (unsigned cycle = 0; cycle < 2; ++cycle) {
        unsigned posts = screen_peer::posts;
        notify_sink(*player, true);
        check(player->pump(), "valid HUD selection is not fatal");
        check(player->active && !player->presenting && !screen_peer::windows &&
              player->control.guidance == -1, "HUD pause releases output and Guidance");
        check(player->control.control == sink && !peer::cancellation_count,
              "HUD pause retains original subscription");
        check(player->packet(h, first_frame, false) && player->pump() && unsigned(screen_peer::posts) == posts,
              "HUD pause consumes video without cluster-plane posts");
        notify_sink(*player, false);
        check(player->pump() && player->presenting, "cluster selection resumes without setting toggle");
        check(unsigned(screen_peer::posts) == posts + 1, "cached map is re-presented on resume");
    }
    notify_sink(*player, true);
    check(player->pump(), "pause before map-off");
    header(h, Stop, 4, 0, 0);
    check(player->packet(h, 0, false), "map-off while HUD paused");
    check(!player->active && !player->have_picture && player->control.control == -1,
          "map-off cancels retained subscription and cached picture");
    check(player->pump() && !player->presenting, "map-off cannot auto-resume");
    delete player;
    peer::expect_cleaned(true, 3);
}

static void test_start_with_hud_selected() {
    prepare_one_frame();
    service_mode = peer::initial_hud = true;
    Player* player = new Player;
    uint8_t h[HeaderSize];
    header(h, Reset, 3, 0, 0);
    check(player->packet(h, 0, false), "reset with HUD initially selected");
    header(h, Video, 3, first_frame_size, 0);
    check(player->packet(h, first_frame, false) && player->pump(), "cache map with HUD initially selected");
    check(player->active && !player->presenting && !peer::handle_created && !screen_peer::posts,
          "initial HUD does not acquire a cluster output");
    notify_sink(*player, false);
    check(player->pump() && player->presenting && screen_peer::posts == 1,
          "initial HUD can transition directly to cached cluster map");
    check(player->stop(), "clean initial-HUD session");
    delete player;
    peer::expect_cleaned(true, 1);
}

int main() {
    load_first_frame();
    test_hud_pause_resume();
    test_start_with_hud_selected();
    test_service_and_trial_deadlines();
    test_reactivation_on_one_connection();
    test_end_of_stream();
    test_partial_stream_reads();
    test_invalid_stream_data();
    test_partial_setup_cleanup();
    test_restart_after_failures();
    printf("AA_PLAYER_LIFECYCLE_OK EOF, hangup, truncation, timeout, oversize, epoch, decoder, "
           "partial setup, restart\n");
}
