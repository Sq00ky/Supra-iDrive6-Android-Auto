// Run the production player entry point, real decoder and cleanup paths.
// Only QNX Screen, sockets, signals and time are replaced; no vehicle access.
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
#define listen fake_listen
#define accept fake_accept
#define signal fake_signal
#include "cluster/g30/player.cpp"
#define screen_post_window underlying_post_window
#include "fake_screen.hpp"
#undef screen_post_window

static void check(bool yes, const char* label) {
    if (!yes) { printf("FAIL %s\n", label); exit(1); }
}
namespace peer {
enum Role { Unassigned, Server, Client, Sink, Discovery, Guidance };
struct Socket { Role role; bool open; unsigned size, offset; uint8_t reply[64]; };
static Socket sockets[32];
static unsigned next_fd, clock_ms, subscriptions, cancellations, rpc_count, fail_rpc, rpc_delay;
static unsigned methods[256], handles[256];
static bool handle_created, stall, hangup, failure_injected, interrupt_post;
static uint8_t stream[aa_cluster::MaxPayload + 1024];
static unsigned size, offset;
static int allocate(Role role) {
    check(next_fd < 32, "bounded socket fixture");
    int fd = next_fd++; sockets[fd] = {}; sockets[fd].open = true; sockets[fd].role = role;
    return fd;
}
static void reset() {
    memset(sockets, 0, sizeof(sockets)); next_fd = 10;
    clock_ms = subscriptions = cancellations = rpc_count = fail_rpc = rpc_delay = size = offset = 0;
    handle_created = stall = hangup = failure_injected = interrupt_post = false;
    *__get_errno_ptr() = 0;
    interrupted = 0; service_mode = false; discovery_session = 1; discovery_reboot = true;
    screen_peer::reset();
}
static void append(const uint8_t* data, unsigned n) {
    check(size + n <= sizeof(stream), "bounded input fixture");
    memcpy(stream + size, data, n); size += n;
}
static void packet(unsigned kind, unsigned epoch, const uint8_t* data = 0, unsigned n = 0) {
    uint8_t h[aa_cluster::HeaderSize]; aa_cluster::header(h, kind, epoch, n, 0);
    append(h, sizeof(h)); if (n) append(data, n);
}
static void cleaned(bool with_handle, unsigned expected_posts, unsigned sessions=1) {
    check(unsigned(screen_peer::posts) == expected_posts, "expected frame presentation count");
    check(!screen_peer::contexts && !screen_peer::windows, "no owned Screen objects remain");
    for (unsigned i = 10; i < next_fd; ++i) check(!sockets[i].open, "every socket closed");
    check(cancellations == (subscriptions ? sessions : 0u), "discovery subscription released, including renewals");
    check(handle_created == with_handle, "expected Guidance ownership");
    if (with_handle) {
        // Literal cancellation sequence; no calls from the model's start path
        // may substitute for cleanup after acquiring handle 200.
        const unsigned expected[] = {9, 9, 9, 12, 4, 9};
        check(rpc_count >= 7, "Guidance cancellation calls made");
        for (unsigned i = 0; i < 6; ++i) {
            check(methods[rpc_count - 6 + i] == expected[i], "Guidance cancellation order");
            check(handles[rpc_count - 6 + i] == 200, "release the acquired handle");
        }
    }
}
}
extern "C" int* __get_errno_ptr() { static int error; return &error; }
extern "C" int screen_post_window(void* window,void* buffer,int count,const int* damage,int flags) {
    if(peer::interrupt_post) { on_signal(15);*__get_errno_ptr()=4;return -1; }
    return underlying_post_window(window,buffer,count,damage,flags);
}
extern "C" int fake_clock(int, PipClock* p) {
    p->tv_sec = peer::clock_ms / 1000; p->tv_nsec = (peer::clock_ms % 1000) * 1000000;
    return 0;
}
extern "C" int fake_usleep(unsigned n) { peer::clock_ms += n / 1000; return 0; }
extern "C" void (*fake_signal(int, void (*handler)(int)))(int) { return handler; }
extern "C" int fake_socket(int, int type, int) { return peer::allocate(type == 2 ? peer::Discovery : peer::Unassigned); }
extern "C" int fake_connect(int fd, const void* address, unsigned) {
    unsigned p = ((const Address*)address)->port; p = (p >> 8) | ((p & 255) << 8);
    check(p == 30503 || p == 30502, "only expected cluster services");
    peer::sockets[fd].role = p == 30503 ? peer::Sink : peer::Guidance; return 0;
}
extern "C" int fake_bind(int fd, const void* address, unsigned) {
    if (((const Address*)address)->ip == 0x0100007f) peer::sockets[fd].role = peer::Server;
    return 0;
}
extern "C" int fake_listen(int, int) { return 0; }
extern "C" int fake_accept(int, void*, unsigned*) { return peer::allocate(peer::Client); }
extern "C" int fake_getsockname(int, void* p, unsigned*) { ((Address*)p)->port = 0x3412; return 0; }
extern "C" int fake_getsockopt(int, int, int, void*, unsigned*) { return 0; }
extern "C" int fake_fcntl(int, int, ...) { return 0; }
extern "C" int fake_close(int fd) {
    check(fd >= 10 && unsigned(fd) < peer::next_fd && peer::sockets[fd].open, "close only a live owned descriptor");
    peer::sockets[fd].open = false; return 0;
}
extern "C" int fake_poll(PollFD* p, unsigned count, int timeout) {
    check(count == 1, "one descriptor per poll");
    peer::Socket& s = peer::sockets[p->fd]; p->revents = 0;
    check(s.open, "poll live descriptor");
    if (s.role == peer::Client && peer::offset == peer::size) {
        if (peer::hangup) { p->revents = PollErrorOrHangup; return 1; }
        if (peer::stall) { peer::clock_ms += timeout; return 0; }
        p->revents = p->events; return 1; // recv reports EOF
    }
    if (s.role == peer::Server || s.role == peer::Client || (p->events & PollWrite) || s.offset < s.size) {
        p->revents = p->events; return 1;
    }
    peer::clock_ms += timeout; return 0;
}
extern "C" int fake_sendto(int, const void* data, unsigned n, int, const void*, unsigned) {
    const uint8_t* p = (const uint8_t*)data;
    check(n == 56, "SOME/IP subscription shape");
    if (!p[35]) { ++peer::cancellations; return n; }
    ++peer::subscriptions;
    for (unsigned i = 10; i < peer::next_fd; ++i) if (peer::sockets[i].role == peer::Sink) {
        peer::Socket& s = peer::sockets[i]; s.size = 29; s.offset = 0;
        memset(s.reply, 0, sizeof(s.reply)); aa_cluster::put32(s.reply, 0xb0098001); aa_cluster::put32(s.reply + 4, 21);
        s.reply[12] = s.reply[13] = 1; s.reply[14] = 2;
        put16(s.reply + 24, 620); put16(s.reply + 26, 313); s.reply[28] = 1;
    }
    return n;
}
extern "C" int fake_send(int fd, const void* data, unsigned n, int) {
    peer::Socket& s = peer::sockets[fd]; const uint8_t* p = (const uint8_t*)data;
    if (s.role == peer::Client) { check(n == 8, "local player hello"); return n; }
    if (s.role == peer::Sink) { check(n == 18 && p[16] == 1, "sink acknowledgement"); return n; }
    check(s.role == peer::Guidance && n >= 16 && peer::rpc_count < 256, "Guidance call shape");
    unsigned method = get16(p + 2), index = peer::rpc_count++;
    peer::clock_ms += peer::rpc_delay;
    peer::methods[index] = method; peer::handles[index] = n >= 20 ? get32(p + 16) : 0;
    s.size = method == 1 ? 24 : 16; s.offset = 0;
    memset(s.reply, 0, sizeof(s.reply)); memcpy(s.reply, p, 16);
    aa_cluster::put32(s.reply + 4, s.size - 8); s.reply[14] = 0x80;
    if (method == 1) { aa_cluster::put32(s.reply + 16, 200); peer::handle_created = true; }
    if (peer::fail_rpc && peer::rpc_count == peer::fail_rpc) {
        s.reply[15] = 1; peer::failure_injected = true;
    }
    return n;
}
extern "C" int fake_recv(int fd, void* data, unsigned n, int) {
    peer::Socket& s = peer::sockets[fd];
    if (s.role == peer::Client) {
        unsigned left = peer::size - peer::offset;
        if (n > left) n = left;
        if (n > 137) n = 137; // fragment headers and compressed access units
        memcpy(data, peer::stream + peer::offset, n); peer::offset += n; return n;
    }
    if (n > s.size - s.offset) n = s.size - s.offset;
    memcpy(data, s.reply + s.offset, n); s.offset += n; return n;
}

static uint8_t first_frame[aa_cluster::MaxPayload];
static unsigned first_size;
static void prepare() {
    peer::reset(); peer::packet(Reset, 3); peer::packet(Video, 3, first_frame, first_size);
}
int main() {
    FILE* f = fopen("fixture.acp", "rb"); check(f != 0, "generated H264 fixture");
    uint8_t h[HeaderSize]; check(fread(h, 1, sizeof(h), f) == sizeof(h), "fixture reset");
    check(fread(h, 1, sizeof(h), f) == sizeof(h) && be32(h + 4) == Video, "fixture video");
    first_size = be32(h + 12); check(first_size <= MaxPayload, "fixture frame bound");
    check(fread(first_frame, 1, first_size, f) == first_size, "fixture frame"); fclose(f);

    // More than ten seconds in total, but every individual reply is prompt.
    // A service must not inherit the old shared ten-second trial deadline.
    prepare(); service_mode = true; peer::rpc_delay = 120;
    check(demo_main() == 0, "service startup survives slow total Guidance setup");
    peer::cleaned(true, 1);
    check(peer::clock_ms > 10000 && peer::clock_ms < 20000, "slow startup reproduced");

    prepare(); service_mode = true; peer::interrupt_post = true;
    check(demo_main() == 0, "SIGTERM during Screen post is orderly cancellation");peer::cleaned(true,0);
    prepare(); peer::stall = true;
    check(demo_main() == 0, "standalone trial deadline still ends idle playback");peer::cleaned(true,1);
    check(peer::clock_ms >= 65000 && peer::clock_ms < 66000, "standalone trial remains bounded");

    prepare(); service_mode=true; peer::rpc_delay=120;
    peer::packet(Stop,4);peer::packet(Reset,5);peer::packet(Video,5,first_frame,first_size);peer::packet(Stop,6);
    check(demo_main()==0,"two slow map activations share one player connection");peer::cleaned(true,2,2);

    prepare(); check(demo_main() == 0, "EOF after an active map"); peer::cleaned(true, 1);
    prepare(); peer::hangup = true;
    check(demo_main() == 0, "socket hangup after an active map"); peer::cleaned(true, 1);

    prepare(); header(h, Video, 3, 16, 0); peer::append(h, 7);
    check(demo_main() == 0, "EOF inside header"); peer::cleaned(true, 1);
    prepare(); header(h, Video, 3, 16, 0); peer::append(h, sizeof(h)); peer::append(first_frame, 3);
    check(demo_main() != 0, "EOF inside payload"); peer::cleaned(true, 1);
    prepare(); header(h, Video, 3, 16, 0); peer::append(h, sizeof(h)); peer::append(first_frame, 3); peer::stall = true;
    check(demo_main() != 0, "stalled partial payload times out"); peer::cleaned(true, 1);
    check(peer::clock_ms < 10000, "partial read has a bounded deadline");

    prepare(); header(h, Video, 3, MaxPayload + 1, 0); peer::append(h, sizeof(h));
    check(demo_main() != 0, "oversized payload rejected before reading"); peer::cleaned(true, 1);
    prepare(); peer::packet(Video, 99, first_frame, first_size);
    check(demo_main() != 0, "stale stream generation rejected"); peer::cleaned(true, 1);
    const uint8_t broken_slice[] = {0, 0, 0, 1, 0x65, 0xff, 0xff, 0xff};
    prepare(); peer::packet(Video, 3, broken_slice, sizeof(broken_slice));
    check(demo_main() != 0, "damaged H264 rejected"); peer::cleaned(true, 1);

    // These errors happen after Screen objects exist and, for the RPC failures,
    // after Guidance has allocated its handle. No frame may reach the output.
    const unsigned failures[] = {2, 8};
    for (unsigned i = 0; i < 2; ++i) {
        prepare(); peer::fail_rpc = failures[i];
        check(demo_main() != 0 && peer::failure_injected, "partial Guidance setup fails"); peer::cleaned(true, 0);
    }
    prepare(); screen_peer::fail_buffers = true;
    check(demo_main() != 0, "Screen allocation fails"); peer::cleaned(false, 0);
    prepare(); screen_peer::fail_post = true;
    check(demo_main() != 0, "Screen presentation fails"); peer::cleaned(true, 0);

    // Every failure above must permit a clean subsequent session.
    prepare(); peer::packet(Stop, 4);
    check(demo_main() == 0, "fresh session succeeds after failures"); peer::cleaned(true, 1);
    printf("AA_PLAYER_LIFECYCLE_OK EOF, hangup, truncation, timeout, oversize, epoch, decoder, partial setup, restart\n");
}
