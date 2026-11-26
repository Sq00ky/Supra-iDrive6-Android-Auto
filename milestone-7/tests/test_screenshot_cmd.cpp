// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test for src/screenshot.cpp (nbt-aad --screenshot) against a fake libscreen that only knows QNX 6.5.0 SP1's
// property ids.

#include "screenshot.hpp"
#include "screenshot_png.hpp"

#if NBT_SCREENSHOT_TIMEOUT_S != 1
#error "the timings below assume the 1 s read guard from tests/Makefile"
#endif

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <string>
#include <vector>

extern "C" {
typedef void* screen_context_t;
typedef void* screen_display_t;
typedef void* screen_pixmap_t;
typedef void* screen_buffer_t;
}

namespace fake {

enum Fail { kNone, kContext, kPixmap, kBuffer, kRead };

struct State {
    Fail fail;
    int err;
    int displays, w, h, stride, fmt;  // stride/fmt 0: what the pixmap was set to
    bool hang;
    int ctxType, setFmt, setUsage, setW, setH, reads, readCount, openCtx, openPix;
    std::vector<uint8_t> mem;
};
State st;
int ctxObj, dispObj, pixObj, bufObj;

void reset() {
    st = State();
    st.displays = 1;
    st.w = 5;
    st.h = 3;
    st.stride = 28;
    st.ctxType = st.setFmt = st.setUsage = st.setW = st.setH = st.readCount = -1;
}

int bad() {
    errno = EINVAL;
    return -1;
}

int stride() { return st.stride ? st.stride : st.setW * 4; }

}  // namespace fake

using fake::st;

extern "C" {

int screen_create_context(screen_context_t* ctx, int type) {
    st.ctxType = type;
    if (st.fail == fake::kContext) { errno = st.err; return -1; }
    *ctx = &fake::ctxObj;
    st.openCtx++;
    return 0;
}
int screen_destroy_context(screen_context_t ctx) {
    if (ctx != &fake::ctxObj) return fake::bad();
    st.openCtx--;
    return 0;
}
int screen_get_context_property_iv(screen_context_t, int prop, int* v) {
    if (prop != 59) return fake::bad();
    *v = st.displays;
    return 0;
}
int screen_get_context_property_pv(screen_context_t, int prop, void** v) {
    if (prop != 60) return fake::bad();
    for (int i = 0; i < st.displays; ++i) v[i] = &fake::dispObj;
    return 0;
}
int screen_get_display_property_iv(screen_display_t d, int prop, int* v) {
    if (d != &fake::dispObj || prop != 40) return fake::bad();
    v[0] = st.w;
    v[1] = st.h;
    return 0;
}
int screen_create_pixmap(screen_pixmap_t* p, screen_context_t ctx) {
    if (ctx != &fake::ctxObj) return fake::bad();
    if (st.fail == fake::kPixmap) { errno = st.err; return -1; }
    *p = &fake::pixObj;
    st.openPix++;
    return 0;
}
int screen_destroy_pixmap(screen_pixmap_t p) {
    if (p != &fake::pixObj) return fake::bad();
    st.openPix--;
    return 0;
}
int screen_set_pixmap_property_iv(screen_pixmap_t p, int prop, const int* v) {
    if (p != &fake::pixObj) return fake::bad();
    if (prop == 14) st.setFmt = v[0];
    else if (prop == 48) st.setUsage = v[0];
    else if (prop == 5) { st.setW = v[0]; st.setH = v[1]; }
    else return fake::bad();
    return 0;
}
int screen_create_pixmap_buffer(screen_pixmap_t p) {
    if (p != &fake::pixObj || st.setW < 1 || st.setH < 1) return fake::bad();
    if (st.fail == fake::kBuffer) { errno = st.err; return -1; }
    st.mem.assign((size_t)fake::stride() * st.setH + st.setW * 4, 0);
    return 0;
}
int screen_get_pixmap_property_pv(screen_pixmap_t p, int prop, void** v) {
    if (p != &fake::pixObj || prop != 37 || st.mem.empty()) return fake::bad();
    v[0] = &fake::bufObj;
    return 0;
}
int screen_get_buffer_property_iv(screen_buffer_t b, int prop, int* v) {
    if (b != &fake::bufObj) return fake::bad();
    if (prop == 44) v[0] = fake::stride();
    else if (prop == 14) v[0] = st.fmt ? st.fmt : st.setFmt;
    else if (prop == 5) { v[0] = st.setW; v[1] = st.setH; }
    else return fake::bad();
    return 0;
}
int screen_get_buffer_property_pv(screen_buffer_t b, int prop, void** v) {
    if (b != &fake::bufObj || prop != 34) return fake::bad();
    v[0] = &st.mem[0];
    return 0;
}
// B = x, G = y, R = 0x80, X = 0x5A; stride padding 0xEE.
int screen_read_display(screen_display_t d, screen_buffer_t b, int count, const int*, int) {
    if (d != &fake::dispObj || b != &fake::bufObj) return fake::bad();
    st.reads++;
    st.readCount = count;
    if (st.hang) sleep(5);
    if (st.fail == fake::kRead) { errno = st.err; return -1; }
    memset(&st.mem[0], 0xEE, st.mem.size());
    for (int y = 0; y < st.setH; ++y)
        for (int x = 0; x < st.setW; ++x) {
            uint8_t* p = &st.mem[(size_t)y * fake::stride() + x * 4];
            p[0] = (uint8_t)x; p[1] = (uint8_t)y; p[2] = 0x80; p[3] = 0x5A;
        }
    return 0;
}

}  // extern "C"

namespace {

enum Mode { kNormal, kGone, kSlow, kTty };

struct Result {
    int code;
    std::string out, line, fakeLine;
    double secs;
};

double now() {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

std::string slurp(int fd) {
    std::string s;
    char b[4096];
    ssize_t n;
    while ((n = read(fd, b, sizeof b)) > 0) s.append(b, (size_t)n);
    close(fd);
    return s;
}

// runScreenshotCommand in a child (it may _exit from the alarm handler). stdout and stderr through pipes; kGone closes
// the stdout pipe at once, kSlow reads it only after 1.5 s, kTty gives the child a pseudo terminal as stdout.
Result run(Mode mode = kNormal, const char* extra = NULL) {
    fflush(stdout);
    fflush(stderr);
    int out[2], err[2];
    assert(pipe(out) == 0 && pipe(err) == 0);
    int master = -1;
    std::string slave;
    if (mode == kTty) {
        master = posix_openpt(O_RDWR | O_NOCTTY);
        assert(master >= 0 && grantpt(master) == 0 && unlockpt(master) == 0);
        slave = ptsname(master);
    }
    double t0 = now();
    pid_t pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
        close(out[0]);
        close(err[0]);
        if (mode == kTty) {
            int s = open(slave.c_str(), O_RDWR | O_NOCTTY);
            dup2(s, 1);
            close(s);
        } else {
            dup2(out[1], 1);
        }
        dup2(err[1], 2);
        close(out[1]);
        close(err[1]);
        char a0[] = "nbt-aad", a1[] = "--screenshot";
        char* av[] = {a0, a1, const_cast<char*>(extra), NULL};
        int rc = nbt::runScreenshotCommand(extra ? 3 : 2, av);
        fprintf(stderr, "fake ctx %d fmt %d usage %d size %dx%d reads %d count %d open %d/%d\n", st.ctxType, st.setFmt,
                st.setUsage, st.setW, st.setH, st.reads, st.readCount, st.openCtx, st.openPix);
        _exit(rc);
    }
    close(out[1]);
    close(err[1]);
    Result r;
    if (mode == kGone) close(out[0]);
    if (mode == kSlow) usleep(1500 * 1000);
    if (mode != kGone) r.out = slurp(out[0]);
    std::string e = slurp(err[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    if (master >= 0) close(master);
    r.code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    r.secs = now() - t0;
    size_t nl = e.find('\n');
    r.line = e.substr(0, nl);
    if (nl != std::string::npos) {
        std::string rest = e.substr(nl + 1);
        r.fakeLine = rest.substr(0, rest.find('\n'));
    }
    printf("  exit %d, %u bytes: %s | %s\n", r.code, (unsigned)r.out.size(), r.line.c_str(), r.fakeLine.c_str());
    return r;
}

bool starts(const std::string& s, const char* prefix) { return s.compare(0, strlen(prefix), prefix) == 0; }
bool ends(const std::string& s, const char* suffix) {
    size_t n = strlen(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}
bool has(const std::string& s, const char* part) { return s.find(part) != std::string::npos; }

// Failed step: its exit code and line, nothing on stdout, every libscreen object released.
Result expectFail(int code, const char* linePrefix, Mode mode = kNormal, const char* extra = NULL) {
    Result r = run(mode, extra);
    assert(r.code == code && starts(r.line, linePrefix));
    assert(r.out.empty() && has(r.fakeLine, "open 0/0"));
    return r;
}

void bigDisplay() {
    st.w = 300;
    st.h = 100;
    st.stride = 0;
}

bool appendTo(void* ctx, const uint8_t* p, size_t n) {
    ((std::string*)ctx)->append((const char*)p, n);
    return true;
}

// The PNG the fake display should give: R = 0x80, G = y, B = x.
std::string expectedPng(int w, int h) {
    std::vector<uint8_t> raw;
    for (int y = 0; y < h; ++y) {
        raw.push_back(0);
        for (int x = 0; x < w; ++x) {
            raw.push_back(0x80);
            raw.push_back((uint8_t)y);
            raw.push_back((uint8_t)x);
        }
    }
    std::string s;
    assert(nbt::png::write(appendTo, &s, &raw[0], w, h));
    return s;
}

void ok() {
    fake::reset();
    Result r = run();
    assert(r.code == 0);
    assert(r.out == expectedPng(5, 3) && r.out.size() == 116);
    assert(starts(r.line, "screenshot ok 5x3 stride 28 fmt 8 read ") && has(r.line, " ms, copy "));
    assert(ends(r.line, " ms, 116 bytes"));
    assert(r.fakeLine == "fake ctx 8 fmt 8 usage 10 size 5x3 reads 1 count 0 open 0/0");
    puts("ok OK");
}

void contextRefused() {
    fake::reset();
    st.fail = fake::kContext;
    st.err = EACCES;
    Result r = expectFail(10, "screenshot FAIL context errno 13");
    assert(r.fakeLine == "fake ctx 8 fmt -1 usage -1 size -1x-1 reads 0 count -1 open 0/0");
    puts("context refused OK");
}

void noDisplays() {
    fake::reset();
    st.displays = 0;
    expectFail(11, "screenshot FAIL displays");
    puts("no displays OK");
}

void readFails() {
    fake::reset();
    st.fail = fake::kRead;
    st.err = EIO;
    Result r = expectFail(15, "screenshot FAIL read errno 5");
    assert(has(r.fakeLine, "reads 1 count 0"));
    puts("read fails OK");
}

void strideTooSmall() {
    fake::reset();
    st.stride = 16;
    Result r = expectFail(16, "screenshot FAIL layout");
    assert(r.line == "screenshot FAIL layout stride 16 < 20");
    puts("stride too small OK");
}

void wrongFormat() {
    fake::reset();
    st.fmt = 7;
    expectFail(16, "screenshot FAIL layout format 7");
    puts("wrong format OK");
}

void hangEndsByItself() {
    fake::reset();
    st.hang = true;
    Result r = run();
    assert(r.code == 19 && r.line == "screenshot FAIL timeout in read" && r.fakeLine.empty());
    assert(r.secs < 3.0 && r.out.empty());
    puts("hang ends by itself OK");
}

void usage() {
    fake::reset();
    Result r = expectFail(2, "screenshot FAIL usage", kNormal, "x");
    assert(starts(r.fakeLine, "fake ctx -1 "));
    puts("usage OK");
}

void terminalRefused() {
    fake::reset();
    Result r = expectFail(3, "screenshot FAIL terminal", kTty);
    assert(has(r.line, "\"/opt/aa/nbt-aad --screenshot\" > shot.png"));
    assert(starts(r.fakeLine, "fake ctx -1 "));
    puts("terminal refused OK");
}

void receiverGone() {
    fake::reset();
    bigDisplay();
    expectFail(17, "screenshot FAIL write errno 32", kGone);
    puts("receiver gone OK");
}

// More than a pipe buffer, read only after the 1 s guard: the transfer must not be cut off.
void slowReceiverNotKilled() {
    fake::reset();
    bigDisplay();
    Result r = run(kSlow);
    assert(r.code == 0 && starts(r.line, "screenshot ok 300x100 "));
    assert(r.out.size() == nbt::png::fileSize(300, 100) && r.out == expectedPng(300, 100));
    puts("slow receiver not killed OK");
}

}  // namespace

int main() {
    ok();
    contextRefused();
    noDisplays();
    readFails();
    strideTooSmall();
    wrongFormat();
    hangEndsByItself();
    usage();
    terminalRefused();
    receiverGone();
    slowReceiverNotKilled();
    puts("test_screenshot_cmd OK");
    return 0;
}
