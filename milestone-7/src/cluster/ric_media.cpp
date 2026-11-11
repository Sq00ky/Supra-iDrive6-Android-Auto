// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// See cluster/ric_media.hpp.
#include "cluster/ric_media.hpp"

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>

#ifdef __QNXNTO__
#include <sys/ioctl.h>
#include <net/if.h>
#include <net/bpf.h>
#endif

namespace nbt {
namespace riclist {

namespace {

uint32_t nowMs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint32_t)ts.tv_sec * 1000u + (uint32_t)(ts.tv_nsec / 1000000));
}

int connectTo(const char* host, int port, int connect_ms, int io_ms) {
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
        tv.tv_sec = connect_ms / 1000;
        tv.tv_usec = (connect_ms % 1000) * 1000;
        if (select(fd + 1, NULL, &w, NULL, &tv) <= 0) { close(fd); errno = ETIMEDOUT; return -1; }
        int err = 0;
        socklen_t l = sizeof(err);
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &l);
        if (err) { close(fd); errno = err; return -1; }
    }
    fcntl(fd, F_SETFL, fl);
    struct timeval tv;
    tv.tv_sec = io_ms / 1000;
    tv.tv_usec = (io_ms % 1000) * 1000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    return fd;
}

bool writeAll(int fd, const Bytes& b) {
    size_t off = 0;
    while (off < b.size()) {
#ifdef MSG_NOSIGNAL
        ssize_t w = send(fd, &b[off], b.size() - off, MSG_NOSIGNAL);
#else
        ssize_t w = send(fd, &b[off], b.size() - off, 0);
#endif
        if (w < 0) { if (errno == EINTR) continue; return false; }
        off += (size_t)w;
    }
    return true;
}

bool readN(int fd, uint8_t* p, size_t n) {
    size_t got = 0;
    while (got < n) {
        ssize_t r = recv(fd, p + got, n - got, 0);
        if (r == 0) { errno = ECONNRESET; return false; }
        if (r < 0) { if (errno == EINTR) continue; return false; }
        got += (size_t)r;
    }
    return true;
}

// One whole SOME/IP message; the KOMBI's magic cookies (service 0xFFFF, about every 10 s) are skipped.
bool readMessage(int fd, Bytes& m) {
    for (int skipped = 0; skipped < 8; ++skipped) {
        uint8_t h[8];
        if (!readN(fd, h, 8)) return false;
        const uint32_t len = ((uint32_t)h[4] << 24) | ((uint32_t)h[5] << 16) | ((uint32_t)h[6] << 8) | h[7];
        if (len < 8 || len > 4096) { errno = EPROTO; return false; }
        m.assign(h, h + 8);
        m.resize(8 + len);
        if (!readN(fd, &m[8], len)) return false;
        if (m[0] == 0xFF && m[1] == 0xFF) continue;
        return true;
    }
    errno = EPROTO;
    return false;
}

#ifdef __QNXNTO__
// bpfRecords() (cluster/ric_sniff.hpp) reads the record header by offset: these are the offsets it assumes.
typedef char bpf_caplen_is_at_8[offsetof(struct bpf_hdr, bh_caplen) == 8 ? 1 : -1];
typedef char bpf_hdrlen_is_at_16[offsetof(struct bpf_hdr, bh_hdrlen) == 16 ? 1 : -1];

// The head unit's own traffic to the KOMBI, read from a BPF device on its interface. Not promiscuous, receive only.
class BpfSource : public FrameSource {
public:
    BpfSource(const std::string& ifname, uint32_t ip) : if_(ifname), ip_(ip), fd_(-1) {}
    virtual ~BpfSource() { close(); }

    virtual bool open(std::string* why) {
        close();
        fd_ = ::open("/dev/bpf", O_RDONLY);
        for (int i = 0; fd_ < 0 && i < 8; ++i) {
            char p[24];
            snprintf(p, sizeof(p), "/dev/bpf%d", i);
            fd_ = ::open(p, O_RDONLY);
        }
        if (fd_ < 0) return fail(why, "no /dev/bpf");
        u_int blen = 32768;
        ioctl(fd_, BIOCSBLEN, &blen);           // a refusal keeps the device's default size
        struct ifreq ifr;
        memset(&ifr, 0, sizeof(ifr));
        strncpy(ifr.ifr_name, if_.c_str(), sizeof(ifr.ifr_name) - 1);
        if (ioctl(fd_, BIOCSETIF, &ifr) < 0) return fail(why, "the interface cannot be attached");
        u_int dlt = 0;
        if (ioctl(fd_, BIOCGDLT, &dlt) < 0 || dlt != DLT_EN10MB) { errno = 0; return fail(why, "not an Ethernet interface"); }
        u_int one = 1;
        if (ioctl(fd_, BIOCIMMEDIATE, &one) < 0) return fail(why, "BIOCIMMEDIATE");
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 200000;
        if (ioctl(fd_, BIOCSRTIMEOUT, &tv) < 0) return fail(why, "BIOCSRTIMEOUT");
        FilterInsn f[kFilterLen];
        kombiFilter(ip_, f);
        struct bpf_insn insns[kFilterLen];
        for (int i = 0; i < kFilterLen; ++i) {
            insns[i].code = f[i].code;
            insns[i].jt = f[i].jt;
            insns[i].jf = f[i].jf;
            insns[i].k = f[i].k;
        }
        struct bpf_program prog;
        prog.bf_len = kFilterLen;
        prog.bf_insns = insns;
        if (ioctl(fd_, BIOCSETF, &prog) < 0) return fail(why, "the packet filter was refused");
        if (ioctl(fd_, BIOCGBLEN, &blen) < 0 || blen < 256) return fail(why, "BIOCGBLEN");
        buf_.resize(blen);
        return true;
    }

    virtual int read(std::vector<Bytes>* frames) {
        if (fd_ < 0) return -1;
        const ssize_t n = ::read(fd_, &buf_[0], buf_.size());
        if (n < 0) return (errno == EINTR || errno == EAGAIN || errno == ETIMEDOUT) ? 0 : -1;
        return bpfRecords(&buf_[0], (size_t)n, BPF_ALIGNMENT, frames);
    }

    virtual void close() {
        if (fd_ >= 0) ::close(fd_);
        fd_ = -1;
    }

private:
    bool fail(std::string* why, const char* what) {
        const int e = errno;
        char b[160];
        if (e) snprintf(b, sizeof(b), "%s (%s)", what, strerror(e));
        else snprintf(b, sizeof(b), "%s", what);
        *why = b;
        close();
        return false;
    }

    std::string if_;
    uint32_t ip_;
    int fd_;
    Bytes buf_;
};
#endif

FrameSource* defaultSource(const std::string& ifname, uint32_t ip) {
#ifdef __QNXNTO__
    return new BpfSource(ifname, ip);
#else
    (void)ifname;
    (void)ip;
    return NULL;
#endif
}

}  // namespace

KombiMedia::KombiMedia()
    : running_(false), stop_(false), show_(false), answered_(0), port_(30502), ip_(0), index_(kDefaultTextIndex),
      src_(NULL), ownSrc_(false), fd_(-1), session_(0x0100), fails_(0) {
    pthread_mutex_init(&mu_, NULL);
    pthread_cond_init(&cv_, NULL);
}

KombiMedia::~KombiMedia() {
    stop();
    pthread_cond_destroy(&cv_);
    pthread_mutex_destroy(&mu_);
}

bool KombiMedia::settingOn(const char* env) { return env && !strcmp(env, "1"); }

void KombiMedia::start(bool audioModeOk, FrameSource* source) {
    if (running_) return;
    if (!settingOn(getenv("NBT_KOMBI_MEDIA"))) {
        printf("[KombiMedia] track title in the cluster list: off (NBT_KOMBI_MEDIA=1 turns it on)\n");
        return;
    }
    if (!audioModeOk) {
        printf("[KombiMedia] track title in the cluster list: off, the audio mode is Bluetooth (the car shows the "
               "phone's track itself)\n");
        return;
    }
    const char* e = getenv("NBT_KOMBI_NAV_HOST");
    host_ = (e && *e) ? e : "160.48.199.96";
    e = getenv("NBT_KOMBI_NAV_PORT");
    port_ = (uint16_t)((e && atoi(e) > 0 && atoi(e) < 65536) ? atoi(e) : 30502);
    e = getenv("NBT_KOMBI_MEDIA_IF");
    if_ = (e && *e) ? e : "vlan73";
    e = getenv("NBT_KOMBI_MEDIA_INDEX");
    const unsigned long idx = (e && *e) ? strtoul(e, NULL, 0) : 0;
    index_ = (idx >= 1 && idx <= 0xFFFFFFul - (unsigned long)kIndexSpan) ? (uint32_t)idx : kDefaultTextIndex;
    struct in_addr a;
    if (inet_aton(host_.c_str(), &a) == 0) {
        printf("[KombiMedia] track title in the cluster list: off, '%s' is not an address\n", host_.c_str());
        return;
    }
    ip_ = ntohl(a.s_addr);
    src_ = source ? source : defaultSource(if_, ip_);
    ownSrc_ = !source;
    if (!src_) {
        printf("[KombiMedia] track title in the cluster list: off, no packet capture on this platform\n");
        return;
    }
    stop_ = false;
    if (pthread_create(&thread_, NULL, &KombiMedia::workerStub, this) != 0) {
        printf("[KombiMedia] track title in the cluster list: off, the worker thread could not be started\n");
        if (ownSrc_) delete src_;
        src_ = NULL;
        return;
    }
    running_ = true;
    printf("[KombiMedia] track title in the cluster list: on (NBT_KOMBI_MEDIA=1) - listens on %s while Android Auto "
           "plays, KOMBI %s:%u, own text model 0x%08X\n", if_.c_str(), host_.c_str(), (unsigned)port_,
           0x03000000u | index_);
}

void KombiMedia::stop() {
    if (!running_) return;
    pthread_mutex_lock(&mu_);
    stop_ = true;
    pthread_cond_signal(&cv_);
    pthread_mutex_unlock(&mu_);
    pthread_join(thread_, NULL);
    running_ = false;
    closeLink();
    if (ownSrc_) delete src_;
    src_ = NULL;
}

void KombiMedia::update(const std::string& song, bool playing) {
    if (!running_) return;
    const bool show = playing && !song.empty();
    pthread_mutex_lock(&mu_);
    if (song != title_ || show != show_) {
        title_ = song;
        show_ = show;
        pthread_cond_signal(&cv_);
    }
    pthread_mutex_unlock(&mu_);
}

unsigned KombiMedia::answered() {
    pthread_mutex_lock(&mu_);
    const unsigned n = answered_;
    pthread_mutex_unlock(&mu_);
    return n;
}

void* KombiMedia::workerStub(void* self) {
    static_cast<KombiMedia*>(self)->worker();
    return NULL;
}

void KombiMedia::closeLink() {
    if (fd_ >= 0) close(fd_);
    fd_ = -1;
}

// Every frame in order, each answered E_OK. A link error closes the connection (the next answer reconnects); nothing
// is retried here: iDrive's next list brings the next chance.
bool KombiMedia::sendAll(const std::vector<OutFrame>& frames, std::string* why) {
    char b[160];
    for (size_t i = 0; i < frames.size(); ++i) {
        const uint16_t method = frames[i].method;
        if (!methodAllowed(method)) { *why = "a method this client never sends"; return false; }
        if (fd_ < 0) {
            fd_ = connectTo(host_.c_str(), port_, 1000, 500);
            if (fd_ < 0) { snprintf(b, sizeof(b), "connect failed (%s)", strerror(errno)); *why = b; return false; }
        }
        if (++session_ == 0) session_ = 1;
        const uint16_t ses = session_;
        if (!writeAll(fd_, someipRequest(method, frames[i].body, kOwnClient, ses))) {
            snprintf(b, sizeof(b), "method 0x%04X: send failed (%s)", method, strerror(errno));
            *why = b;
            closeLink();
            return false;
        }
        Bytes reply;
        if (!readMessage(fd_, reply)) {
            snprintf(b, sizeof(b), "method 0x%04X: no reply (%s)", method, strerror(errno));
            *why = b;
            closeLink();
            return false;
        }
        const char* p = replyProblem(reply, method, kOwnClient, ses);
        if (p) {
            snprintf(b, sizeof(b), "method 0x%04X: %s (type 0x%02X, return code 0x%02X)", method, p,
                     reply.size() > 14 ? reply[14] : 0, reply.size() > 15 ? reply[15] : 0);
            *why = b;
            return false;
        }
    }
    return true;
}

void KombiMedia::worker() {
    MediaList logic(index_);
    StreamSniffer sniff;
    bool open = false, dead = false;
    uint32_t idleSince = 0;
    unsigned seenLists = 0, seenOthers = 0, seenMoved = 0, base = 0;
    unsigned long packets = 0, onPort = 0, healthPackets = 0;   // what the capture has delivered since it opened
    uint32_t openedMs = 0, healthMs = 0;
    bool silentLogged = false;
    uint32_t seenHandle = 0;
    std::vector<Bytes> frames;
    std::vector<SomeipMsg> msgs;
    std::vector<OutFrame> out;

    pthread_mutex_lock(&mu_);
    while (!stop_) {
        if (dead || (!show_ && !open)) {        // nothing to do until something plays (or until stop)
            pthread_cond_wait(&cv_, &mu_);
            continue;
        }
        const std::string title = title_;
        const bool show = show_;
        pthread_mutex_unlock(&mu_);

        if (!open) {
            std::string why;
            if (!src_->open(&why)) {
                printf("[KombiMedia] cannot listen on %s: %s. The track title stays off until nbt-aad is restarted.\n",
                       if_.c_str(), why.c_str());
                dead = true;
                pthread_mutex_lock(&mu_);
                continue;
            }
            open = true;
            sniff = StreamSniffer();
            logic = MediaList(index_);
            seenLists = seenOthers = seenMoved = 0;
            seenHandle = 0;
            base = 0;
            idleSince = 0;
            packets = onPort = healthPackets = 0;
            silentLogged = false;
            openedMs = healthMs = nowMs();
            printf("[KombiMedia] listening on %s for iDrive's track list\n", if_.c_str());
        }

        out.clear();
        logic.setTitle(title, show, &out);
        frames.clear();
        if (src_->read(&frames) < 0) {
            printf("[KombiMedia] listening on %s ended (%s). The track title stays off until nbt-aad is restarted.\n",
                   if_.c_str(), strerror(errno));
            src_->close();
            open = false;
            dead = true;
            pthread_mutex_lock(&mu_);
            continue;
        }
        for (size_t i = 0; i < frames.size(); ++i) {
            TcpSeg seg;
            if (frames[i].empty() || !parseTcp(&frames[i][0], frames[i].size(), &seg)) continue;
            if (seg.dst != ip_) continue;
            if (packets++ == 0) printf("[KombiMedia] the capture works: first packet to the KOMBI seen\n");
            if (seg.dport != port_) continue;
            ++onPort;
            msgs.clear();
            sniff.feed(seg, &msgs);
            for (size_t k = 0; k < msgs.size(); ++k) logic.onSniffed(msgs[k], &out);
        }

        if (logic.lists() != seenLists && (seenLists == 0 || logic.handle() != seenHandle)) {
            printf("[KombiMedia] iDrive's track list seen: handle 0x%X, its title text model 0x%08X\n", logic.handle(),
                   logic.idriveTitleId());
            seenHandle = logic.handle();
        }
        seenLists = logic.lists();
        if (logic.others() != seenOthers) {
            if (seenOthers == 0) {
                char hex[2 * 96 + 1];
                const Bytes& o = logic.otherList();
                size_t n = 0;
                for (; n < o.size() && n < 96; ++n) snprintf(hex + 2 * n, 3, "%02x", o[n]);
                hex[2 * n] = 0;
                printf("[KombiMedia] iDrive sent another kind of list (%u bytes), left alone: %s\n", (unsigned)o.size(), hex);
            }
            seenOthers = logic.others();
        }
        if (logic.moved() != seenMoved) {
            printf("[KombiMedia] iDrive used our text model: ours is now 0x%08X\n", logic.textId());
            seenMoved = logic.moved();
        }

        if (!out.empty()) {
            bool text = false;
            for (size_t i = 0; i < out.size(); ++i) text = text || out[i].method == kMethodText;
            std::string why;
            if (sendAll(out, &why)) {
                if (text) printf("[KombiMedia] cluster list: '%s' (text model 0x%08X, handle 0x%X)\n", title.c_str(),
                                 logic.textId(), logic.handle());
                fails_ = 0;
            } else {
                logic.sendFailed();
                if (fails_ < 3 || fails_ % 20 == 0) printf("[KombiMedia] the KOMBI did not take the title: %s\n", why.c_str());
                ++fails_;
            }
        }

        const uint32_t now = nowMs();
        if (packets == 0 && !silentLogged && (uint32_t)(now - openedMs) > 30000u) {
            silentLogged = true;
            printf("[KombiMedia] no packet to the KOMBI seen on %s in 30 s (iDrive sends it a keep-alive about every "
                   "10 s): the capture is not working\n", if_.c_str());
        }
        if (packets != healthPackets && (uint32_t)(now - healthMs) > 60000u) {
            healthMs = now;
            healthPackets = packets;
            printf("[KombiMedia] capture: %lu packets to the KOMBI, %lu on the list port, iDrive's lists %u, answered %u\n",
                   packets, onPort, logic.lists(), logic.answered());
        }
        if (show) idleSince = 0;
        else if (!idleSince) idleSince = now | 1u;
        else if ((uint32_t)(now - idleSince) > 10000u) {       // nothing has played for 10 s: stop listening
            printf("[KombiMedia] stopped listening (nothing playing): answered %u of iDrive's %u lists, %lu packets seen\n",
                   logic.answered(), logic.lists(), packets);
            src_->close();
            closeLink();
            open = false;
        }

        pthread_mutex_lock(&mu_);
        answered_ += logic.answered() - base;       // base = what of this logic's count is already in answered_
        base = logic.answered();
    }
    pthread_mutex_unlock(&mu_);
    if (open) src_->close();
}

}  // namespace riclist
}  // namespace nbt
