// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// See gesture_sniff.hpp.
#include "gesture_sniff.hpp"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <vector>

#ifdef __QNXNTO__
#include <sys/ioctl.h>
#include <sys/time.h>
#include <net/if.h>
#include <net/bpf.h>
#endif

namespace nbt {
namespace gesture {

namespace {
uint32_t nowMs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint32_t)ts.tv_sec * 1000u + (uint32_t)(ts.tv_nsec / 1000000));
}

#ifdef __QNXNTO__
// Receive-only BPF device on `ifname` with the UDP-port filter. -1 and *why on failure.
int openBpf(const std::string& ifname, std::vector<uint8_t>* buf, const char** why) {
    int fd = ::open("/dev/bpf", O_RDONLY);
    for (int i = 0; fd < 0 && i < 8; ++i) {
        char p[24];
        snprintf(p, sizeof(p), "/dev/bpf%d", i);
        fd = ::open(p, O_RDONLY);
    }
    if (fd < 0) { *why = "no /dev/bpf"; return -1; }
    u_int blen = 16384;
    ioctl(fd, BIOCSBLEN, &blen);
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname.c_str(), sizeof(ifr.ifr_name) - 1);
    u_int dlt = 0, one = 1;
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 250000;
    riclist::FilterInsn f[kFilterLen];
    udpPortFilter(kCameraPort, f);
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
    if (ioctl(fd, BIOCSETIF, &ifr) < 0) *why = "the interface cannot be attached";
    else if (ioctl(fd, BIOCGDLT, &dlt) < 0 || dlt != DLT_EN10MB) *why = "not an Ethernet interface";
    else if (ioctl(fd, BIOCIMMEDIATE, &one) < 0) *why = "BIOCIMMEDIATE";
    else if (ioctl(fd, BIOCSRTIMEOUT, &tv) < 0) *why = "BIOCSRTIMEOUT";
    else if (ioctl(fd, BIOCSETF, &prog) < 0) *why = "the packet filter was refused";
    else if (ioctl(fd, BIOCGBLEN, &blen) < 0 || blen < 256) *why = "BIOCGBLEN";
    else { buf->resize(blen); return fd; }
    ::close(fd);
    return -1;
}
#endif
}  // namespace

Sniffer::Sniffer() : fn_(NULL), user_(NULL), running_(false), started_(false) {}
Sniffer::~Sniffer() { stop(); }

bool Sniffer::start(GestureFn fn, void* user) {
    if (started_) return true;
    fn_ = fn;
    user_ = user;
    const char* e = getenv("NBT_GESTURE_IF");
    if_ = (e && *e) ? e : "vlan73";
    running_ = true;
    if (pthread_create(&thread_, NULL, &Sniffer::threadMain, this) != 0) {
        running_ = false;
        printf("[Gesture] capture thread failed to start\n");
        return false;
    }
    started_ = true;
    return true;
}

void Sniffer::stop() {
    if (!started_) return;
    running_ = false;
    pthread_join(thread_, NULL);   // reads time out every 250 ms
    started_ = false;
}

void* Sniffer::threadMain(void* self) {
    static_cast<Sniffer*>(self)->run();
    return NULL;
}

void Sniffer::run() {
#ifndef __QNXNTO__
    printf("[Gesture] no BPF on this build: two-finger gesture capture off\n");
#else
    std::vector<uint8_t> buf;
    std::vector<riclist::Bytes> frames;
    Runs runs;
    unsigned long pdus = 0, datagrams = 0, gestures = 0;
    unsigned lines = 0;
    while (running_) {
        const char* why = "";
        const int fd = openBpf(if_, &buf, &why);
        if (fd < 0) {
            printf("[Gesture] capture on %s failed (%s, errno %d): two-finger gesture -> next track is off; retry in 30 s "
                   "(NBT_GESTURE_IF names another interface)\n", if_.c_str(), why, errno);
            for (int i = 0; i < 120 && running_; ++i) usleep(250000);
            continue;
        }
        printf("[Gesture] watching %s for the gesture camera (UDP %d, receive only: iDrive still gets every gesture)\n",
               if_.c_str(), (int)kCameraPort);
        while (running_) {
            const ssize_t n = ::read(fd, &buf[0], buf.size());
            if (n < 0) {
                if (errno == EINTR || errno == EAGAIN || errno == ETIMEDOUT) continue;
                printf("[Gesture] capture read failed (errno %d): reopening\n", errno);
                break;
            }
            frames.clear();
            riclist::bpfRecords(&buf[0], (size_t)n, BPF_ALIGNMENT, &frames);
            for (size_t i = 0; i < frames.size(); ++i) {
                const uint8_t* pay = NULL;
                size_t len = 0;
                uint16_t dport = 0;
                if (frames[i].empty() || !udpPayload(&frames[i][0], frames[i].size(), &pay, &len, &dport)) continue;
                if (dport != kCameraPort) continue;
                if (++datagrams == 1)
                    printf("[Gesture] first datagram to UDP %d seen on %s (%u bytes): the capture works\n",
                           (int)kCameraPort, if_.c_str(), (unsigned)len);
                CamPdu cp[4];
                const int np = splitCameraPdus(pay, len, cp, 4);
                for (int q = 0; q < np; ++q) {
                    ++pdus;
                    if (!isGesturePdu(cp[q])) continue;
                    const int id = cp[q].body[0];
                    if (!runs.fresh(id, nowMs())) continue;
                    ++gestures;
                    if (++lines <= 200)
                        printf("[Gesture] %s (id %d), gesture #%lu\n", gestureName((unsigned)id), id, gestures);
                    if (fn_) fn_(id, user_);
                }
            }
        }
        ::close(fd);
    }
    printf("[Gesture] capture stopped (%lu datagrams, %lu PDUs, %lu gestures)\n", datagrams, pdus, gestures);
#endif
}

}  // namespace gesture
}  // namespace nbt
