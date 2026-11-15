// Internal methods of Session::Impl; recovered native control contract.
Address address(uint32_t ip, unsigned port) {
    Address a = {}; a.length = sizeof(a); a.family = 2;
    a.ip = ip; a.port = uint16_t((port << 8) | (port >> 8)); return a;
}
bool wait_fd(int fd, short events, bool cleanup = false) {
    for (int n = 0; n < (cleanup ? 10 : 20) && (cleanup || !stopped()); ++n) {
        PollFD p = { fd, events, 0 };
        if (poll(&p, 1, 100) > 0) {
            bool ready = (p.revents & events) && !(p.revents & PollErrorOrHangup);
            if (!ready) { printf("[KombiPiP] Socket not ready fd=%d events=%x returned=%x\n", fd, events, p.revents); fflush(0); }
            return ready;
        }
    }
    if (cleanup || !stopped()) { printf("[KombiPiP] Socket wait timed out fd=%d events=%x\n", fd, events); fflush(0); }
    return false;
}
int open_tcp(uint32_t ip, unsigned port) {
    int fd = socket(2, 1, 0);
    if (fd < 0) return -1;
    Address local = address(ProducerIP, 0);
    if (bind(fd, &local, sizeof(local)) < 0) { ::close(fd); return -1; }
    // QNX 6 ARM: F_SETFL=4, O_NONBLOCK=0x80. The worker never blocks the HMI.
    if (fcntl(fd, 4, 0x80) < 0) { ::close(fd); return -1; }
    Address a = address(ip, port);
    if (connect(fd, &a, sizeof(a)) != 0) {
        int error = -1; unsigned size = sizeof(error);
        if (!wait_fd(fd, PollWrite) || getsockopt(fd, 0xffff, 0x1007, &error, &size) < 0 || error) {
            ::close(fd); return -1;
        }
    }
    return fd;
}
bool transfer(int fd, uint8_t* data, unsigned size, bool sending, bool cleanup = false) {
    unsigned retries = 0;
    while (size && (cleanup || !stopped())) {
        if (!wait_fd(fd, sending ? PollWrite : PollRead, cleanup)) return false;
        int n = sending ? send(fd, data, size, 0x0800) : recv(fd, data, size, 0);
        int error = n < 0 ? *__get_errno_ptr() : 0;
        // Readiness is a snapshot; nonblocking I/O may still be interrupted or
        // return EAGAIN. Retry without losing the partial message, with a bound.
        if (n < 0 && (error == 4 || error == 11) && retries++ < 20) {
            usleep(10000);
            continue;
        }
        if (n <= 0 || unsigned(n) > size) {
            printf("[KombiPiP] Socket transfer failed fd=%d sending=%u result=%d errno=%d remaining=%u\n", fd, unsigned(sending), n, error, size); fflush(0);
            return false;
        }
        retries = 0;
        size -= unsigned(n); data += n;
    }
    return !size;
}
void subscribe(int udp, unsigned port, unsigned ttl) {
    uint8_t p[56] = {};
    put16(p, 0xffff); put16(p + 2, 0x8100); put32(p + 4, 48);
    put16(p + 10, discovery_session); p[12] = p[13] = 1; p[14] = 2;
    p[16] = discovery_reboot ? 0xc0 : 0x40; put32(p + 20, 16);
    if (++discovery_session > 65535) { discovery_session = 1; discovery_reboot = false; }
    p[24] = 6; p[27] = 0x10; put16(p + 28, 0xb009); put16(p + 30, 1);
    p[32] = 1; p[35] = uint8_t(ttl); put16(p + 38, 1); put32(p + 40, 12);
    put16(p + 44, 9); p[46] = 4;
    p[48] = 160; p[49] = 48; p[50] = 199; p[51] = 120;
    p[53] = 6; put16(p + 54, port);
    Address a = address(0x60c730a0, 30490);
    sendto(udp, p, sizeof(p), 0, &a, sizeof(a));
}
int message(int fd, uint8_t* p, unsigned capacity, bool cleanup = false, bool skip_cookies = true) {
    for (unsigned cookies = 0; cookies < 16; ++cookies) {
        if (!transfer(fd, p, 16, false, cleanup)) return -1;
        unsigned length = get32(p + 4);
        if (length < 8 || length > capacity - 8) return -1;
        if (!transfer(fd, p + 16, length - 8, false, cleanup)) return -1;
        if (!skip_cookies || get32(p) != 0xffff8000) return int(length + 8);
    }
    return -1;
}
bool guidance_call(int fd, unsigned method, uint8_t* body, unsigned size,
                          unsigned session, uint32_t* handle = 0, bool cleanup = false) {
    uint8_t h[528] = {}, reply[4096];
    if (size > sizeof(h) - 16) return false;
    put16(h, 0xb006); put16(h + 2, method); put32(h + 4, size + 8);
    put16(h + 8, 0x434e); put16(h + 10, session); h[12] = h[13] = 1;
    for (unsigned i = 0; i < size; ++i) h[16 + i] = body[i];
    // One write avoids the Nagle/delayed-ACK pause between header and body.
    if (!transfer(fd, h, 16 + size, true, cleanup)) {
        printf("[KombiPiP] Guidance write failed method=%u session=%u\n", method, session); fflush(0);
        return false;
    }
    int n = message(fd, reply, sizeof(reply), cleanup);
    if (n < 16 || get32(reply) != (0xb0060000u | method) || get16(reply + 8) != 0x434e
        || get16(reply + 10) != session || reply[12] != 1 || reply[13] != 1
        || reply[14] != 0x80 || reply[15]) {
        printf("[KombiPiP] Guidance reply failed method=%u session=%u bytes=%d type=%u code=%u\n",
               method, session, n, n >= 16 ? reply[14] : 255, n >= 16 ? reply[15] : 255); fflush(0);
        return false;
    }
    if (handle) {
        if (n < 24) return false;
        *handle = get32(reply + 16);
        if (!*handle || *handle == 0xffffffff) return false;
    }
    return true;
}
unsigned create_string(uint8_t* p, const char* s) {
    unsigned n = 0; while (s[n]) ++n;
    put32(p, 4 + n * 2); put16(p + 4, 0xfeff);
    for (unsigned i = 0; i < n; ++i) put16(p + 6 + i * 2, uint8_t(s[i]));
    put16(p + 6 + n * 2, 0); return 8 + n * 2;
}
bool guidance_start(int fd, uint32_t& handle, unsigned& session,
                    const NativePipResource& pip = NativePipResource{620,313,0,0}) {
    uint8_t body[512]; unsigned size = 0;
    size += create_string(body + size, "BMW.IC.Guidance");
    size += create_string(body + size, "ICGuidanceController");
    size += create_string(body + size, "BMW AG");
    put32(body + size, 3); put32(body + size + 4, 2); put32(body + size + 8, 16);
    if (!guidance_call(fd, 1, body, size + 12, session++, &handle)) return false;
    // PiP resource: resource type 2 / variant 3, fixed two-u16 dimensions,
    // followed by the two native one-byte configuration enums.
    const uint32_t resource = 0x02000000;
    put32(body, handle); put32(body + 4, resource); body[8] = 2;
    // Method 2 is ric_checkResource; method 3 below sets its configuration.
    if (!guidance_call(fd, 2, body, 9, session++)) return false;
    put32(body + 9, 11); put32(body + 13, 3);
    put32(body + 17, resource); body[21] = 2;
    put16(body + 22, pip.width); put16(body + 24, pip.height);
    body[26] = pip.color_depth; body[27] = pip.packaging;
    if (!guidance_call(fd, 3, body, 28, session++)
        || !guidance_call(fd, 4, body, 4, session++)) return false;
    for (unsigned i = 0; i < nbt::kombinav::tmpl::phase_b_count; ++i) {
        const nbt::kombinav::tmpl::Frame& f = nbt::kombinav::tmpl::phase_b[i];
        if (f.len > sizeof(body) || f.len < 4) return false;
        for (unsigned j = 0; j < f.len; ++j) body[j] = f.body[j];
        put32(body, handle);
        // Detailed maneuver's high-guidance resource (not the trailing model ID).
        if (f.method == 11 && f.len == 71 && body[8] == 7) put32(body + 35, resource);
        // Synthetic 1000 m GS3 threshold, greater than this route's 180 m distance.
        if (f.method == 10 && f.len == 59 && body[8] == 34) {
            put32(body + 38, 0x408f4000); put32(body + 42, 0);
        }
        unsigned body_size = unsigned(f.len);
        if (!clearNavigationLabels(f.method, body, body_size)
            || !guidance_call(fd, f.method, body, body_size, session++)) return false;
        refresh();
    }
    printf("[KombiPiP] Native PiP guidance configured: handle=%u resource=%ux%u colorDepth=%u packagingType=%u\n",
           handle, unsigned(pip.width), unsigned(pip.height), unsigned(pip.color_depth), unsigned(pip.packaging)); fflush(0);
    return true;
}
bool sink_ready(int fd, bool initial = true) {
    uint8_t p[64];
    int n = message(fd, p, sizeof(p), false, initial);
    if (!initial && n == 16 && get32(p) == 0xffff8000) return true;
    if (n != 29) { printf("[KombiPiP] Unexpected sink message bytes=%d id=%08x\n", n, n >= 4 ? get32(p) : 0); fflush(0); return false; }
    unsigned length = get32(p + 4);
    // DisplaySink.newDisplaySink: scale(double), width(u16), height(u16), selection(u8).
    return get16(p) == 0xb009 && get16(p + 2) == 0x8001 && length == 21
        && p[12] == 1 && p[13] == 1 && p[14] == 2 && p[15] == 0
        && get16(p + 24) == 620 && get16(p + 26) == 313 && p[28] == 1;
}
bool guidance_heartbeat(int fd) {
    PollFD incoming = {fd, PollRead, 0};
    if (poll(&incoming, 1, 0) <= 0) return true;
    uint8_t p[16];
    // No guidance RPC is outstanding while streaming. Consume the server's
    // periodic cookie so it cannot accumulate in front of the later Stop reply.
    return message(fd, p, sizeof(p), false, false) == 16 && get32(p) == 0xffff8000;
}
