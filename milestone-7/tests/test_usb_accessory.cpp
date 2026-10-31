// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test for src/usb_aoap.hpp (devctl layout, node names, frame lengths) and src/usb_accessory.cpp (watcher +
// bridge threads). A pty linked as <dir>/oap12 stands in for devc-serusb-aoap: the master side is the phone.
// g++ -std=gnu++98 -Wall -Wextra -pthread -Iinclude -Isrc -o /tmp/test_usb_accessory
//     tests/test_usb_accessory.cpp src/usb_accessory.cpp && /tmp/test_usb_accessory   (~32 s: real retry gaps)
#include "usb_aoap.hpp"
#include "usb_accessory.hpp"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

using namespace nbt;

// QNX <devctl.h>: __DIOF = 0x40000000, __DIOTF = 0xC0000000, | (size << 16) | (class << 8) | cmd.
static unsigned diof(unsigned cls, unsigned cmd, unsigned size) { return 0x40000000u | (size << 16) | (cls << 8) | cmd; }
static unsigned diotf(unsigned cls, unsigned cmd, unsigned size) { return 0xC0000000u | (size << 16) | (cls << 8) | cmd; }

static void testHeader() {
    assert(aoap::kDcmdGetMode == diof(5, 1, 4));
    assert(aoap::kDcmdSwitch == diotf(5, 2, 4));
    assert(aoap::kDcmdConfigSwitch == diotf(5, 3, sizeof(aoap::Identity)));
    assert(aoap::kDcmdGetProtocol == diof(5, 4, 4));

    // Offsets aoap-switch's io_devctl copies from (data + 4, 260, 516, 772, 1028, 1284).
    assert(offsetof(aoap::Identity, manufacturer) == 4);
    assert(offsetof(aoap::Identity, model) == 260);
    assert(offsetof(aoap::Identity, description) == 516);
    assert(offsetof(aoap::Identity, version) == 772);
    assert(offsetof(aoap::Identity, url) == 1028);
    assert(offsetof(aoap::Identity, serial) == 1284);

    aoap::Identity id;
    aoap::fillIdentity(id);
    assert(id.protocol == 1);
    assert(strcmp(id.manufacturer, "Android") == 0);
    assert(strcmp(id.model, "Android Auto") == 0);
    assert(strcmp(id.description, "Android Auto") == 0);
    assert(strcmp(id.version, "2.0.1") == 0);
    assert(id.url[0] && id.serial[0]);
    aoap::fillIdentity(id, "", NULL);   // empty overrides keep the defaults
    assert(strcmp(id.manufacturer, "Android") == 0 && strcmp(id.model, "Android Auto") == 0);
    char longname[300];
    memset(longname, 'x', sizeof(longname) - 1);
    longname[sizeof(longname) - 1] = 0;
    aoap::fillIdentity(id, "Google", longname);
    assert(strcmp(id.manufacturer, "Google") == 0);
    assert(strlen(id.model) == aoap::kMaxField);   // fits aoap-switch's 128-byte copy
    for (size_t i = aoap::kMaxField; i < sizeof(id.model); ++i) assert(id.model[i] == 0);

    assert(aoap::isNode("android012", "android"));
    assert(aoap::isNode("oap13", "oap"));
    assert(!aoap::isNode("oap", "oap"));
    assert(!aoap::isNode("oapx1", "oap"));
    assert(!aoap::isNode("oap1a", "oap"));
    assert(!aoap::isNode("android", "android"));
    assert(!aoap::isNode("io-usb", "oap"));
    assert(!aoap::isNode(NULL, "oap"));

    const uint8_t last[] = { 0x00, 0x03, 0x00, 0x08 };          // FIRST|LAST, 8 payload bytes
    const uint8_t first[] = { 0x03, 0x09, 0x40, 0x00 };         // FIRST only (encrypted): +4 total size
    const uint8_t middle[] = { 0x03, 0x08, 0x01, 0x00 };        // continuation
    assert(aoap::frameLength(last, 3) == 0);
    assert(aoap::frameLength(last, 4) == 12);
    assert(aoap::frameLength(first, 4) == 8 + 0x4000);
    assert(aoap::frameLength(middle, 4) == 4 + 0x100);
    printf("header: ok\n");
}

// ---- bridge test ----

// Set by the watch thread (onConnect), read by the test thread.
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static int g_sessionFd = -1;
static int g_sessions = 0;
static bool g_active = false;

static void onConnect(int fd, void*) {
    pthread_mutex_lock(&g_mu);
    g_sessionFd = fd;
    g_active = true;
    ++g_sessions;
    pthread_mutex_unlock(&g_mu);
}

// Like sessionOwner() in main.cpp: the test's "session" is a USB one while its fd is open.
static Transport owner(void*) {
    pthread_mutex_lock(&g_mu);
    bool b = g_active;
    pthread_mutex_unlock(&g_mu);
    return b ? kTransportUsb : kTransportNone;
}

static int sessions() {
    pthread_mutex_lock(&g_mu);
    int n = g_sessions;
    pthread_mutex_unlock(&g_mu);
    return n;
}

static int sessionFd() {
    pthread_mutex_lock(&g_mu);
    int fd = g_sessionFd;
    pthread_mutex_unlock(&g_mu);
    return fd;
}

static double now() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static bool waitSessions(int n, double secs) {
    double end = now() + secs;
    while (now() < end) {
        if (sessions() >= n) return true;
        usleep(20000);
    }
    return sessions() >= n;
}

// Read exactly n bytes from fd within 2 s.
static bool readN(int fd, uint8_t* out, size_t n) {
    size_t got = 0;
    double end = now() + 2.0;
    while (got < n && now() < end) {
        struct pollfd p = { fd, POLLIN, 0 };
        if (poll(&p, 1, 100) <= 0) continue;
        ssize_t r = read(fd, out + got, n - got);
        if (r <= 0) return false;
        got += (size_t)r;
    }
    return got == n;
}

static void writeAllOrDie(int fd, const uint8_t* p, size_t n) {
    while (n) {
        ssize_t w = write(fd, p, n);
        assert(w > 0);
        p += w;
        n -= (size_t)w;
    }
}

static int openPhone(const char* link) {
    int m = posix_openpt(O_RDWR | O_NOCTTY);
    assert(m >= 0);
    assert(grantpt(m) == 0 && unlockpt(m) == 0);
    struct termios t;
    assert(tcgetattr(m, &t) == 0);
    cfmakeraw(&t);   // byte-exact both ways
    assert(tcsetattr(m, TCSANOW, &t) == 0);
    unlink(link);
    assert(symlink(ptsname(m), link) == 0);
    return m;
}

static void endSession() {
    pthread_mutex_lock(&g_mu);
    int fd = g_sessionFd;
    g_sessionFd = -1;
    g_active = false;
    pthread_mutex_unlock(&g_mu);
    close(fd);
}

static void testBridge() {
    const char* dir = "/tmp/nbt_usbacc_dev";
    const char* link = "/tmp/nbt_usbacc_dev/oap12";
    int rc = system("rm -rf /tmp/nbt_usbacc_dev && mkdir -p /tmp/nbt_usbacc_dev");
    assert(rc == 0);
    // An aoap-switch node that is a plain file: the switch fails (no devctl here) and is not retried.
    FILE* f = fopen("/tmp/nbt_usbacc_dev/android01", "w");
    assert(f);
    fclose(f);
    int phone = openPhone(link);

    UsbAccessory usb(&onConnect, NULL, &owner, NULL, dir);
    assert(usb.start());
    assert(waitSessions(1, 3.0));
    int s = sessionFd();
    assert(s >= 0);
    assert(usb.phonePresent());
    unlink("/tmp/nbt_usbacc_dev/android01");   // from here on only oap12 says a phone is plugged in

    // Head unit -> phone, written in pieces that split headers: arrives byte-exact.
    uint8_t out[12 + 8 + 0x40];
    const uint8_t f1[12] = { 0x00, 0x03, 0x00, 0x08, 0, 1, 0, 1, 0, 6, 0, 0 };   // VersionRequest
    memcpy(out, f1, 12);
    out[12] = 0x03; out[13] = 0x09; out[14] = 0x00; out[15] = 0x40;               // FIRST-only frame
    out[16] = 0; out[17] = 0; out[18] = 0x01; out[19] = 0x00;
    for (int i = 0; i < 0x40; ++i) out[20 + i] = (uint8_t)i;
    writeAllOrDie(s, out, 2);
    usleep(50000);
    writeAllOrDie(s, out + 2, 15);
    usleep(50000);
    writeAllOrDie(s, out + 17, sizeof(out) - 17);
    uint8_t got[sizeof(out)];
    assert(readN(phone, got, sizeof(got)));
    assert(memcmp(got, out, sizeof(out)) == 0);

    // Phone -> head unit.
    const uint8_t in[8] = { 0x00, 0x03, 0x00, 0x04, 0x00, 0x02, 0x00, 0x01 };
    writeAllOrDie(phone, in, sizeof(in));
    uint8_t gin[sizeof(in)];
    assert(readN(s, gin, sizeof(gin)));
    assert(memcmp(gin, in, sizeof(in)) == 0);
    printf("bridge data: ok\n");

    // The session ends (ByeBye): the bridge is reaped (reader interrupted) and a new session starts on the same
    // accessory connection after the retry gap, up to 3 in total.
    endSession();
    assert(waitSessions(2, 5.0));
    endSession();
    assert(waitSessions(3, 5.0));
    endSession();
    assert(!waitSessions(4, 4.5));
    printf("session retries: ok\n");

    // Replug: the node goes away and comes back -> a fresh start.
    unlink(link);
    close(phone);
    sleep(1);
    assert(!usb.phonePresent());   // the wireless thread may use the Bluetooth links again
    phone = openPhone(link);
    assert(waitSessions(4, 3.0));
    s = sessionFd();

    // Unplug while the session runs: the session socket sees the phone leave.
    close(phone);
    uint8_t b;
    double t0 = now();
    struct pollfd p = { s, POLLIN, 0 };
    assert(poll(&p, 1, 3000) == 1);
    assert(read(s, &b, 1) == 0);
    assert(now() - t0 < 3.0);
    endSession();
    unlink(link);
    sleep(1);   // let the watcher see the node go (a quick re-add counts as the same connection)
    printf("unplug: ok\n");

    // stop() with a bridge up returns promptly.
    phone = openPhone(link);
    assert(waitSessions(5, 3.0));
    t0 = now();
    usb.stop();
    assert(now() - t0 < 2.0);
    assert(!usb.phonePresent());
    struct pollfd p2 = { sessionFd(), POLLIN, 0 };
    assert(poll(&p2, 1, 1000) == 1);
    assert(read(sessionFd(), &b, 1) == 0);
    endSession();
    close(phone);
    printf("stop: ok\n");
}

// Item 3 of the review: the phone stops reading while the head unit has data queued, so the writer sits in
// write() on /dev/oap. When the session ends, the bridge must still be ended (writer interrupted) and reaped, or
// no USB session could ever start again.
static void testStalledWriter() {
    const char* dir = "/tmp/nbt_usbacc_stall";
    const char* link = "/tmp/nbt_usbacc_stall/oap21";
    int rc = system("rm -rf /tmp/nbt_usbacc_stall && mkdir -p /tmp/nbt_usbacc_stall");
    assert(rc == 0);
    int phone = openPhone(link);   // never read: the pty fills up and the bridge's write() blocks
    int base = sessions();
    UsbAccessory usb(&onConnect, NULL, &owner, NULL, dir);
    assert(usb.start());
    assert(waitSessions(base + 1, 3.0));
    int s = sessionFd();
    assert(fcntl(s, F_SETFL, fcntl(s, F_GETFL) | O_NONBLOCK) == 0);
    uint8_t frame[4 + 1000];
    memset(frame, 0x5a, sizeof(frame));
    frame[0] = 0x03; frame[1] = 0x0b; frame[2] = 1000 >> 8; frame[3] = 1000 & 0xff;
    size_t queued = 0;
    for (int i = 0; i < 20000; ++i) {   // until the socket is full too (the writer is stuck behind it)
        ssize_t w = write(s, frame, sizeof(frame));
        if (w < 0) { assert(errno == EAGAIN || errno == EWOULDBLOCK); break; }
        queued += (size_t)w;
    }
    assert(queued > 64 * 1024);
    usleep(300000);
    endSession();   // ByeBye: the session is over, the phone is still silent
    assert(waitSessions(base + 2, 6.0));   // reaped (~1 s) + retry gap (3 s)
    printf("stalled writer: ok\n");

    // stop() with the new bridge stuck the same way (the pty is still full) returns promptly too.
    s = sessionFd();
    assert(fcntl(s, F_SETFL, fcntl(s, F_GETFL) | O_NONBLOCK) == 0);
    for (int i = 0; i < 20000 && write(s, frame, sizeof(frame)) > 0; ++i) { }
    usleep(300000);
    double t0 = now();
    usb.stop();
    assert(now() - t0 < 2.0);
    endSession();
    close(phone);
    printf("stop with a stalled writer: ok\n");
}

// Item 5: an accessory node that cannot be opened yet (driver not ready, or held by another client) is retried and
// the failures do not use up the sessions of that plug-in.
static void testOpenRetries() {
    const char* dir = "/tmp/nbt_usbacc_retry";
    const char* link = "/tmp/nbt_usbacc_retry/oap33";
    int rc = system("rm -rf /tmp/nbt_usbacc_retry && mkdir -p /tmp/nbt_usbacc_retry");
    assert(rc == 0);
    assert(symlink("/tmp/nbt_usbacc_retry/not-there-yet", link) == 0);   // listed by readdir, open() fails
    int base = sessions();
    UsbAccessory usb(&onConnect, NULL, &owner, NULL, dir);
    assert(usb.start());
    sleep(4);   // 4 failed opens, one a second: more than the 3 sessions of a plug-in
    assert(sessions() == base);
    assert(usb.phonePresent());
    // The driver comes up: same node name, swapped atomically so the watcher never sees it missing.
    int phone = openPhone("/tmp/nbt_usbacc_retry/tmp");
    assert(rename("/tmp/nbt_usbacc_retry/tmp", link) == 0);
    assert(waitSessions(base + 1, 3.0));
    usb.stop();
    endSession();
    close(phone);
    printf("open retries: ok\n");
}

static int countLines(const char* path, const char* needle) {
    FILE* f = fopen(path, "r");
    assert(f);
    char line[512];
    int n = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, needle)) ++n;
    }
    fclose(f);
    return n;
}

// Item 5: an aoap-switch node whose switch fails (here: no devctl on the host) is retried a second apart, up to
// 5 times, instead of once for the whole plug-in.
static void testSwitchRetries() {
    const char* dir = "/tmp/nbt_usbacc_switch";
    const char* log = "/tmp/nbt_usbacc_switch.log";
    int rc = system("rm -rf /tmp/nbt_usbacc_switch && mkdir -p /tmp/nbt_usbacc_switch");
    assert(rc == 0);
    FILE* f = fopen("/tmp/nbt_usbacc_switch/android05", "w");
    assert(f);
    fclose(f);
    int saved = dup(1);
    int out = open(log, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    assert(saved >= 0 && out >= 0);
    assert(dup2(out, 1) == 1);   // stdout is unbuffered (main)
    UsbAccessory usb(&onConnect, NULL, &owner, NULL, dir);
    assert(usb.start());
    sleep(7);
    usb.stop();
    assert(dup2(saved, 1) == 1);
    close(saved);
    close(out);
    assert(countLines(log, "android05: accessory switch failed") == 5);
    assert(countLines(log, "android05: giving up after 5 tries") == 1);
    printf("switch retries: ok\n");
}

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);   // the daemon log lines interleave with the asserts
    testHeader();
    testBridge();
    testStalledWriter();
    testOpenRetries();
    testSwitchRetries();
    printf("test_usb_accessory: all passed\n");
    return 0;
}
