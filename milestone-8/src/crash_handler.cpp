// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "crash_handler.hpp"

#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

namespace nbt {

volatile int g_crumb_stage = 0;
volatile int g_crumb_channel = -1;
volatile int g_crumb_msg = -1;

namespace {

void putNum(char* buf, size_t& n, long v) {
    char tmp[24];
    int k = 0;
    bool neg = v < 0;
    unsigned long u = neg ? (unsigned long)(-v) : (unsigned long)v;
    do { tmp[k++] = (char)('0' + (u % 10)); u /= 10; } while (u && k < 22);
    if (neg) buf[n++] = '-';
    while (k) buf[n++] = tmp[--k];
}

void put(char* buf, size_t& n, const char* s) {
    while (*s && n < 200) buf[n++] = *s++;
}

// Async-signal-safe: only write(2) and raise(). No printf/malloc here.
void fatalHandler(int sig) {
    char buf[256];
    size_t n = 0;
    put(buf, n, "\n[CRASH] fatal signal ");
    putNum(buf, n, sig);
    put(buf, n, sig == SIGSEGV ? " (SIGSEGV)" : sig == SIGBUS ? " (SIGBUS)" :
                sig == SIGILL ? " (SIGILL)" : sig == SIGFPE ? " (SIGFPE)" :
                sig == SIGABRT ? " (SIGABRT)" : "");
    put(buf, n, " stage=");
    putNum(buf, n, g_crumb_stage);
    put(buf, n, " last_channel=");
    putNum(buf, n, g_crumb_channel);
    put(buf, n, " last_msg=0x");
    {
        static const char hex[] = "0123456789ABCDEF";
        int v = g_crumb_msg;
        if (v < 0) { buf[n++] = '-'; buf[n++] = '1'; }
        else {
            bool started = false;
            for (int s = 28; s >= 0; s -= 4) {
                int d = (v >> s) & 0xF;
                if (d || started || s == 0) { buf[n++] = hex[d]; started = true; }
            }
        }
    }
    buf[n++] = '\n';
    ssize_t w = write(1, buf, n);
    (void)w;
    signal(sig, SIG_DFL);   // let the OS dump core / terminate as usual
    raise(sig);
}

} // namespace

void installProcessGuards() {
    // Without this, stdout redirected to a file is fully buffered and the tail of the
    // log (the part that explains a crash) is lost.
    setvbuf(stdout, NULL, _IOLBF, 0);

    // send() to a closed peer raises SIGPIPE, whose default action kills the daemon.
    signal(SIGPIPE, SIG_IGN);

    signal(SIGSEGV, fatalHandler);
    signal(SIGBUS,  fatalHandler);
    signal(SIGILL,  fatalHandler);
    signal(SIGFPE,  fatalHandler);
    signal(SIGABRT, fatalHandler);
}

} // namespace nbt
