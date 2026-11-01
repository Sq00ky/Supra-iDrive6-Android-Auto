// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "kombi_client.hpp"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

namespace nbt {
namespace kombinav {

namespace {

const size_t kMaxReply = 4096;
const int kMaxCookies = 4;

double monoS() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

void setDetail(std::string* d, const char* what, int err = 0) {
    if (!d) return;
    char b[160];
    if (err) snprintf(b, sizeof(b), "%s (%s)", what, strerror(err));
    else snprintf(b, sizeof(b), "%s", what);
    *d = b;
}

// Blocking reads with SO_RCVTIMEO: 0 = ok, else errno-ish (-1 = peer closed).
int recvExact(int fd, uint8_t* p, size_t n) {
    while (n) {
        ssize_t r = recv(fd, p, n, 0);
        if (r == 0) return -1;
        if (r < 0) {
            if (errno == EINTR) continue;
            return errno ? errno : EIO;
        }
        p += r; n -= (size_t)r;
    }
    return 0;
}

int sendAll(int fd, const uint8_t* p, size_t n) {
    while (n) {
        ssize_t w = send(fd, p, n, 0);
        if (w < 0) {
            if (errno == EINTR) continue;
            return errno ? errno : EIO;
        }
        p += w; n -= (size_t)w;
    }
    return 0;
}

uint32_t be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

// One message (header + payload) into out. 0 = ok; -2 = bad length.
int readMessage(int fd, Bytes& out) {
    out.resize(16);
    int e = recvExact(fd, &out[0], 16);
    if (e) return e;
    uint32_t len = be32(&out[4]);
    if (len < 8 || len > kMaxReply) return -2;
    out.resize(8 + len);
    return len > 8 ? recvExact(fd, &out[16], len - 8) : 0;
}

bool isCookie(const Bytes& m) {
    return m.size() >= 4 && m[0] == 0xFF && m[1] == 0xFF && m[2] == 0x80 && m[3] == 0x00;
}

void setTimeouts(int fd, int ms) {
    struct timeval tv;
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

// ---- async-signal-safe emergency cancel --------------------------------------------------------------------------
uint8_t g_emBuf[2048];
size_t g_emLen[8];
size_t g_emCount = 0;
struct sockaddr_in g_emAddr;
volatile int g_emReady = 0;

} // namespace

KombiClient::KombiClient(const std::string& host, uint16_t port, int timeout_ms)
    : host_(host), port_(port), timeout_ms_(timeout_ms), fd_(-1), session_(0) {}

KombiClient::~KombiClient() { close(); }

void KombiClient::close() {
    if (fd_ >= 0) ::close(fd_);
    fd_ = -1;
}

uint16_t KombiClient::nextSession() {
    session_ = (uint16_t)(session_ % 0xFFFE + 1);
    return session_;
}

int KombiClient::connectFd(int timeout_ms) const {
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons(port_);
    if (inet_pton(AF_INET, host_.c_str(), &a.sin_addr) != 1) { errno = EINVAL; return -1; }
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    int fl = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, fl | O_NONBLOCK);
    int rc = connect(fd, (struct sockaddr*)&a, sizeof(a));
    if (rc < 0 && errno != EINPROGRESS) { int e = errno; ::close(fd); errno = e; return -1; }
    if (rc < 0) {
        struct pollfd p;
        p.fd = fd; p.events = POLLOUT; p.revents = 0;
        int pr;
        do { pr = poll(&p, 1, timeout_ms); } while (pr < 0 && errno == EINTR);
        int err = 0;
        socklen_t el = sizeof(err);
        if (pr <= 0) err = pr == 0 ? ETIMEDOUT : errno;
        else if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &el) < 0) err = errno;
        if (err) { ::close(fd); errno = err; return -1; }
    }
    fcntl(fd, F_SETFL, fl & ~O_NONBLOCK);
    setTimeouts(fd, timeout_ms_);
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    return fd;
}

// Sends one request and waits for its reply (cookies skipped). false + rejected=false: connection problem;
// false + rejected=true: the KOMBI answered with something other than a matching E_OK.
bool KombiClient::sendFrame(int fd, uint16_t method, const Bytes& body, uint16_t session, std::string* detail,
                            bool* rejected) const {
    *rejected = false;
    if (!methodAllowed(method)) { *rejected = true; setDetail(detail, "method not whitelisted"); return false; }
    Bytes req = someipRequest(method, body, kClientId, session);
    int e = sendAll(fd, &req[0], req.size());
    if (e) { setDetail(detail, "send failed", e); return false; }
    Bytes reply;
    for (int cookies = 0;; ++cookies) {
        e = readMessage(fd, reply);
        if (e == -1) { setDetail(detail, "connection closed by the KOMBI"); return false; }
        if (e == -2) { setDetail(detail, "invalid SOME/IP length"); return false; }
        if (e) { setDetail(detail, "no reply", e); return false; }
        if (!isCookie(reply)) break;
        if (cookies >= kMaxCookies) { setDetail(detail, "only magic cookies received"); return false; }
    }
    uint16_t svc = (uint16_t)((reply[0] << 8) | reply[1]), rm = (uint16_t)((reply[2] << 8) | reply[3]);
    uint16_t cid = (uint16_t)((reply[8] << 8) | reply[9]), rs = (uint16_t)((reply[10] << 8) | reply[11]);
    if (svc != 0xB006 || rm != method || cid != kClientId || rs != session || reply[14] != 0x80 || reply[15] != 0x00) {
        *rejected = true;
        if (detail) {
            char b[120];
            snprintf(b, sizeof(b), "method 0x%04X: reply type 0x%02X return code 0x%02X (not a matching E_OK)",
                     method, reply[14], reply[15]);
            *detail = b;
        }
        return false;
    }
    return true;
}

// Discards queued KOMBI messages (cookies while we sit idle). false = the connection is unusable.
bool KombiClient::drain(std::string* detail) {
    for (;;) {
        struct pollfd p;
        p.fd = fd_; p.events = POLLIN; p.revents = 0;
        if (poll(&p, 1, 0) <= 0) return true;
        Bytes m;
        int e = readMessage(fd_, m);
        if (e) { setDetail(detail, e == -1 ? "connection closed by the KOMBI" : "drain failed"); return false; }
    }
}

KombiClient::Result KombiClient::send(const std::vector<Frame>& frames, std::string* detail) {
    for (size_t i = 0; i < frames.size(); ++i) {
        bool done = false;
        for (int attempt = 1; attempt <= 2 && !done; ++attempt) {
            if (fd_ < 0) {
                fd_ = connectFd(timeout_ms_);
                if (fd_ < 0) {
                    setDetail(detail, "connect failed", errno);
                    if (attempt == 2) return kLink;
                    continue;
                }
            }
            bool rejected = false;
            if (drain(detail) && sendFrame(fd_, frames[i].method, frames[i].body, nextSession(), detail, &rejected)) {
                done = true;
                break;
            }
            if (rejected) return kRejected;
            close();
            if (attempt == 2) return kLink;
        }
    }
    return kOk;
}

bool KombiClient::runPhase(const std::vector<Frame>& frames, uint16_t session0, std::string* detail) const {
    int fd = connectFd(timeout_ms_);
    if (fd < 0) { setDetail(detail, "connect failed", errno); return false; }
    bool ok = true;
    for (size_t i = 0; i < frames.size() && ok; ++i) {
        bool rejected = false;
        ok = sendFrame(fd, frames[i].method, frames[i].body, (uint16_t)(session0 + i), detail, &rejected);
    }
    ::close(fd);
    return ok;
}

bool KombiClient::cancel(const Graph& g, double retry_s, double wait_s, AbortFn abort, void* user,
                         std::string* detail) {
    std::vector<Frame> full, s0;
    g.cancel(full);
    g.cancelState0(s0);
    double t_end = monoS() + retry_s;
    for (;;) {
        close();
        if (runPhase(full, 400, detail) || runPhase(s0, 500, detail)) return true;
        if (monoS() + wait_s > t_end || (abort && abort(user))) return false;
        for (double t = 0; t < wait_s && !(abort && abort(user)); t += 0.1) usleep(100000);
        if (abort && abort(user)) return false;
    }
}

void KombiClient::prepareEmergency(const Graph& g) {
    g_emReady = 0;
    std::vector<Frame> fr;
    g.cancel(fr);
    size_t off = 0, n = 0;
    for (size_t i = 0; i < fr.size() && n < sizeof(g_emLen) / sizeof(g_emLen[0]); ++i) {
        Bytes req = someipRequest(fr[i].method, fr[i].body, kClientId, (uint16_t)(700 + i));
        if (off + req.size() > sizeof(g_emBuf)) return;
        memcpy(g_emBuf + off, &req[0], req.size());
        off += req.size();
        g_emLen[n++] = req.size();
    }
    memset(&g_emAddr, 0, sizeof(g_emAddr));
    g_emAddr.sin_family = AF_INET;
    g_emAddr.sin_port = htons(port_);
    if (inet_pton(AF_INET, host_.c_str(), &g_emAddr.sin_addr) != 1) return;
    g_emCount = n;
    g_emReady = 1;
}

// Async-signal-safe: socket/fcntl/connect/poll/setsockopt/send/recv/close only, no allocation.
void KombiClient::emergencyCancel() {
    if (!g_emReady) return;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return;
    int fl = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, fl | O_NONBLOCK);
    if (connect(fd, (struct sockaddr*)&g_emAddr, sizeof(g_emAddr)) < 0) {
        if (errno != EINPROGRESS) { ::close(fd); return; }
        struct pollfd p;
        p.fd = fd; p.events = POLLOUT; p.revents = 0;
        int err = 0;
        socklen_t el = sizeof(err);
        if (poll(&p, 1, 1000) <= 0 || getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &el) < 0 || err) {
            ::close(fd);
            return;
        }
    }
    fcntl(fd, F_SETFL, fl & ~O_NONBLOCK);
    setTimeouts(fd, 1000);
    size_t off = 0;
    for (size_t i = 0; i < g_emCount; ++i) {
        if (sendAll(fd, g_emBuf + off, g_emLen[i])) break;
        off += g_emLen[i];
        uint8_t h[16];
        int cookies = 0;
        for (;;) {   // the reply (skip up to 4 cookies); its content doesn't change what we do next
            if (recvExact(fd, h, 16)) { cookies = -1; break; }
            uint32_t len = be32(h + 4);
            if (len < 8 || len > kMaxReply) { cookies = -1; break; }
            uint8_t skip[64];
            size_t left = len - 8;
            while (left) {
                size_t k = left < sizeof(skip) ? left : sizeof(skip);
                if (recvExact(fd, skip, k)) { left = 0; cookies = -1; break; }
                left -= k;
            }
            if (cookies < 0) break;
            if (!(h[0] == 0xFF && h[1] == 0xFF && h[2] == 0x80 && h[3] == 0x00) || ++cookies > kMaxCookies) break;
        }
        if (cookies < 0) break;
    }
    ::close(fd);
}

} // namespace kombinav
} // namespace nbt
