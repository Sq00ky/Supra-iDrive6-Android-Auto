// Included inside Control: these helpers share its sockets and reply state.
// Every operation uses the same absolute deadline for header/body progress.
// Cleanup may ignore a stop request, but it still obeys its bounded deadline.
// Values below are milliseconds unless they describe a SOME/IP byte count.
enum {
    PollSliceMilliseconds = 100,
    OperationTimeoutMilliseconds = 2000,
    CleanupOperationTimeoutMilliseconds = 1000,
    SomeIpHeaderBytes = 16,
    SomeIpLengthBase = 8,
    MaximumCookies = 16
};

Address address(uint32_t ip, unsigned port) {
    Address endpoint = {};
    endpoint.length = sizeof(endpoint);
    endpoint.family = 2;
    endpoint.ip = ip;
    endpoint.port = uint16_t((port << 8) | (port >> 8));
    return endpoint;
}

bool wait_until(int socket_fd, short events, uint32_t deadline, bool cleanup) {
    while (cleanup || !stopped()) {
        // Signed subtraction also handles wraparound of the 32-bit clock.
        int remaining = int32_t(deadline - millis());
        if (remaining <= 0) {
            break;
        }
        PollFD poll_request = {socket_fd, events, 0};
        int poll_result =
            poll(&poll_request, 1,
                 remaining < PollSliceMilliseconds ? remaining : PollSliceMilliseconds);
        if (poll_result < 0 && *__get_errno_ptr() == 4) { // QNX EINTR.
            continue;
        }
        if (poll_result < 0) {
            return false;
        }
        if (poll_result > 0) {
            bool ready =
                (poll_request.revents & events) && !(poll_request.revents & PollErrorOrHangup);
            if (!ready) {
                printf("[KombiPiP] Socket not ready fd=%d events=%x returned=%x\n", socket_fd,
                       events, poll_request.revents);
                fflush(0);
            }
            return ready;
        }
    }
    return false;
}

bool wait_fd(int socket_fd, short events, bool cleanup = false) {
    return wait_until(
        socket_fd, events,
        millis() + (cleanup ? CleanupOperationTimeoutMilliseconds : OperationTimeoutMilliseconds),
        cleanup);
}

int open_tcp(uint32_t ip, unsigned port) {
    int socket_fd = socket(2, 1, 0);
    if (socket_fd < 0) {
        return -1;
    }
    Address local = address(ProducerIP, 0);
    if (bind(socket_fd, &local, sizeof(local)) < 0) {
        ::close(socket_fd);
        return -1;
    }
    // QNX 6 ARM: F_SETFL=4, O_NONBLOCK=0x80. The worker never blocks the HMI.
    if (fcntl(socket_fd, 4, 0x80) < 0) {
        ::close(socket_fd);
        return -1;
    }
    Address remote = address(ip, port);
    if (connect(socket_fd, &remote, sizeof(remote)) != 0) {
        int error = -1;
        unsigned error_size = sizeof(error);
        // QNX SOL_SOCKET=0xffff and SO_ERROR=0x1007 complete nonblocking connect.
        if (!wait_fd(socket_fd, PollWrite) ||
            getsockopt(socket_fd, 0xffff, 0x1007, &error, &error_size) < 0 || error) {
            ::close(socket_fd);
            return -1;
        }
    }
    return socket_fd;
}

bool transfer_until(int socket_fd, uint8_t* data, unsigned size, unsigned& transferred,
                    bool sending, uint32_t deadline, bool cleanup) {
    while (transferred < size && (cleanup || !stopped())) {
        if (!wait_until(socket_fd, sending ? PollWrite : PollRead, deadline, cleanup)) {
            return false;
        }
        // QNX MSG_NOSIGNAL=0x0800: report a closed peer through the return value.
        int transfer_size = sending
                                ? send(socket_fd, data + transferred, size - transferred, 0x0800)
                                : recv(socket_fd, data + transferred, size - transferred, 0);
        int error = transfer_size < 0 ? *__get_errno_ptr() : 0;
        if (transfer_size < 0 && (error == 4 || error == 11)) { // EINTR / EAGAIN.
            usleep(1000);
            continue;
        }
        if (transfer_size <= 0 || unsigned(transfer_size) > size - transferred) {
            printf("[KombiPiP] Socket transfer failed fd=%d sending=%u result=%d errno=%d "
                   "remaining=%u\n",
                   socket_fd, unsigned(sending), transfer_size, error, size - transferred);
            fflush(0);
            return false;
        }
        transferred += unsigned(transfer_size);
    }
    return transferred == size;
}

bool transfer(int socket_fd, uint8_t* data, unsigned size, bool sending, bool cleanup = false) {
    unsigned transferred = 0;
    return transfer_until(
        socket_fd, data, size, transferred, sending,
        millis() + (cleanup ? CleanupOperationTimeoutMilliseconds : OperationTimeoutMilliseconds),
        cleanup);
}

void subscribe(int udp, unsigned port, unsigned ttl) {
    // SOME/IP service discovery: subscribe to DisplaySink event group 1.
    // The IPv4/TCP endpoint option advertises our producer address and the
    // local port of the already-connected sink socket. TTL zero unsubscribes.
    uint8_t packet[56] = {};
    put16(packet, 0xffff);
    put16(packet + 2, 0x8100);
    put32(packet + 4, 48);
    put16(packet + 10, discovery_session);
    packet[12] = packet[13] = 1;
    packet[14] = 2;
    packet[16] = discovery_reboot ? 0xc0 : 0x40;
    put32(packet + 20, 16);
    if (++discovery_session > 65535) {
        discovery_session = 1;
        discovery_reboot = false;
    }
    packet[24] = 6;
    packet[27] = 0x10;
    put16(packet + 28, 0xb009);
    put16(packet + 30, 1);
    packet[32] = 1;
    packet[35] = uint8_t(ttl);
    put16(packet + 38, 1);
    put32(packet + 40, 12);
    put16(packet + 44, 9);
    packet[46] = 4;
    // The target sockaddr word stores the advertised IPv4 octets low to high.
    for (unsigned octet = 0; octet < 4; ++octet) {
        packet[48 + octet] = uint8_t(VideoProfile::ProducerIP >> (octet * 8));
    }
    packet[53] = 6;
    put16(packet + 54, port);
    Address destination = address(VideoProfile::ClusterIP, VideoProfile::DiscoveryPort);
    sendto(udp, packet, sizeof(packet), 0, &destination, sizeof(destination));
}

int message(int socket_fd, uint8_t* packet, unsigned capacity, bool cleanup = false,
            bool skip_cookies = true) {
    // A cookie is a header-only 0xffff8000 keepalive. Initial handshakes skip
    // cookies; streaming callers can consume exactly one and keep pumping.
    for (unsigned cookie_count = 0; cookie_count < MaximumCookies; ++cookie_count) {
        if (!transfer(socket_fd, packet, SomeIpHeaderBytes, false, cleanup)) {
            return -1;
        }
        unsigned encoded_length = get32(packet + 4);
        // SOME/IP length counts the last eight header bytes plus the body.
        if (encoded_length < SomeIpLengthBase || encoded_length > capacity - SomeIpLengthBase) {
            return -1;
        }
        if (!transfer(socket_fd, packet + SomeIpHeaderBytes, encoded_length - SomeIpLengthBase,
                      false, cleanup)) {
            return -1;
        }
        if (!skip_cookies || get32(packet) != 0xffff8000) {
            return int(encoded_length + SomeIpLengthBase);
        }
    }
    return -1;
}

// reply_used/reply_size survive timeout or interruption. A later close() can
// resume the exact partial reply before issuing the first cancellation RPC.
// Malformed framing/mismatched request identity permanently marks the stream
// broken; an explicit negative reply completes the transaction but fails it.
bool finish_guidance(int socket_fd, uint32_t deadline, bool cleanup, uint32_t* returned_handle) {
    for (unsigned cookie_count = 0; cookie_count < MaximumCookies; ++cookie_count) {
        if (reply_used < SomeIpHeaderBytes &&
            !transfer_until(socket_fd, reply_bytes, SomeIpHeaderBytes, reply_used, false, deadline,
                            cleanup)) {
            return false;
        }
        if (!reply_size) {
            unsigned encoded_length = get32(reply_bytes + 4);
            if (encoded_length < SomeIpLengthBase ||
                encoded_length > sizeof(reply_bytes) - SomeIpLengthBase) {
                broken_stream = true;
                return false;
            }
            reply_size = encoded_length + SomeIpLengthBase;
        }
        if (!transfer_until(socket_fd, reply_bytes, reply_size, reply_used, false, deadline,
                            cleanup)) {
            return false;
        }
        unsigned message_size = reply_size;
        reply_used = reply_size = 0;
        if (get32(reply_bytes) == 0xffff8000 && message_size == SomeIpHeaderBytes) {
            continue;
        }
        // Only the exact outstanding request is admissible. Unrelated replies
        // are protocol errors, not something to skip until a request matches.
        // 0xb006 is Guidance; 0x434e is our client ID; protocol/interface
        // versions are 1. Types 0x80/0x81 are response/error response.
        if (get32(reply_bytes) != (0xb0060000u | pending_method) ||
            get16(reply_bytes + 8) != 0x434e || get16(reply_bytes + 10) != pending_session ||
            reply_bytes[12] != 1 || reply_bytes[13] != 1 ||
            (reply_bytes[14] != 0x80 && reply_bytes[14] != 0x81)) {
            broken_stream = true;
            return false;
        }
        pending_reply = false;
        // A matching error response or nonzero return code ends this RPC.
        if (reply_bytes[14] != 0x80 || reply_bytes[15]) {
            return false;
        }
        if (returned_handle) {
            if (message_size < 24) {
                return false;
            }
            *returned_handle = get32(reply_bytes + 16);
            if (!*returned_handle || *returned_handle == 0xffffffff) {
                return false;
            }
        }
        return true;
    }
    return false;
}

bool guidance_call(int socket_fd, unsigned method, uint8_t* body, unsigned size, unsigned session,
                   uint32_t* handle = 0, bool cleanup = false) {
    uint8_t request[528] = {};
    if (size > sizeof(request) - SomeIpHeaderBytes || pending_reply || broken_stream) {
        return false;
    }
    uint32_t started_at = millis(),
             deadline = started_at + (cleanup ? CleanupOperationTimeoutMilliseconds
                                              : OperationTimeoutMilliseconds);
    if (cleanup && int32_t(cleanup_deadline - deadline) < 0) {
        deadline = cleanup_deadline;
    }
    put16(request, 0xb006);
    put16(request + 2, method);
    put32(request + 4, size + SomeIpLengthBase);
    put16(request + 8, 0x434e);
    put16(request + 10, session);
    request[12] = request[13] = 1;
    for (unsigned i = 0; i < size; ++i) {
        request[SomeIpHeaderBytes + i] = body[i];
    }
    unsigned written = 0;
    // One write avoids the Nagle/delayed-ACK pause between header and body.
    if (!transfer_until(socket_fd, request, SomeIpHeaderBytes + size, written, true, deadline,
                        cleanup)) {
        // Sending another request after a partial write would corrupt framing.
        if (written) {
            broken_stream = true;
        }
        printf("[KombiPiP] Guidance write failed method=%u session=%u bytes=%u\n", method, session,
               written);
        fflush(0);
        return false;
    }
    // No second RPC can start until this reply has completed. Record identity
    // only after the entire request was sent; partial writes poison the stream.
    pending_method = method;
    pending_session = session;
    pending_reply = true;
    if (!finish_guidance(socket_fd, deadline, cleanup, handle)) {
        printf("[KombiPiP] Guidance reply failed method=%u session=%u buffered=%u pending=%u "
               "broken=%u elapsed_ms=%u interrupted=%d\n",
               method, session, reply_used, unsigned(pending_reply), unsigned(broken_stream),
               millis() - started_at, int(interrupted));
        fflush(0);
        return false;
    }
    return true;
}

// Guidance strings carry a BE32 byte length, UTF-16BE BOM, characters and NUL.
// Callers supply the captured ASCII controller/vendor names.
unsigned create_string(uint8_t* destination, const char* text) {
    unsigned character_count = 0;
    while (text[character_count]) {
        ++character_count;
    }
    put32(destination, 4 + character_count * 2);
    put16(destination + 4, 0xfeff);
    for (unsigned i = 0; i < character_count; ++i) {
        put16(destination + 6 + i * 2, uint8_t(text[i]));
    }
    put16(destination + 6 + character_count * 2, 0);
    return 8 + character_count * 2;
}

bool guidance_start(int socket_fd, uint32_t& handle, unsigned& session,
                    const NativePipResource& resource_profile = NativePipResource{
                        VideoProfile::ContentWidth, VideoProfile::ContentHeight, 0, 0}) {
    // Create controller (method 1), check/configure the PiP resource (2/3),
    // activate it (4), then replay the captured graph in its original order.
    uint8_t body[512];
    unsigned size = 0;
    size += create_string(body + size, "BMW.IC.Guidance");
    size += create_string(body + size, "ICGuidanceController");
    size += create_string(body + size, "BMW AG");
    put32(body + size, 3);
    put32(body + size + 4, 2);
    put32(body + size + 8, 16);
    if (!guidance_call(socket_fd, 1, body, size + 12, session++, &handle)) {
        return false;
    }
    // PiP resource: resource type 2 / variant 3, fixed two-u16 dimensions,
    // followed by the two native one-byte configuration enums.
    const uint32_t resource = 0x02000000;
    put32(body, handle);
    put32(body + 4, resource);
    body[8] = 2;
    // Method 2 is ric_checkResource; method 3 below sets its configuration.
    if (!guidance_call(socket_fd, 2, body, 9, session++)) {
        return false;
    }
    put32(body + 9, 11);
    put32(body + 13, 3);
    put32(body + 17, resource);
    body[21] = 2;
    put16(body + 22, resource_profile.width);
    put16(body + 24, resource_profile.height);
    body[26] = resource_profile.color_depth;
    body[27] = resource_profile.packaging;
    if (!guidance_call(socket_fd, 3, body, 28, session++) ||
        !guidance_call(socket_fd, 4, body, 4, session++)) {
        return false;
    }
    for (unsigned i = 0; i < nbt::kombinav::tmpl::phase_b_count; ++i) {
        const nbt::kombinav::tmpl::Frame& frame = nbt::kombinav::tmpl::phase_b[i];
        if (frame.len > sizeof(body) || frame.len < 4) {
            return false;
        }
        for (unsigned j = 0; j < frame.len; ++j) {
            body[j] = frame.body[j];
        }
        // Templates start with the captured controller handle. Change only
        // this working copy and the explicitly documented presentation fields.
        put32(body, handle);
        // Detailed maneuver's high-guidance resource (not the trailing model ID).
        if (frame.method == 11 && frame.len == 71 && body[8] == 7) {
            put32(body + 35, resource);
        }
        // Synthetic 1000 m GS3 threshold, greater than this route's 180 m distance.
        if (frame.method == 10 && frame.len == 59 && body[8] == 34) {
            put32(body + 38, 0x408f4000);
            put32(body + 42, 0);
        }
        unsigned body_size = unsigned(frame.len);
        if (!clearNavigationLabels(frame.method, body, body_size) ||
            !guidance_call(socket_fd, frame.method, body, body_size, session++)) {
            return false;
        }
        refresh();
    }
    printf("[KombiPiP] Native PiP guidance configured: handle=%u resource=%ux%u colorDepth=%u "
           "packagingType=%u\n",
           handle, unsigned(resource_profile.width), unsigned(resource_profile.height),
           unsigned(resource_profile.color_depth), unsigned(resource_profile.packaging));
    fflush(0);
    return true;
}

bool sink_ready(int socket_fd, bool initial = true) {
    uint8_t packet[64];
    int message_size = message(socket_fd, packet, sizeof(packet), false, initial);
    if (!initial && message_size == 16 && get32(packet) == 0xffff8000) {
        return true;
    }
    if (message_size != 29) {
        printf("[KombiPiP] Unexpected sink message bytes=%d id=%08x\n", message_size,
               message_size >= 4 ? get32(packet) : 0);
        fflush(0);
        return false;
    }
    unsigned encoded_length = get32(packet + 4);
    // DisplaySink.newDisplaySink: scale(double), width(u16), height(u16), selection(u8).
    return get16(packet) == 0xb009 && get16(packet + 2) == 0x8001 && encoded_length == 21 &&
           packet[12] == 1 && packet[13] == 1 && packet[14] == 2 && packet[15] == 0 &&
           get16(packet + 24) == VideoProfile::ContentWidth &&
           get16(packet + 26) == VideoProfile::ContentHeight &&
           packet[28] == VideoProfile::SinkSelection;
}

bool guidance_heartbeat(int socket_fd) {
    PollFD incoming = {socket_fd, PollRead, 0};
    if (poll(&incoming, 1, 0) <= 0) {
        return true;
    }
    uint8_t packet[16];
    // No guidance RPC is outstanding while streaming. Consume the server's
    // periodic cookie so it cannot accumulate in front of the later Stop reply.
    return message(socket_fd, packet, sizeof(packet), false, false) == 16 &&
           get32(packet) == 0xffff8000;
}
