// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// A fake J29 KOMBI for the host tests: a SOME/IP server on 127.0.0.1 that records every request and answers E_OK, or
// an error for the requests chosen with rejectNth / rejectUntil. Optional magic cookies like the real one's.
#ifndef FAKE_KOMBI_HPP
#define FAKE_KOMBI_HPP

#include "cluster/j29_graph.hpp"
#include <assert.h>
#include <pthread.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <vector>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

using nbt::kombinav::Bytes;

inline double fakeKombiMonoS() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

// One request as the KOMBI received it: the 16-byte SOME/IP header, its method, the body, and which connection
// (0 = the first accepted) it came on.
struct Seen { uint16_t method; Bytes body; Bytes header; int conn; };

class FakeKombi {
public:
    enum Reject { kNone, kNth, kUntil };
    FakeKombi() : reject_(kNone), nth_(0), until_(0), cookie_ms_(0), stop_(false) {
        pthread_mutex_init(&mu_, NULL);
        srv_ = socket(AF_INET, SOCK_STREAM, 0);
        int one = 1;
        setsockopt(srv_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        struct sockaddr_in a;
        memset(&a, 0, sizeof(a));
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        assert(bind(srv_, (struct sockaddr*)&a, sizeof(a)) == 0 && listen(srv_, 8) == 0);
        socklen_t l = sizeof(a);
        getsockname(srv_, (struct sockaddr*)&a, &l);
        port_ = ntohs(a.sin_port);
        pthread_create(&acc_, NULL, &FakeKombi::acceptStub, this);
    }
    ~FakeKombi() {
        pthread_mutex_lock(&mu_);
        stop_ = true;
        pthread_mutex_unlock(&mu_);
        shutdown(srv_, SHUT_RDWR);
        close(srv_);
        pthread_join(acc_, NULL);
        dropAll();
        usleep(50000);
    }
    uint16_t port() const { return port_; }
    void rejectNth(size_t n) { pthread_mutex_lock(&mu_); reject_ = kNth; nth_ = n; pthread_mutex_unlock(&mu_); }
    void rejectUntil(double t) { pthread_mutex_lock(&mu_); reject_ = kUntil; until_ = t; pthread_mutex_unlock(&mu_); }
    void cookies(int ms) { pthread_mutex_lock(&mu_); cookie_ms_ = ms; pthread_mutex_unlock(&mu_); }
    bool stopping() { pthread_mutex_lock(&mu_); bool s = stop_; pthread_mutex_unlock(&mu_); return s; }
    std::vector<Seen> seen() { pthread_mutex_lock(&mu_); std::vector<Seen> s = seen_; pthread_mutex_unlock(&mu_); return s; }
    size_t count() { pthread_mutex_lock(&mu_); size_t n = seen_.size(); pthread_mutex_unlock(&mu_); return n; }
    void dropAll() {   // the KOMBI closes every connection (link blip)
        pthread_mutex_lock(&mu_);
        for (size_t i = 0; i < conns_.size(); ++i) shutdown(conns_[i], SHUT_RDWR);
        pthread_mutex_unlock(&mu_);
    }

private:
    static void* acceptStub(void* p) { static_cast<FakeKombi*>(p)->acceptLoop(); return NULL; }
    void acceptLoop() {
        while (!stopping()) {
            int c = accept(srv_, NULL, NULL);
            if (c < 0) return;
            pthread_mutex_lock(&mu_);
            conns_.push_back(c);
            int index = (int)conns_.size() - 1;
            int cookie_ms = cookie_ms_;
            pthread_mutex_unlock(&mu_);
            Conn* k = new Conn;
            k->self = this; k->fd = c; k->index = index;
            pthread_t t;
            pthread_create(&t, NULL, &FakeKombi::connStub, k);
            pthread_detach(t);
            if (cookie_ms) {
                Conn* k2 = new Conn;
                k2->self = this; k2->fd = c;
                pthread_create(&t, NULL, &FakeKombi::cookieStub, k2);
                pthread_detach(t);
            }
        }
    }
    struct Conn { FakeKombi* self; int fd; int index; };
    static bool rd(int fd, uint8_t* p, size_t n) {
        while (n) { ssize_t r = recv(fd, p, n, 0); if (r <= 0) return false; p += r; n -= r; }
        return true;
    }
    static void* connStub(void* p) {
        Conn* k = static_cast<Conn*>(p);
        FakeKombi* s = k->self;
        int fd = k->fd;
        int index = k->index;
        delete k;
        for (;;) {
            uint8_t h[16];
            if (!rd(fd, h, 16)) break;
            uint32_t len = ((uint32_t)h[4] << 24) | (h[5] << 16) | (h[6] << 8) | h[7];
            Bytes body(len - 8);
            if (len > 8 && !rd(fd, &body[0], body.size())) break;
            Seen x;
            x.method = (uint16_t)((h[2] << 8) | h[3]);
            x.body = body;
            x.header.assign(h, h + 16);
            x.conn = index;
            pthread_mutex_lock(&s->mu_);
            s->seen_.push_back(x);
            size_t n = s->seen_.size();
            bool bad = (s->reject_ == kNth && n == s->nth_) || (s->reject_ == kUntil && fakeKombiMonoS() < s->until_);
            pthread_mutex_unlock(&s->mu_);
            uint8_t r[16];
            memcpy(r, h, 16);
            r[4] = 0; r[5] = 0; r[6] = 0; r[7] = 8; r[14] = 0x80; r[15] = bad ? 1 : 0;
            if (send(fd, r, 16, MSG_NOSIGNAL) != 16) break;
        }
        close(fd);
        return NULL;
    }
    static void* cookieStub(void* p) {
        Conn* k = static_cast<Conn*>(p);
        FakeKombi* s = k->self;
        int fd = k->fd;
        delete k;
        const uint8_t cookie[16] = { 0xFF, 0xFF, 0x80, 0x00, 0, 0, 0, 8, 0xDE, 0xAD, 0xBE, 0xEF, 1, 1, 2, 0 };
        pthread_mutex_lock(&s->mu_);
        int ms = s->cookie_ms_;
        pthread_mutex_unlock(&s->mu_);
        while (!s->stopping()) {
            usleep(ms * 1000);
            if (send(fd, cookie, 16, MSG_NOSIGNAL) != 16) break;
        }
        return NULL;
    }

    int srv_;
    uint16_t port_;
    pthread_t acc_;
    pthread_mutex_t mu_;
    std::vector<Seen> seen_;
    std::vector<int> conns_;
    Reject reject_;               // guarded by mu_ (like everything below)
    size_t nth_;
    double until_;
    int cookie_ms_;
    bool stop_;
};

#endif
