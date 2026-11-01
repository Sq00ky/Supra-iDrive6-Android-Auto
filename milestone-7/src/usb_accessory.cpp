// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "usb_accessory.hpp"
#include "usb_aoap.hpp"
#include "usb_direct.hpp"
#include "nbt_env.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <dirent.h>
#include <sched.h>
#include <deque>
#include <sys/socket.h>
#ifdef __QNX__
#include <devctl.h>
#endif

namespace nbt {

static const int kMaxSessionsPerNode = 3;     // then the phone has to be replugged (it stays in accessory mode)
static const double kRetryGapS = 3.0;         // between sessions on the same accessory connection
// A node can show up in /dev before its driver answers (or another client holds it): retry, without that
// counting as a session.
static const int kMaxSwitchTries = 5;         // /dev/android*: open or switch devctl failed
static const int kMaxOpenFails = 10;          // /dev/oap*: open failed
static const double kNodeRetryS = 1.0;
static const int kNotOursTicks = 2;           // scans (~1 s) the owner is not USB before the bridge is ended
static const unsigned kZeroReadsBeforeNap = 64;
// devc-serusb-aoap runs with -l 16384 (umass-enum.cfg) and drops whatever part of a transfer does not fit the
// read buffer: read with plenty of room.
static const size_t kUsbReadBuf = 65536;
static const size_t kSockReadBuf = 16384;
static const unsigned kTraceTransfers = 48;   // per direction and bridge: first transfers logged frame by frame
// devc-serusb-aoap keeps every completed bulk-in transfer in a queue, but it also lets a second client read the same
// node: the media app's A4A handler (NBTMediaMainApp OAPReaderThread) opens /dev/oap* as soon as the phone is in
// accessory mode. A transfer that completes while our read() is not waiting stays queued and goes to whichever
// client reads next (car log 2026-10-31: 12 TLS records in a row missing in a burst of channel opens). So the USB
// reader does nothing but read and queue, at a raised priority, and a second thread feeds the session socket.
static const int kRxPriority = 21;            // like the QNX USB drivers; NBT_USB_RX_PRIO overrides, 0 = leave
static const size_t kMaxRxQueued = 4u << 20;  // bytes read from the phone that the session has not taken yet
static const double kDriverGraceS = 2.0;      // NBT_USB_DIRECT: wait this long after the accessory appears
// NBT_USB_DIRECT: a device that has been plugged in this long without a /dev/android* node is switched by us. One scan:
// medialauncher's own AOA request comes at plug-in, and a phone that was not answering then (still on "charging only")
// is only switched by us anyway, from the moment "Android Auto" is chosen on it (it re-enumerates).
static const double kSelfSwitchAfterS = 0.5;

static double monoSec() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static int doDevctl(int fd, unsigned dcmd, void* data, size_t n) {
#ifdef __QNX__
    return devctl(fd, static_cast<int>(dcmd), data, n, NULL);   // EOK or an errno value
#else
    (void)fd; (void)dcmd; (void)data; (void)n;
    return ENOSYS;
#endif
}

// SIGUSR2 only interrupts a bridge thread's blocking read() / write() on /dev/oap (the driver implements
// io_unblock). Nothing else in the daemon uses it.
static void wakeHandler(int) { }

static void unblockWake() {
    sigset_t s;
    sigemptyset(&s);
    sigaddset(&s, SIGUSR2);
    pthread_sigmask(SIG_UNBLOCK, &s, NULL);
}

static bool contains(const std::vector<std::string>& v, const std::string& s) {
    for (size_t i = 0; i < v.size(); ++i) {
        if (v[i] == s) return true;
    }
    return false;
}

// One bridge. Its threads only ever touch this object, so a bridge that has to be abandoned (stuck in the driver)
// keeps its own fds and cannot disturb the next one.
struct UsbBridge {
    pthread_mutex_t mu;     // guards stop, reader_done, writer_done (QNX GCC 4.4: no atomics)
    std::string node;
    int usb_fd;             // /dev/oap*, or -1 with link
    UsbDirectLink* link;    // NBT_USB_DIRECT: io-usb callbacks fill rx, the writer writes through it
    int sock_fd;
    pthread_cond_t cv;      // with mu: rx queue changed, or stop
    pthread_t reader;       // /dev/oap -> rx queue
    pthread_t fwd;          // rx queue -> socket
    pthread_t writer;       // socket -> /dev/oap
    bool has_reader;
    bool has_fwd;
    bool has_writer;
    bool stop;
    bool reader_done;
    bool fwd_done;
    bool writer_done;
    std::deque<std::vector<uint8_t> > rx;   // guarded by mu: transfers in the order the driver handed them over
    size_t rx_bytes;                         // guarded by mu
    uint64_t bytes_in;      // each written by one thread, read after joining it
    uint64_t bytes_out;
    double start_s;

    UsbBridge(const std::string& n, int ufd, int sfd)
        : node(n), usb_fd(ufd), link(NULL), sock_fd(sfd), reader(0), fwd(0), writer(0), has_reader(false), has_fwd(false),
          has_writer(false), stop(false), reader_done(false), fwd_done(false), writer_done(false), rx_bytes(0),
          bytes_in(0), bytes_out(0), start_s(monoSec()) {
        pthread_mutex_init(&mu, NULL);
        pthread_cond_init(&cv, NULL);
    }
    ~UsbBridge() {
        pthread_cond_destroy(&cv);
        pthread_mutex_destroy(&mu);
    }

    bool get(const bool& f) {
        pthread_mutex_lock(&mu);
        bool v = f;
        pthread_mutex_unlock(&mu);
        return v;
    }
    void set(bool& f, bool v) {
        pthread_mutex_lock(&mu);
        f = v;
        pthread_cond_broadcast(&cv);   // the forwarder waits for the queue or stop
        pthread_mutex_unlock(&mu);
    }
    bool stopping() { return get(stop); }
    bool readerDone() { return !has_reader || get(reader_done); }
    bool fwdDone() { return !has_fwd || get(fwd_done); }
    bool writerDone() { return !has_writer || get(writer_done); }
    // A thread that was never started (the reader with a link) has not ended anything.
    bool anyDone() {
        return (has_reader && get(reader_done)) || (has_fwd && get(fwd_done)) || (has_writer && get(writer_done));
    }
    bool allDone() { return readerDone() && fwdDone() && writerDone(); }
};

static void raisePriority() {
#ifdef __QNX__
    const char* e = getenv("NBT_USB_RX_PRIO");
    int prio = (e && *e) ? atoi(e) : kRxPriority;
    if (prio <= 0) return;
    struct sched_param sp;
    int policy;
    if (pthread_getschedparam(pthread_self(), &policy, &sp) != 0 || sp.sched_priority >= prio) return;
    sp.sched_priority = prio;
    int r = pthread_setschedparam(pthread_self(), policy, &sp);
    if (r != 0) printf("[UsbAccessory] USB reader priority %d not set: %s\n", prio, strerror(r));
#endif
}

static bool writeAll(UsbBridge* b, int fd, const uint8_t* p, size_t n) {
    while (n > 0) {
        if (b->stopping()) return false;
        ssize_t w = write(fd, p, n);
        if (w < 0 && errno == EINTR) continue;   // woken by reapBridge(): stop is checked above
        if (w <= 0) return false;
        p += w;
        n -= static_cast<size_t>(w);
    }
    return true;
}

// After the first kTraceTransfers, one line per second per direction (only while transfers flow, so the last line
// shows when a direction went quiet), and the time of the last transfer when the bridge ends.
struct FlowStats {
    const char* dir;
    double window_s;
    unsigned transfers, frames_bytes;
    unsigned total;
    double last_s;
    FlowStats(const char* d) : dir(d), window_s(0), transfers(0), frames_bytes(0), total(0), last_s(-1) {}
    void add(UsbBridge* b, size_t n, size_t queued);
    void end(UsbBridge* b);
};

void FlowStats::add(UsbBridge* b, size_t n, size_t queued) {
    double now = monoSec() - b->start_s;
    ++total;
    last_s = now;
    if (total <= kTraceTransfers) return;
    if (transfers == 0) window_s = now;
    ++transfers;
    frames_bytes += static_cast<unsigned>(n);
    if (now - window_s >= 1.0) {
        printf("[UsbAccessory] %s +%.1f s: %u transfers, %u bytes in %.1f s (%u waiting)\n", dir, now, transfers,
               frames_bytes, now - window_s, (unsigned)queued);
        transfers = 0;
        frames_bytes = 0;
    }
}

void FlowStats::end(UsbBridge* b) {
    if (last_s < 0) printf("[UsbAccessory] %s: no transfers\n", dir);
    else printf("[UsbAccessory] %s: %u transfers, the last at +%.3f s (bridge ended at +%.3f s)\n", dir, total,
                last_s, monoSec() - b->start_s);
}

// Log one USB transfer as the AA frames in it (experimental bring-up aid; only the first kTraceTransfers).
// carry: bytes of a frame begun in an earlier transfer that are still to come; -1 once the stream is lost.
static void traceTransfer(UsbBridge* b, const char* dir, unsigned k, const uint8_t* p, size_t n, long& carry) {
    char line[320];
    int used = snprintf(line, sizeof(line), "[UsbAccessory] %s #%u +%.3f s: %u bytes:", dir, k,
                        monoSec() - b->start_s, (unsigned)n);
    size_t off = 0;
    if (carry > 0) {
        size_t c = static_cast<size_t>(carry) < n ? static_cast<size_t>(carry) : n;
        used += snprintf(line + used, sizeof(line) - used, " (+%u of previous frame)", (unsigned)c);
        off = c;
        carry -= static_cast<long>(c);
    }
    while (carry >= 0 && off < n && used < (int)sizeof(line) - 40) {
        if (n - off < 4) { carry = -1; break; }   // header split across transfers: stop decoding
        size_t len = aoap::frameLength(p + off, n - off);
        used += snprintf(line + used, sizeof(line) - used, " [ch %u 0x%02X %u]", p[off], p[off + 1], (unsigned)len);
        if (off + len > n) { carry = static_cast<long>(off + len - n); off = n; break; }
        off += len;
    }
    printf("%s%s\n", line, carry < 0 ? " (frame boundaries lost)" : "");
}

// /dev/oap -> rx queue. Back in read() as soon as possible: no logging, no socket calls (those go through io-pkt).
static void* bridgeReader(void* arg) {
    UsbBridge* b = reinterpret_cast<UsbBridge*>(arg);
    unblockWake();
    raisePriority();
    std::vector<uint8_t> buf(kUsbReadBuf);
    const char* why = "stopped";
    unsigned zeros = 0;
    while (!b->stopping()) {
        ssize_t n = read(b->usb_fd, &buf[0], buf.size());
        if (n < 0 && errno == EINTR) continue;   // stop is checked by the loop
        if (n < 0) { why = strerror(errno); break; }
        if (n == 0) {
            // Not the end: the driver answers each read with one USB transfer, and a zero-length packet (ending a
            // message that is a multiple of the max packet size) is a transfer too. The end is an error (the
            // driver gets slain when the phone is unplugged). Just don't spin if it ever returns only zeros.
            if (++zeros >= kZeroReadsBeforeNap) usleep(10000);
            continue;
        }
        zeros = 0;
        pthread_mutex_lock(&b->mu);
        bool full = b->rx_bytes + static_cast<size_t>(n) > kMaxRxQueued;
        if (!full) {
            b->rx.push_back(std::vector<uint8_t>(buf.begin(), buf.begin() + n));
            b->rx_bytes += static_cast<size_t>(n);
            pthread_cond_broadcast(&b->cv);
        }
        pthread_mutex_unlock(&b->mu);
        if (full) { why = "session not reading (receive queue full)"; break; }
    }
    printf("[UsbAccessory] USB reader ended (%s)\n", why);
    b->set(b->stop, true);
    b->set(b->reader_done, true);
    return NULL;
}

// NBT_USB_DIRECT: what bridgeReader does after each read(), called by io-usb for each transfer in order.
static bool directRx(const uint8_t* data, size_t n, void* user) {
    UsbBridge* b = reinterpret_cast<UsbBridge*>(user);
    if (!data) {
        printf("[UsbAccessory] USB reader ended (%s)\n", b->link->why());
        b->set(b->stop, true);
        return false;
    }
    pthread_mutex_lock(&b->mu);
    bool full = b->rx_bytes + n > kMaxRxQueued;
    if (!full) {
        b->rx.push_back(std::vector<uint8_t>(data, data + n));
        b->rx_bytes += n;
        pthread_cond_broadcast(&b->cv);
    }
    pthread_mutex_unlock(&b->mu);
    if (full) {
        printf("[UsbAccessory] USB reader ended (session not reading (receive queue full))\n");
        b->set(b->stop, true);
    }
    return !full;
}

// rx queue -> socket, in order. Ends the session's socket when it ends (the session sees the phone leave).
static void* bridgeForwarder(void* arg) {
    UsbBridge* b = reinterpret_cast<UsbBridge*>(arg);
    unblockWake();
    const char* why = "stopped";
    unsigned transfers = 0;
    long carry = 0;
    size_t most_queued = 0;
    FlowStats stats("in");
    for (;;) {
        pthread_mutex_lock(&b->mu);
        while (b->rx.empty() && !b->stop) pthread_cond_wait(&b->cv, &b->mu);
        if (b->stop) {
            pthread_mutex_unlock(&b->mu);
            break;
        }
        if (b->rx.size() > most_queued) most_queued = b->rx.size();
        std::vector<uint8_t> t;
        t.swap(b->rx.front());
        b->rx.pop_front();
        b->rx_bytes -= t.size();
        size_t queued = b->rx.size();
        pthread_mutex_unlock(&b->mu);
        stats.add(b, t.size(), queued);

        if (transfers < kTraceTransfers) traceTransfer(b, "in", transfers, &t[0], t.size(), carry);
        ++transfers;
        if (!writeAll(b, b->sock_fd, &t[0], t.size())) {
            why = b->stopping() ? "stopped" : "session closed";
            break;
        }
        b->bytes_in += t.size();
    }
    printf("[UsbAccessory] USB forwarder ended (%s; at most %u transfers waited for the session)\n", why,
           (unsigned)most_queued);
    stats.end(b);
    b->set(b->stop, true);
    shutdown(b->sock_fd, SHUT_RDWR);
    b->set(b->fwd_done, true);
    return NULL;
}

// socket -> /dev/oap, one AA frame per bulk transfer (like other head units do).
static void* bridgeWriter(void* arg) {
    UsbBridge* b = reinterpret_cast<UsbBridge*>(arg);
    unblockWake();
    std::vector<uint8_t> pending;
    std::vector<uint8_t> buf(kSockReadBuf);
    const char* why = "session closed";
    unsigned transfers = 0;
    long carry = 0;
    FlowStats stats("out");
    while (true) {
        ssize_t n = read(b->sock_fd, &buf[0], buf.size());
        if (n < 0 && errno == EINTR) {
            if (b->stopping()) { why = "stopped"; break; }
            continue;
        }
        if (n <= 0) break;
        pending.insert(pending.end(), buf.begin(), buf.begin() + n);
        const uint8_t* base = &pending[0];   // not empty: n > 0 bytes were just added
        size_t off = 0;
        bool ok = true;
        for (;;) {
            size_t len = aoap::frameLength(base + off, pending.size() - off);
            if (len == 0 || pending.size() - off < len) break;
            if (transfers < kTraceTransfers) traceTransfer(b, "out", transfers, base + off, len, carry);
            ++transfers;
            if (b->link ? !b->link->write(base + off, len) : !writeAll(b, b->usb_fd, base + off, len)) {
                ok = false;
                break;
            }
            stats.add(b, len, 0);
            b->bytes_out += len;
            off += len;
        }
        pending.erase(pending.begin(), pending.begin() + off);
        if (!ok) { why = b->stopping() ? "stopped" : strerror(errno); break; }
    }
    printf("[UsbAccessory] USB writer ended (%s)\n", why);
    stats.end(b);
    b->set(b->stop, true);
    b->set(b->writer_done, true);
    return NULL;
}

UsbAccessory::UsbAccessory(ClientConnectedCallback callback, void* user_data, OwnerCallback owner, void* owner_data,
                           const char* dev_dir)
    : dev_dir_(dev_dir ? dev_dir : "/dev"),
      client_callback_(callback),
      client_user_data_(user_data),
      owner_(owner),
      owner_data_(owner_data),
      watch_thread_(0),
      is_running_(false),
      phone_present_(false),
      bridge_(NULL),
      not_ours_ticks_(0),
      direct_(NULL),
      next_driver_stop_s_(0),
      direct_drivers_(0),
      direct_leaked_(false) {
    pthread_mutex_init(&flag_mutex_, NULL);
}

UsbAccessory::~UsbAccessory() {
    stop();
    pthread_mutex_destroy(&flag_mutex_);
}

// QNX 6.5 / GCC 4.4 has no atomics: flags read by other threads go through a mutex (never held across anything
// else).
bool UsbAccessory::flag(const bool& f) const {
    pthread_mutex_t* m = const_cast<pthread_mutex_t*>(&flag_mutex_);
    pthread_mutex_lock(m);
    bool v = f;
    pthread_mutex_unlock(m);
    return v;
}

void UsbAccessory::setFlag(bool& f, bool v) {
    pthread_mutex_lock(&flag_mutex_);
    f = v;
    pthread_mutex_unlock(&flag_mutex_);
}

bool UsbAccessory::start() {
    if (flag(is_running_)) return true;

    // Always (also over an inherited SIG_IGN or SA_RESTART handler): without it a stuck bridge thread can never be
    // woken.
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = wakeHandler;   // no SA_RESTART: the interrupted read() / write() must return EINTR
    sigemptyset(&sa.sa_mask);
    sigaction(SIGUSR2, &sa, NULL);

    if (usbDirectEnabled() && !direct_) {
        direct_ = new UsbDirect();
        if (!direct_->start()) {
            printf("[UsbAccessory] NBT_USB_DIRECT: io-usb not available, using %s/oap* instead\n", dev_dir_.c_str());
            delete direct_;
            direct_ = NULL;
        }
    }

    setFlag(is_running_, true);
    if (pthread_create(&watch_thread_, NULL, &UsbAccessory::watchThreadStub, this) != 0) {
        perror("[UsbAccessory] watch thread");
        setFlag(is_running_, false);
        watch_thread_ = 0;
        return false;
    }
    printf("[UsbAccessory] watching %s/android* and %s/oap* (Android Auto over USB, experimental)\n",
           dev_dir_.c_str(), dev_dir_.c_str());
    return true;
}

void UsbAccessory::stop() {
    if (!flag(is_running_)) return;
    setFlag(is_running_, false);
    if (watch_thread_) {
        pthread_join(watch_thread_, NULL);   // polls is_running_ every 100 ms
        watch_thread_ = 0;
    }
    reapBridge(true);
    if (direct_ && !direct_leaked_) {   // an abandoned bridge's link would still use it
        direct_->stop();
        delete direct_;
        direct_ = NULL;
    }
    setFlag(phone_present_, false);
    printf("[UsbAccessory] stopped.\n");
}

void* UsbAccessory::watchThreadStub(void* arg) {
    reinterpret_cast<UsbAccessory*>(arg)->watchLoop();
    return NULL;
}

UsbAccessory::AccessoryNode* UsbAccessory::findNode(const std::string& name) {
    for (size_t i = 0; i < nodes_.size(); ++i) {
        if (nodes_[i].name == name) return &nodes_[i];
    }
    return NULL;
}

void UsbAccessory::watchLoop() {
    while (flag(is_running_)) {
        reapBridge(false);

        std::vector<std::string> androids, oaps;
        DIR* d = opendir(dev_dir_.c_str());
        if (d) {
            struct dirent* e;
            while ((e = readdir(d)) != NULL) {
                if (aoap::isNode(e->d_name, "android")) androids.push_back(e->d_name);
                else if (aoap::isNode(e->d_name, "oap")) oaps.push_back(e->d_name);
            }
            closedir(d);
        }
        // NBT_USB_DIRECT: devc-serusb-aoap must not read the phone alongside us. Its node shows the phone is in
        // accessory mode; stop the driver, then use the accessory io-usb reports (its node is gone by then).
        // The accessory is only opened once it has been known for kDriverGraceS (vdev-medialaunch starts the driver
        // for it meanwhile) and no driver process is left (it ignores SIGTERM while it holds the phone).
        std::vector<std::string> accs = oaps;
        if (direct_) {
            std::vector<std::string> known = direct_->accessories(kDriverGraceS);
            if ((!oaps.empty() || !known.empty() || bridge_) && monoSec() >= next_driver_stop_s_) {
                direct_drivers_ = direct_->stopAoapDriver();
                next_driver_stop_s_ = monoSec() + 1.0;
            }
            accs = direct_drivers_ == 0 ? known : std::vector<std::string>();
            if (bridge_ && !contains(accs, bridge_->node)) accs.push_back(bridge_->node);
        }
        setFlag(phone_present_, !androids.empty() || !oaps.empty() || !accs.empty() || (direct_ && bridge_));

        // Forget nodes that went away: a phone plugged in again gets a fresh start.
        for (size_t i = switches_.size(); i-- > 0;) {
            if (!contains(androids, switches_[i].name)) switches_.erase(switches_.begin() + i);
        }
        for (size_t i = nodes_.size(); i-- > 0;) {
            if (!contains(accs, nodes_[i].name) && !(direct_ && direct_drivers_ > 0)) {
                printf("[UsbAccessory] %s%s%s gone (phone unplugged)\n", direct_ ? "" : dev_dir_.c_str(),
                       direct_ ? "" : "/", nodes_[i].name.c_str());
                nodes_.erase(nodes_.begin() + i);
            }
        }

        const double now = monoSec();
        const Transport owner = owner_ ? owner_(owner_data_) : kTransportNone;
        if (!bridge_ && mayStart(owner, kTransportUsb)) {
            // medialauncher creates /dev/android* only if the phone answered its one AOA request at plug-in: a phone
            // still in "charging only" did not, and is asked again through io-usb until it answers.
            if (direct_ && androids.empty() && accs.empty()) {
                aoap::Identity id;
                aoap::fillIdentity(id, getenv("NBT_USB_MANUFACTURER"), getenv("NBT_USB_MODEL"));
                direct_->switchPhones(id, kSelfSwitchAfterS);
            }
            for (size_t i = 0; i < androids.size(); ++i) {
                SwitchNode* s = NULL;
                for (size_t j = 0; j < switches_.size(); ++j) {
                    if (switches_[j].name == androids[i]) s = &switches_[j];
                }
                if (!s) {
                    SwitchNode n = { androids[i], 0, 0, false };
                    switches_.push_back(n);
                    s = &switches_.back();
                }
                if (s->done || now < s->next_try_s) continue;
                std::string path = dev_dir_ + "/" + androids[i];
                TriggerResult r = trigger(path.c_str());
                if (r != kTriggerFailed) {
                    s->done = true;
                } else if (++s->tries >= kMaxSwitchTries) {
                    s->done = true;
                    printf("[UsbAccessory] %s: giving up after %d tries; unplug and replug the phone to try again\n",
                           path.c_str(), s->tries);
                } else {
                    s->next_try_s = now + kNodeRetryS;
                }
            }
            for (size_t i = 0; i < accs.size() && !bridge_; ++i) {
                AccessoryNode* n = findNode(accs[i]);
                if (!n) {
                    AccessoryNode a = { accs[i], 0, 0, 0 };
                    nodes_.push_back(a);
                    n = &nodes_.back();
                }
                if (n->sessions >= kMaxSessionsPerNode || n->open_fails >= kMaxOpenFails) continue;
                if (now < n->next_try_s) continue;
                BridgeResult r = startBridge(accs[i]);   // does not add to nodes_: n stays valid
                if (r == kBridgeStarted) {
                    n->open_fails = 0;
                    if (++n->sessions == kMaxSessionsPerNode)
                        printf("[UsbAccessory] last session attempt on %s: after it, unplug and replug the phone\n",
                               accs[i].c_str());
                } else if (r == kBridgeOpenFailed) {
                    if (++n->open_fails >= kMaxOpenFails)
                        printf("[UsbAccessory] %s: could not be opened %d times in a row, giving up; unplug and "
                               "replug the phone to try again\n", accs[i].c_str(), n->open_fails);
                    n->next_try_s = now + kNodeRetryS;
                } else if (r == kBridgeFailed) {
                    n->next_try_s = now + kRetryGapS;
                }   // kBridgeLeaked: reapBridge() already took the node out of use
            }
        }

        for (int i = 0; i < 5 && flag(is_running_); ++i) usleep(100000);
    }
}

UsbAccessory::TriggerResult UsbAccessory::trigger(const char* path) {
    int fd = open(path, O_RDWR | O_NOCTTY);
    if (fd < 0) {
        printf("[UsbAccessory] %s: open failed: %s\n", path, strerror(errno));
        return kTriggerFailed;
    }
    int mode = 0, proto = 0;
    int rm = doDevctl(fd, aoap::kDcmdGetMode, &mode, sizeof(mode));
    int rp = doDevctl(fd, aoap::kDcmdGetProtocol, &proto, sizeof(proto));
    printf("[UsbAccessory] %s: phone found, AOA protocol %d (%s), mode %d (%s)\n", path, proto,
           rp ? strerror(rp) : "ok", mode, rm ? strerror(rm) : "ok");
    if (rm == 0 && mode != 0) {
        // aoap-switch answers a second switch with success without doing anything: say who was first.
        printf("[UsbAccessory] %s: already switched by another client (BMW A4A?), leaving it alone\n", path);
        close(fd);
        return kTriggerOtherClient;
    }

    aoap::Identity id;
    aoap::fillIdentity(id, getenv("NBT_USB_MANUFACTURER"), getenv("NBT_USB_MODEL"));
    int r = doDevctl(fd, aoap::kDcmdConfigSwitch, &id, sizeof(id));
    close(fd);
    if (r != 0) {
        printf("[UsbAccessory] %s: accessory switch failed: %s\n", path, strerror(r));
        return kTriggerFailed;
    }
    printf("[UsbAccessory] %s: switch to accessory mode sent as '%s' / '%s'; waiting for %s/oap*\n", path,
           id.manufacturer, id.model, dev_dir_.c_str());
    return kTriggerSwitched;
}

UsbAccessory::BridgeResult UsbAccessory::startBridge(const std::string& node) {
    std::string path = direct_ ? node : dev_dir_ + "/" + node;
    int ufd = -1;
    UsbDirectLink* link = NULL;
    if (direct_) {
        link = direct_->open(node);
        if (!link) return kBridgeOpenFailed;
    } else {
        ufd = open(path.c_str(), O_RDWR | O_NOCTTY);
        if (ufd < 0) {
            printf("[UsbAccessory] %s: open failed: %s\n", path.c_str(), strerror(errno));
            return kBridgeOpenFailed;
        }
    }
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
        printf("[UsbAccessory] socketpair failed: %s\n", strerror(errno));
        if (link) link->close();
        if (ufd >= 0) close(ufd);
        return kBridgeFailed;
    }

    UsbBridge* b = new UsbBridge(node, ufd, sv[0]);
    b->link = link;
    // With a link io-usb's callbacks do the reader's work (directRx), started once the threads run.
    b->has_reader = !link && pthread_create(&b->reader, NULL, &bridgeReader, b) == 0;
    b->has_fwd = (link || b->has_reader) && pthread_create(&b->fwd, NULL, &bridgeForwarder, b) == 0;
    b->has_writer = b->has_fwd && pthread_create(&b->writer, NULL, &bridgeWriter, b) == 0;
    bridge_ = b;
    not_ours_ticks_ = 0;
    if (!b->has_writer || (link && !link->startRx(&directRx, b))) {
        printf("[UsbAccessory] bridge threads failed to start\n");
        close(sv[1]);
        return reapBridge(true) ? kBridgeFailed : kBridgeLeaked;
    }

    printf("[UsbAccessory] %s open: starting Android Auto over USB (session fd %d)\n", path.c_str(), sv[1]);
    if (client_callback_) {
        client_callback_(sv[1], client_user_data_);   // the session owns sv[1] from here on
    } else {
        close(sv[1]);
    }
    return kBridgeStarted;
}

// force = false (every scan): end the bridge once one of its threads has ended or its session is over (the
// session owner is no longer USB: the threads may be stuck on a phone that stopped talking), and reap it.
// force = true (stop()): end it now and wait up to 3 s. Returns false when a stuck bridge had to be abandoned.
bool UsbAccessory::reapBridge(bool force) {
    UsbBridge* b = bridge_;
    if (!b) return true;
    if (force) {
        b->set(b->stop, true);
    } else {
        Transport o = owner_ ? owner_(owner_data_) : kTransportUsb;
        not_ours_ticks_ = (o != kTransportUsb) ? not_ours_ticks_ + 1 : 0;
        if (not_ours_ticks_ >= kNotOursTicks && !b->stopping()) {
            printf("[UsbAccessory] the session on %s is over (owner now: %s): ending the bridge\n", b->node.c_str(),
                   transportName(o));
            b->set(b->stop, true);
        }
    }
    if (b->anyDone()) b->set(b->stop, true);

    if (b->stopping()) {
        shutdown(b->sock_fd, SHUT_RDWR);   // wakes the writer in read() / forwarder in write(), ends the session's socket
        if (b->link) b->link->abort();     // ends the io-usb reads and a write in progress
        // A thread in read() / write() on /dev/oap stays there until the phone moves: interrupt it. Repeated,
        // because a signal that arrives just before the call is lost.
        for (int i = 0; i < (force ? 30 : 1) && !b->allDone(); ++i) {
            if (!b->readerDone()) pthread_kill(b->reader, SIGUSR2);
            if (!b->writerDone()) pthread_kill(b->writer, SIGUSR2);
            if (force) usleep(100000);
        }
    }
    if (!b->allDone()) {
        if (!force) return true;
        // Leave it to itself: its threads keep their own fds (closing them could hand the numbers to the next
        // bridge) and the object. The node is not used again until the phone is replugged.
        printf("[UsbAccessory] bridge on %s did not stop in 3 s: abandoning it until the phone is replugged\n",
               b->node.c_str());
        if (b->has_reader) pthread_detach(b->reader);
        if (b->has_fwd) pthread_detach(b->fwd);
        if (b->link) direct_leaked_ = true;
        if (b->has_writer) pthread_detach(b->writer);
        AccessoryNode* n = findNode(b->node);
        if (n) n->sessions = kMaxSessionsPerNode;
        bridge_ = NULL;
        not_ours_ticks_ = 0;
        return false;
    }
    if (b->has_reader) pthread_join(b->reader, NULL);
    if (b->has_fwd) pthread_join(b->fwd, NULL);
    if (b->has_writer) pthread_join(b->writer, NULL);
    if (b->link) b->link->close();   // waits for io-usb to finish with its transfers; no more directRx after it
    if (b->usb_fd >= 0) close(b->usb_fd);
    close(b->sock_fd);
    const double end = monoSec();
    printf("[UsbAccessory] %s closed after %.1f s: %llu bytes from the phone, %llu bytes to it\n", b->node.c_str(),
           end - b->start_s, (unsigned long long)b->bytes_in, (unsigned long long)b->bytes_out);
    AccessoryNode* n = findNode(b->node);
    if (n) n->next_try_s = end + kRetryGapS;
    delete b;
    bridge_ = NULL;
    not_ours_ticks_ = 0;
    return true;
}

} // namespace nbt
