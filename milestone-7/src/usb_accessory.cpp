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

// SIGUSR2 only interrupts the reader's blocking read() on /dev/oap (the driver implements io_unblock).
static void wakeHandler(int) { }

static bool writeAll(int fd, const uint8_t* p, size_t n) {
    while (n > 0) {
        ssize_t w = write(fd, p, n);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) return false;
        p += w;
        n -= static_cast<size_t>(w);
    }
    return true;
}

static bool contains(const std::vector<std::string>& v, const std::string& s) {
    for (size_t i = 0; i < v.size(); ++i) {
        if (v[i] == s) return true;
    }
    return false;
}

UsbAccessory::UsbAccessory(ClientConnectedCallback callback, void* user_data, BusyCallback busy, void* busy_data,
                           const char* dev_dir)
    : dev_dir_(dev_dir ? dev_dir : "/dev"),
      client_callback_(callback),
      client_user_data_(user_data),
      busy_(busy),
      busy_data_(busy_data),
      watch_thread_(0),
      is_running_(false),
      bridge_up_(false),
      usb_fd_(-1),
      sock_fd_(-1),
      reader_thread_(0),
      writer_thread_(0),
      bridge_stop_(false),
      reader_done_(false),
      writer_done_(false),
      bytes_in_(0),
      bytes_out_(0),
      bridge_start_s_(0),
      bridge_end_s_(0) {
    pthread_mutex_init(&flag_mutex_, NULL);
}

UsbAccessory::~UsbAccessory() {
    stop();
    pthread_mutex_destroy(&flag_mutex_);
}

// QNX 6.5 / GCC 4.4 has no atomics: the flags shared by the watch, reader and writer threads go through a mutex
// (never held across anything else).
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

    struct sigaction old;
    if (sigaction(SIGUSR2, NULL, &old) == 0 && old.sa_handler == SIG_DFL) {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = wakeHandler;   // no SA_RESTART: the interrupted read() must return EINTR
        sigemptyset(&sa.sa_mask);
        sigaction(SIGUSR2, &sa, NULL);
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
    printf("[UsbAccessory] stopped.\n");
}

void* UsbAccessory::watchThreadStub(void* arg) {
    reinterpret_cast<UsbAccessory*>(arg)->watchLoop();
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

        // Forget nodes that went away: a phone plugged in again gets a fresh start.
        for (size_t i = triggered_.size(); i-- > 0;) {
            if (!contains(androids, triggered_[i])) triggered_.erase(triggered_.begin() + i);
        }
        for (size_t i = sessions_.size(); i-- > 0;) {
            if (!contains(oaps, sessions_[i].first)) {
                printf("[UsbAccessory] %s/%s gone (phone unplugged)\n", dev_dir_.c_str(),
                       sessions_[i].first.c_str());
                sessions_.erase(sessions_.begin() + i);
            }
        }

        const bool busy = bridge_up_ || (busy_ && busy_(busy_data_));
        if (!busy) {
            for (size_t i = 0; i < androids.size(); ++i) {
                if (contains(triggered_, androids[i])) continue;
                triggered_.push_back(androids[i]);
                std::string path = dev_dir_ + "/" + androids[i];
                trigger(path.c_str());
            }
            for (size_t i = 0; i < oaps.size() && !bridge_up_; ++i) {
                int* count = NULL;
                for (size_t j = 0; j < sessions_.size(); ++j) {
                    if (sessions_[j].first == oaps[i]) count = &sessions_[j].second;
                }
                if (count && *count >= kMaxSessionsPerNode) continue;
                if (count && monoSec() - bridge_end_s_ < kRetryGapS) continue;
                if (!count) {
                    sessions_.push_back(std::make_pair(oaps[i], 0));
                    count = &sessions_.back().second;
                }
                ++*count;
                if (*count == kMaxSessionsPerNode)
                    printf("[UsbAccessory] last session attempt on %s: after it, unplug and replug the phone\n",
                           oaps[i].c_str());
                startBridge(oaps[i]);
            }
        }

        for (int i = 0; i < 5 && flag(is_running_); ++i) usleep(100000);
    }
}

bool UsbAccessory::trigger(const char* path) {
    int fd = open(path, O_RDWR);
    if (fd < 0) {
        printf("[UsbAccessory] %s: open failed: %s\n", path, strerror(errno));
        return false;
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
        return false;
    }

    aoap::Identity id;
    aoap::fillIdentity(id, getenv("NBT_USB_MANUFACTURER"), getenv("NBT_USB_MODEL"));
    int r = doDevctl(fd, aoap::kDcmdConfigSwitch, &id, sizeof(id));
    close(fd);
    if (r != 0) {
        printf("[UsbAccessory] %s: accessory switch failed: %s\n", path, strerror(r));
        return false;
    }
    printf("[UsbAccessory] %s: switch to accessory mode sent as '%s' / '%s'; waiting for %s/oap*\n", path,
           id.manufacturer, id.model, dev_dir_.c_str());
    return true;
}

bool UsbAccessory::startBridge(const std::string& node) {
    std::string path = dev_dir_ + "/" + node;
    int ufd = open(path.c_str(), O_RDWR);
    if (ufd < 0) {
        printf("[UsbAccessory] %s: open failed: %s\n", path.c_str(), strerror(errno));
        bridge_end_s_ = monoSec();
        return false;
    }
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
        printf("[UsbAccessory] socketpair failed: %s\n", strerror(errno));
        close(ufd);
        bridge_end_s_ = monoSec();
        return false;
    }

    usb_fd_ = ufd;
    sock_fd_ = sv[0];
    bridge_node_ = node;
    bridge_start_s_ = monoSec();
    setFlag(bridge_stop_, false);
    setFlag(reader_done_, false);
    setFlag(writer_done_, false);
    bytes_in_ = 0;
    bytes_out_ = 0;
    bool rd = pthread_create(&reader_thread_, NULL, &UsbAccessory::readerThreadStub, this) == 0;
    bool wr = rd && pthread_create(&writer_thread_, NULL, &UsbAccessory::writerThreadStub, this) == 0;
    if (!wr) {
        printf("[UsbAccessory] bridge threads failed to start\n");
        close(sv[1]);
        if (rd) {   // let reapBridge() stop and join the reader and close both fds
            setFlag(writer_done_, true);
            writer_thread_ = 0;
            bridge_up_ = true;
            reapBridge(true);
        } else {
            close(ufd);
            close(sv[0]);
            usb_fd_ = sock_fd_ = -1;
            bridge_end_s_ = monoSec();
        }
        return false;
    }
    bridge_up_ = true;

    printf("[UsbAccessory] %s open: starting Android Auto over USB (session fd %d)\n", path.c_str(), sv[1]);
    if (client_callback_) {
        client_callback_(sv[1], client_user_data_);   // the session owns sv[1] from here on
    } else {
        close(sv[1]);
    }
    return true;
}

// force = false: reap a bridge whose threads have ended (waking the reader once the writer is gone).
// force = true: end the bridge now and wait for it (stop()).
void UsbAccessory::reapBridge(bool force) {
    if (!bridge_up_) return;
    if (force) {
        setFlag(bridge_stop_, true);
        shutdown(sock_fd_, SHUT_RDWR);   // wakes the writer and ends the session's socket
    }
    // The reader sits in read() on /dev/oap until the phone sends something: once the bridge is ending, interrupt
    // it. Repeated, because a signal that arrives just before it enters read() is lost.
    if (flag(writer_done_)) setFlag(bridge_stop_, true);
    for (int i = 0; i < (force ? 30 : 1) && !(flag(reader_done_) && flag(writer_done_)); ++i) {
        if (flag(bridge_stop_) && !flag(reader_done_)) pthread_kill(reader_thread_, SIGUSR2);
        if (force) usleep(100000);
    }
    if (!(flag(reader_done_) && flag(writer_done_))) {
        if (!force) return;
        printf("[UsbAccessory] bridge on %s did not stop in 3 s, leaving it\n", bridge_node_.c_str());
        pthread_detach(reader_thread_);
        if (writer_thread_) pthread_detach(writer_thread_);
        bridge_up_ = false;
        return;
    }
    pthread_join(reader_thread_, NULL);
    if (writer_thread_) pthread_join(writer_thread_, NULL);
    reader_thread_ = 0;
    writer_thread_ = 0;
    close(usb_fd_);
    close(sock_fd_);
    usb_fd_ = -1;
    sock_fd_ = -1;
    bridge_up_ = false;
    bridge_end_s_ = monoSec();
    printf("[UsbAccessory] %s closed after %.1f s: %llu bytes from the phone, %llu bytes to it\n",
           bridge_node_.c_str(), bridge_end_s_ - bridge_start_s_, (unsigned long long)bytes_in_,
           (unsigned long long)bytes_out_);
}

void* UsbAccessory::readerThreadStub(void* arg) {
    reinterpret_cast<UsbAccessory*>(arg)->readerLoop();
    return NULL;
}

void* UsbAccessory::writerThreadStub(void* arg) {
    reinterpret_cast<UsbAccessory*>(arg)->writerLoop();
    return NULL;
}

void UsbAccessory::readerLoop() {
    std::vector<uint8_t> buf(kUsbReadBuf);
    const char* why = "stopped";
    while (!flag(bridge_stop_)) {
        ssize_t n = read(usb_fd_, &buf[0], buf.size());
        if (n < 0 && errno == EINTR) continue;   // bridge_stop_ is checked by the loop
        if (n < 0) { why = strerror(errno); break; }
        if (n == 0) { why = "end of data"; break; }
        if (!writeAll(sock_fd_, &buf[0], static_cast<size_t>(n))) { why = "session closed"; break; }
        bytes_in_ += static_cast<uint64_t>(n);
    }
    printf("[UsbAccessory] USB reader ended (%s)\n", why);
    shutdown(sock_fd_, SHUT_RDWR);   // the session sees the phone leave
    setFlag(reader_done_, true);
}

void UsbAccessory::writerLoop() {
    std::vector<uint8_t> pending;
    std::vector<uint8_t> buf(kSockReadBuf);
    const char* why = "session closed";
    while (true) {
        ssize_t n = read(sock_fd_, &buf[0], buf.size());
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        pending.insert(pending.end(), buf.begin(), buf.begin() + n);
        // Hand the phone whole frames, one per bulk transfer, like other head units do.
        const uint8_t* base = &pending[0];   // not empty: n > 0 bytes were just added
        size_t off = 0;
        bool ok = true;
        for (;;) {
            size_t len = aoap::frameLength(base + off, pending.size() - off);
            if (len == 0 || pending.size() - off < len) break;
            if (!writeAll(usb_fd_, base + off, len)) { ok = false; break; }
            bytes_out_ += len;
            off += len;
        }
        pending.erase(pending.begin(), pending.begin() + off);
        if (!ok) { why = strerror(errno); break; }
    }
    printf("[UsbAccessory] USB writer ended (%s)\n", why);
    setFlag(bridge_stop_, true);
    setFlag(writer_done_, true);
}

} // namespace nbt
