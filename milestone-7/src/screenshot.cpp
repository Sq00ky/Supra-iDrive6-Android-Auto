// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
//
// nbt-aad --screenshot: reads the composed display 0 once and writes it to stdout as a PNG. Property ids and flags are
// QNX 6.5.0 SP1's (the unit's libscreen is SP1); include/screen_video.h differs and is not used here.

#include "screenshot.hpp"
#include "screenshot_png.hpp"
#include "nbt_paths.hpp"

#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifndef NBT_SCREENSHOT_TIMEOUT_S
#define NBT_SCREENSHOT_TIMEOUT_S 10
#endif

extern "C" {
typedef void* screen_context_t;
typedef void* screen_display_t;
typedef void* screen_pixmap_t;
typedef void* screen_buffer_t;
int screen_create_context(screen_context_t* ctx, int type);
int screen_destroy_context(screen_context_t ctx);
int screen_get_context_property_iv(screen_context_t ctx, int prop, int* val);
int screen_get_context_property_pv(screen_context_t ctx, int prop, void** val);
int screen_get_display_property_iv(screen_display_t disp, int prop, int* val);
int screen_create_pixmap(screen_pixmap_t* pix, screen_context_t ctx);
int screen_destroy_pixmap(screen_pixmap_t pix);
int screen_set_pixmap_property_iv(screen_pixmap_t pix, int prop, const int* val);
int screen_create_pixmap_buffer(screen_pixmap_t pix);
int screen_get_pixmap_property_pv(screen_pixmap_t pix, int prop, void** val);
int screen_get_buffer_property_iv(screen_buffer_t buf, int prop, int* val);
int screen_get_buffer_property_pv(screen_buffer_t buf, int prop, void** val);
int screen_read_display(screen_display_t disp, screen_buffer_t buf, int count, const int* rects, int flags);
}

namespace {

const int kCtxDisplayManager = 1 << 3;
const int kPropBufferSize = 5;
const int kPropFormat = 14;
const int kPropPointer = 34;
const int kPropRenderBuffers = 37;
const int kPropSize = 40;
const int kPropStride = 44;
const int kPropUsage = 48;
const int kPropDisplayCount = 59;
const int kPropDisplays = 60;
const int kUsageRead = 1 << 1;
const int kUsageNative = 1 << 3;
const int kFormatRgba8888 = 8;
const int kFormatRgbx8888 = 9;

enum {
    kExitOk = 0,
    kExitUsage = 2,
    kExitTerminal = 3,
    kExitContext = 10,
    kExitDisplays = 11,
    kExitSize = 12,
    kExitPixmap = 13,
    kExitBuffer = 14,
    kExitRead = 15,
    kExitLayout = 16,
    kExitWrite = 17,
    kExitMemory = 18,
    kExitTimeout = 19
};

const char* volatile g_step = "";

// Async-signal-safe (write only). False with errno set.
bool writeAll(int fd, const void* data, size_t n) {
    const char* p = (const char*)data;
    while (n > 0) {
        ssize_t w = write(fd, p, n);
        if (w < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        p += w;
        n -= (size_t)w;
    }
    return true;
}

void onAlarm(int) {
    static const char kHead[] = "screenshot FAIL timeout in ";
    writeAll(2, kHead, sizeof kHead - 1);
    writeAll(2, g_step, strlen(g_step));
    writeAll(2, "\n", 1);
    _exit(kExitTimeout);
}

long monoMs() {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long)t.tv_sec * 1000L + t.tv_nsec / 1000000L;
}

struct Shot {
    int w, h, stride, fmt;
    long readMs, copyMs;
    uint8_t* raw;  // PNG rows, see nbt::png::packRows
    const char* step;
    char why[160];
};

struct Screen {
    screen_context_t ctx;
    screen_display_t* disps;
    screen_pixmap_t pix;
};

int fail(Shot& s, int code, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
int fail(Shot& s, int code, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s.why, sizeof s.why, fmt, ap);
    va_end(ap);
    return code;
}

int failErrno(Shot& s, int code, int err) { return fail(s, code, "errno %d (%s)", err, strerror(err)); }

int grab(Shot& s, Screen& scr) {
    g_step = "context";
    if (screen_create_context(&scr.ctx, kCtxDisplayManager) != 0) {
        scr.ctx = NULL;
        return failErrno(s, kExitContext, errno);
    }

    g_step = "displays";
    int count = 0;
    if (screen_get_context_property_iv(scr.ctx, kPropDisplayCount, &count) != 0) return failErrno(s, kExitDisplays, errno);
    if (count < 1) return fail(s, kExitDisplays, "count %d", count);
    scr.disps = (screen_display_t*)calloc((size_t)count, sizeof(screen_display_t));
    if (!scr.disps) return failErrno(s, kExitDisplays, ENOMEM);
    if (screen_get_context_property_pv(scr.ctx, kPropDisplays, (void**)scr.disps) != 0)
        return failErrno(s, kExitDisplays, errno);
    screen_display_t disp = scr.disps[0];

    g_step = "size";
    int size[2] = {0, 0};
    if (screen_get_display_property_iv(disp, kPropSize, size) != 0) return failErrno(s, kExitSize, errno);
    if (!nbt::png::sizeOk(size[0], size[1])) return fail(s, kExitSize, "%dx%d", size[0], size[1]);
    s.w = size[0];
    s.h = size[1];

    g_step = "pixmap";
    if (screen_create_pixmap(&scr.pix, scr.ctx) != 0) {
        scr.pix = NULL;
        return failErrno(s, kExitPixmap, errno);
    }
    const int format = kFormatRgba8888;
    const int usage = kUsageRead | kUsageNative;
    if (screen_set_pixmap_property_iv(scr.pix, kPropFormat, &format) != 0 ||
        screen_set_pixmap_property_iv(scr.pix, kPropUsage, &usage) != 0 ||
        screen_set_pixmap_property_iv(scr.pix, kPropBufferSize, size) != 0)
        return failErrno(s, kExitPixmap, errno);

    g_step = "buffer";
    screen_buffer_t buf = NULL;
    if (screen_create_pixmap_buffer(scr.pix) != 0) return failErrno(s, kExitBuffer, errno);
    if (screen_get_pixmap_property_pv(scr.pix, kPropRenderBuffers, (void**)&buf) != 0)
        return failErrno(s, kExitBuffer, errno);

    g_step = "read";
    long t0 = monoMs();
    if (screen_read_display(disp, buf, 0, NULL, 0) != 0) return failErrno(s, kExitRead, errno);
    long t1 = monoMs();
    s.readMs = t1 - t0;

    g_step = "layout";
    void* ptr = NULL;
    int bufSize[2] = {0, 0};
    if (screen_get_buffer_property_pv(buf, kPropPointer, &ptr) != 0 ||
        screen_get_buffer_property_iv(buf, kPropStride, &s.stride) != 0 ||
        screen_get_buffer_property_iv(buf, kPropFormat, &s.fmt) != 0 ||
        screen_get_buffer_property_iv(buf, kPropBufferSize, bufSize) != 0)
        return failErrno(s, kExitLayout, errno);
    if (!ptr) return fail(s, kExitLayout, "pointer NULL");
    if (bufSize[0] != s.w || bufSize[1] != s.h)
        return fail(s, kExitLayout, "buffer %dx%d != display %dx%d", bufSize[0], bufSize[1], s.w, s.h);
    if (s.fmt != kFormatRgba8888 && s.fmt != kFormatRgbx8888) return fail(s, kExitLayout, "format %d", s.fmt);
    if (s.stride < s.w * 4) return fail(s, kExitLayout, "stride %d < %d", s.stride, s.w * 4);

    g_step = "memory";
    s.raw = (uint8_t*)malloc(nbt::png::rawSize(s.w, s.h));
    if (!s.raw) return failErrno(s, kExitMemory, ENOMEM);
    nbt::png::packRows((const uint8_t*)ptr, s.w, s.h, s.stride, s.raw);
    s.copyMs = monoMs() - t1;
    return kExitOk;
}

// Reads display 0 into s.raw; libscreen is released before it returns, so the transfer never holds it.
int capture(Shot& s) {
    Screen scr;
    memset(&scr, 0, sizeof scr);
    int code = grab(s, scr);
    s.step = g_step;
    g_step = "cleanup";
    if (scr.pix) screen_destroy_pixmap(scr.pix);
    if (scr.ctx) screen_destroy_context(scr.ctx);
    free(scr.disps);
    return code;
}

// ctx: int that receives errno on failure.
bool putStdout(void* ctx, const uint8_t* p, size_t n) {
    if (writeAll(1, p, n)) return true;
    *(int*)ctx = errno;
    return false;
}

}  // namespace

int nbt::runScreenshotCommand(int argc, char**) {
    if (argc != 2) {
        fprintf(stderr, "screenshot FAIL usage (nbt-aad --screenshot takes no arguments)\n");
        return kExitUsage;
    }
    if (isatty(1)) {
        fprintf(stderr, "screenshot FAIL terminal (stdout is a terminal; run it from the laptop: "
                        "ssh ... \"%s/" NBT_DAEMON_NAME " --screenshot\" > shot.png)\n", appDir());
        return kExitTerminal;
    }
    signal(SIGPIPE, SIG_IGN);  // a vanished receiver becomes "FAIL write", not a silent death
    signal(SIGALRM, onAlarm);
    alarm(NBT_SCREENSHOT_TIMEOUT_S);

    Shot s;
    memset(&s, 0, sizeof s);
    int code = capture(s);
    alarm(0);  // the transfer may take as long as SSH needs

    if (code == kExitOk) {
        s.step = "write";
        int err = EIO;
        if (!png::write(putStdout, &err, s.raw, s.w, s.h)) code = failErrno(s, kExitWrite, err);
    }
    free(s.raw);

    if (code == kExitOk)
        fprintf(stderr, "screenshot ok %dx%d stride %d fmt %d read %ld ms, copy %ld ms, %u bytes\n", s.w, s.h, s.stride,
                s.fmt, s.readMs, s.copyMs, (unsigned)png::fileSize(s.w, s.h));
    else
        fprintf(stderr, "screenshot FAIL %s %s\n", s.step, s.why);
    return code;
}
