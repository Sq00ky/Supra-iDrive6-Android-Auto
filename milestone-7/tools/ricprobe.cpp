// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
//
// ricprobe -- one question for the car: does the KOMBI take list-service frames (SOME/IP 0xB00E, see
// src/cluster/ric_list.hpp) from a second client, the way it takes route guidance from nbt-aad?
//
//   ricprobe text <index> <text>     text model 0x03<index>, then the commit
//   ricprobe restore                 puts "Previous title" back into text model 2
//   ricprobe raw <method> <bodyhex>  one frame as given (methods 3, 4, 9 and B only), no commit
//   options (before the command): -a <host> (160.48.199.96)  -p <port> (30502)  -H <handle hex> (64)  -n = no commit
//
// The test: iDrive playing Bluetooth audio from the phone, Android Auto stopped. Roll the thumb wheel: the cluster
// lists "Previous title" / the track / "Next title". Run `ricprobe text 2 "TEST 123"`, roll the wheel again: the first
// row reads TEST 123 if the KOMBI took it. `ricprobe restore` puts the label back (it also comes back with a restart of
// the head unit). Nothing is created or disposed, and only this one service is spoken to.
#include "cluster/ric_list.hpp"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>

using namespace nbt;
using namespace nbt::riclist;

static const uint16_t kClient = 0x7E01;   // the id nbt-aad uses for route guidance; iDrive is 0x0001

static void hexLine(const char* what, const Bytes& b) {
    printf("%s (%u bytes):", what, (unsigned)b.size());
    for (size_t i = 0; i < b.size() && i < 96; ++i) printf(" %02x", b[i]);
    printf("%s\n", b.size() > 96 ? " ..." : "");
}

static int connectTo(const char* host, int port, int timeout_ms) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)port);
    if (inet_aton(host, &a.sin_addr) == 0) { close(fd); errno = EINVAL; return -1; }
    int fl = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, fl | O_NONBLOCK);
    int rc = connect(fd, (struct sockaddr*)&a, sizeof(a));
    if (rc != 0 && errno != EINPROGRESS) { int e = errno; close(fd); errno = e; return -1; }
    if (rc != 0) {
        fd_set w;
        FD_ZERO(&w);
        FD_SET(fd, &w);
        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        if (select(fd + 1, NULL, &w, NULL, &tv) <= 0) { close(fd); errno = ETIMEDOUT; return -1; }
        int err = 0;
        socklen_t l = sizeof(err);
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &l);
        if (err) { close(fd); errno = err; return -1; }
    }
    fcntl(fd, F_SETFL, fl);
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    return fd;
}

static bool readN(int fd, uint8_t* p, size_t n) {
    size_t got = 0;
    while (got < n) {
        ssize_t r = recv(fd, p + got, n - got, 0);
        if (r == 0) { errno = ECONNRESET; return false; }
        if (r < 0) { if (errno == EINTR) continue; return false; }
        got += (size_t)r;
    }
    return true;
}

// One whole SOME/IP message. The KOMBI's magic cookies (service 0xFFFF) are skipped.
static bool readMessage(int fd, Bytes& m) {
    for (int skipped = 0; skipped < 8; ++skipped) {
        uint8_t h[8];
        if (!readN(fd, h, 8)) return false;
        uint32_t len = ((uint32_t)h[4] << 24) | ((uint32_t)h[5] << 16) | ((uint32_t)h[6] << 8) | h[7];
        if (len < 8 || len > 65536) { errno = EPROTO; return false; }
        m.assign(h, h + 8);
        m.resize(8 + len);
        if (!readN(fd, &m[8], len)) return false;
        if (m[0] == 0xFF && m[1] == 0xFF) { printf("  (magic cookie from the KOMBI, skipped)\n"); continue; }
        return true;
    }
    errno = EPROTO;
    return false;
}

// Sends one frame and prints what came back. True = the matching E_OK.
static bool exchange(int fd, uint16_t method, const Bytes& body, uint16_t session) {
    if (!methodAllowed(method)) { printf("method 0x%04X is not one this tool sends\n", method); return false; }
    const Bytes req = someipRequest(method, body, kClient, session);
    char what[64];
    snprintf(what, sizeof(what), "TX method 0x%04X session 0x%04X", method, session);
    hexLine(what, body);
    size_t off = 0;
    while (off < req.size()) {
        ssize_t w = send(fd, &req[off], req.size() - off, 0);
        if (w < 0) { if (errno == EINTR) continue; printf("  send failed: %s\n", strerror(errno)); return false; }
        off += (size_t)w;
    }
    Bytes reply;
    if (!readMessage(fd, reply)) { printf("  no reply: %s\n", strerror(errno)); return false; }
    const char* why = replyProblem(reply, method, kClient, session);
    printf("  RX type 0x%02X return code 0x%02X, %u payload bytes: %s\n", reply.size() > 14 ? reply[14] : 0,
           reply.size() > 15 ? reply[15] : 0, reply.size() > 16 ? (unsigned)(reply.size() - 16) : 0, why ? why : "E_OK");
    if (why) hexLine("  reply", reply);
    return why == NULL;
}

static Bytes fromHex(const char* s) {
    Bytes b;
    while (s[0] && s[1]) {
        unsigned v = 0;
        if (sscanf(s, "%2x", &v) != 1) break;
        b.push_back((uint8_t)v);
        s += 2;
    }
    return b;
}

static int usage() {
    printf("ricprobe [-a host] [-p port] [-H handlehex] [-n] text <index> <text> | restore | raw <method> <bodyhex>\n");
    return 2;
}

int main(int argc, char** argv) {
    const char* host = "160.48.199.96";
    int port = 30502;
    uint32_t handle = kHandle;
    bool commit = true;
    int i = 1;
    for (; i < argc && argv[i][0] == '-'; ++i) {
        if (!strcmp(argv[i], "-n")) { commit = false; continue; }
        if (i + 1 >= argc) return usage();
        if (!strcmp(argv[i], "-a")) host = argv[++i];
        else if (!strcmp(argv[i], "-p")) port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-H")) handle = (uint32_t)strtoul(argv[++i], NULL, 16);
        else return usage();
    }
    if (i >= argc) return usage();
    const char* cmd = argv[i];
    uint16_t method = kMethodText;
    Bytes body;
    if (!strcmp(cmd, "text") && i + 2 < argc) {
        const uint32_t index = (uint32_t)strtoul(argv[i + 1], NULL, 0);
        if (!textFrame(handle, index, argv[i + 2], body)) { printf("text too long (125 characters at most) or bad index\n"); return 2; }
    } else if (!strcmp(cmd, "restore")) {
        textFrame(handle, kTextPrevLabel, "Previous title", body);
    } else if (!strcmp(cmd, "raw") && i + 2 < argc) {
        method = (uint16_t)strtoul(argv[i + 1], NULL, 16);
        body = fromHex(argv[i + 2]);
        commit = false;
        if (!methodAllowed(method)) { printf("method 0x%04X is not one this tool sends (3, 4, 9, B)\n", method); return 2; }
    } else {
        return usage();
    }
    printf("ricprobe: KOMBI list service 0x%04X at %s:%d, handle 0x%X, client 0x%04X\n", kService, host, port, handle, kClient);
    int fd = connectTo(host, port, 3000);
    if (fd < 0) { printf("connect failed: %s\n", strerror(errno)); return 1; }
    printf("connected\n");
    bool ok = exchange(fd, method, body, 0x0101);
    if (ok && commit) ok = exchange(fd, kMethodCommit, commitFrame(handle), 0x0102);
    close(fd);
    printf(ok ? "RESULT: the KOMBI answered E_OK to every frame. Now look at the cluster list.\n"
              : "RESULT: not accepted (see above).\n");
    return ok ? 0 : 1;
}
