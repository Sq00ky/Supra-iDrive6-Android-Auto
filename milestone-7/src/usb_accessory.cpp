// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "usb_accessory.hpp"
#include "usb_aoap.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <dirent.h>
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
    int usb_fd;
    int sock_fd;
    pthread_t reader;
    pthread_t writer;
    bool has_reader;
    bool has_writer;
    bool stop;
    bool reader_done;
    bool writer_done;
    uint64_t bytes_in;      // each written by one thread, read after joining it
    uint64_t bytes_out;
    double start_s;

    UsbBridge(const std::string& n, int ufd, int sfd)
        : node(n), usb_fd(ufd), sock_fd(sfd), reader(0), writer(0), has_reader(false), has_writer(false),
          stop(false), reader_done(false), writer_done(false), bytes_in(0), bytes_out(0), start_s(monoSec()) {
        pthread_mutex_init(&mu, NULL);
    }
    ~UsbBridge() { pthread_mutex_destroy(&mu); }

    bool get(const bool& f) {
        pthread_mutex_lock(&mu);
        bool v = f;
        pthread_mutex_unlock(&mu);
        return v;
    }
    void set(bool& f, bool v) {
        pthread_mutex_lock(&mu);
        f = v;
        pthread_mutex_unlock(&mu);
    }
    bool stopping() { return get(stop); }
    bool readerDone() { return !has_reader || get(reader_done); }
    bool writerDone() { return !has_writer || get(writer_done); }
};

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

// /dev/oap -> socket.
static void* bridgeReader(void* arg) {
    UsbBridge* b = reinterpret_cast<UsbBridge*>(arg);
    unblockWake();
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
        if (!writeAll(b, b->sock_fd, &buf[0], static_cast<size_t>(n))) {
            why = b->stopping() ? "stopped" : "session closed";
            break;
        }
        b->bytes_in += static_cast<uint64_t>(n);
    }
    printf("[UsbAccessory] USB reader ended (%s)\n", why);
    b->set(b->stop, true);
    shutdown(b->sock_fd, SHUT_RDWR);   // the session sees the phone leave
    b->set(b->reader_done, true);
    return NULL;
}

// socket -> /dev/oap, one AA frame per bulk transfer (like other head units do).
static void* bridgeWriter(void* arg) {
    UsbBridge* b = reinterpret_cast<UsbBridge*>(arg);
    unblockWake();
    std::vector<uint8_t> pending;
    std::vector<uint8_t> buf(kSockReadBuf);
    const char* why = "session closed";
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
            if (!writeAll(b, b->usb_fd, base + off, len)) { ok = false; break; }
            b->bytes_out += len;
            off += len;
        }
        pending.erase(pending.begin(), pending.begin() + off);
        if (!ok) { why = b->stopping() ? "stopped" : strerror(errno); break; }
    }
    printf("[UsbAccessory] USB writer ended (%s)\n", why);
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
      not_ours_ticks_(0) {
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
        setFlag(phone_present_, !androids.empty() || !oaps.empty());

        // Forget nodes that went away: a phone plugged in again gets a fresh start.
        for (size_t i = switches_.size(); i-- > 0;) {
            if (!contains(androids, switches_[i].name)) switches_.erase(switches_.begin() + i);
        }
        for (size_t i = nodes_.size(); i-- > 0;) {
            if (!contains(oaps, nodes_[i].name)) {
                printf("[UsbAccessory] %s/%s gone (phone unplugged)\n", dev_dir_.c_str(), nodes_[i].name.c_str());
                nodes_.erase(nodes_.begin() + i);
            }
        }

        const double now = monoSec();
        const Transport owner = owner_ ? owner_(owner_data_) : kTransportNone;
        if (!bridge_ && mayStart(owner, kTransportUsb)) {
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
            for (size_t i = 0; i < oaps.size() && !bridge_; ++i) {
                AccessoryNode* n = findNode(oaps[i]);
                if (!n) {
                    AccessoryNode a = { oaps[i], 0, 0, 0 };
                    nodes_.push_back(a);
                    n = &nodes_.back();
                }
                if (n->sessions >= kMaxSessionsPerNode || n->open_fails >= kMaxOpenFails) continue;
                if (now < n->next_try_s) continue;
                BridgeResult r = startBridge(oaps[i]);   // does not add to nodes_: n stays valid
                if (r == kBridgeStarted) {
                    n->open_fails = 0;
                    if (++n->sessions == kMaxSessionsPerNode)
                        printf("[UsbAccessory] last session attempt on %s: after it, unplug and replug the phone\n",
                               oaps[i].c_str());
                } else if (r == kBridgeOpenFailed) {
                    if (++n->open_fails >= kMaxOpenFails)
                        printf("[UsbAccessory] %s: could not be opened %d times in a row, giving up; unplug and "
                               "replug the phone to try again\n", oaps[i].c_str(), n->open_fails);
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
    std::string path = dev_dir_ + "/" + node;
    int ufd = open(path.c_str(), O_RDWR | O_NOCTTY);
    if (ufd < 0) {
        printf("[UsbAccessory] %s: open failed: %s\n", path.c_str(), strerror(errno));
        return kBridgeOpenFailed;
    }
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
        printf("[UsbAccessory] socketpair failed: %s\n", strerror(errno));
        close(ufd);
        return kBridgeFailed;
    }

    UsbBridge* b = new UsbBridge(node, ufd, sv[0]);
    b->has_reader = pthread_create(&b->reader, NULL, &bridgeReader, b) == 0;
    b->has_writer = b->has_reader && pthread_create(&b->writer, NULL, &bridgeWriter, b) == 0;
    bridge_ = b;
    not_ours_ticks_ = 0;
    if (!b->has_writer) {
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
    if (b->readerDone() || b->writerDone()) b->set(b->stop, true);

    if (b->stopping()) {
        shutdown(b->sock_fd, SHUT_RDWR);   // wakes a writer in read() and ends the session's socket
        // A thread in read() / write() on /dev/oap stays there until the phone moves: interrupt it. Repeated,
        // because a signal that arrives just before the call is lost.
        for (int i = 0; i < (force ? 30 : 1) && !(b->readerDone() && b->writerDone()); ++i) {
            if (!b->readerDone()) pthread_kill(b->reader, SIGUSR2);
            if (!b->writerDone()) pthread_kill(b->writer, SIGUSR2);
            if (force) usleep(100000);
        }
    }
    if (!(b->readerDone() && b->writerDone())) {
        if (!force) return true;
        // Leave it to itself: its threads keep their own fds (closing them could hand the numbers to the next
        // bridge) and the object. The node is not used again until the phone is replugged.
        printf("[UsbAccessory] bridge on %s did not stop in 3 s: abandoning it until the phone is replugged\n",
               b->node.c_str());
        if (b->has_reader) pthread_detach(b->reader);
        if (b->has_writer) pthread_detach(b->writer);
        AccessoryNode* n = findNode(b->node);
        if (n) n->sessions = kMaxSessionsPerNode;
        bridge_ = NULL;
        not_ours_ticks_ = 0;
        return false;
    }
    if (b->has_reader) pthread_join(b->reader, NULL);
    if (b->has_writer) pthread_join(b->writer, NULL);
    close(b->usb_fd);
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
