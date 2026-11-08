// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Log pump: see log_pump.hpp.
#include "log_pump.hpp"

#include "crash_handler.hpp"
#include "nbt_env.hpp"
#include "usb_log.hpp"

#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <string>

namespace nbt {
namespace logpump {

namespace {

uint64_t monoMs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000);
}

void cloexec(int fd) {
    if (fd >= 0) fcntl(fd, F_SETFD, FD_CLOEXEC);
}

uint64_t countNl(const char* p, size_t n) {
    uint64_t k = 0;
    for (size_t i = 0; i < n; ++i) k += p[i] == '\n';
    return k;
}

} // namespace

// ---- LogRing ----------------------------------------------------------------------------------------------------

LogRing::LogRing(size_t capacity)
    : buf_(NULL), cap_(0), head_(0), tail_(0), used_(0), usedW_(0), dropping_(false), skipToNl_(false), lastNl_(true),
      pendLines_(0), pendBytes_(0), droppedLines_(0), droppedBytes_(0) {
    pthread_mutex_init(&mu_, NULL);
    if (capacity < 1024) capacity = 1024;
    buf_ = (char*)malloc(capacity);
    if (buf_) cap_ = capacity;
}

LogRing::~LogRing() {
    free(buf_);
    pthread_mutex_destroy(&mu_);
}

void LogRing::copyIn(const char* p, size_t n) {
    size_t first = cap_ - head_;
    if (first > n) first = n;
    memcpy(buf_ + head_, p, first);
    if (n > first) memcpy(buf_, p + first, n - first);
    head_ = (head_ + n) % cap_;
    used_ += n;
    usedW_ = (uint32_t)used_;
    lastNl_ = p[n - 1] == '\n';
}

size_t LogRing::notice(char* out, size_t n) const {
    int k = snprintf(out, n, "%s[Log] %llu lines (%llu bytes) not saved: the log writer fell behind (log buffer of %u KB "
                     "full)\n", lastNl_ ? "" : "\n", (unsigned long long)pendLines_, (unsigned long long)pendBytes_,
                     (unsigned)(cap_ / 1024));
    if (k < 0) return 0;
    return (size_t)k < n ? (size_t)k : n - 1;
}

void LogRing::drop(const char* p, size_t n) {
    const uint64_t lines = countNl(p, n);
    dropping_ = true;
    pendLines_ += lines;
    pendBytes_ += n;
    droppedLines_ += lines;
    droppedBytes_ += n;
}

size_t LogRing::put(const char* p, size_t n) {
    if (!p || !n) return 0;
    pthread_mutex_lock(&mu_);
    if (!cap_) { pthread_mutex_unlock(&mu_); return 0; }
    if (skipToNl_) {                      // the rest of a line whose start was dropped
        const char* nl = (const char*)memchr(p, '\n', n);
        const size_t k = nl ? (size_t)(nl - p) + 1 : n;
        drop(p, k);
        if (nl) skipToNl_ = false;
        p += k;
        n -= k;
    }
    size_t taken = 0;
    if (n) {
        char note[192];
        const size_t nn = dropping_ ? notice(note, sizeof(note)) : 0;
        if (cap_ - used_ >= nn + n) {
            if (nn) {
                copyIn(note, nn);
                dropping_ = false;
                pendLines_ = pendBytes_ = 0;
            }
            copyIn(p, n);
            taken = n;
        } else {
            drop(p, n);
            if (p[n - 1] != '\n') skipToNl_ = true;
        }
    }
    pthread_mutex_unlock(&mu_);
    return taken;
}

void LogRing::flushNotice() {
    pthread_mutex_lock(&mu_);
    // Not in the middle of a dropped line: the notice would land inside the line's remainder otherwise.
    if (cap_ && dropping_ && !skipToNl_) {
        char note[192];
        const size_t nn = notice(note, sizeof(note));
        if (nn && cap_ - used_ >= nn) {
            copyIn(note, nn);
            dropping_ = false;
            pendLines_ = pendBytes_ = 0;
        }
    }
    pthread_mutex_unlock(&mu_);
}

size_t LogRing::peek(const char** p, size_t max) {
    pthread_mutex_lock(&mu_);
    size_t k = used_;
    if (k > cap_ - tail_) k = cap_ - tail_;
    if (k > max) k = max;
    *p = buf_ + tail_;
    pthread_mutex_unlock(&mu_);
    return k;
}

void LogRing::consume(size_t n) {
    pthread_mutex_lock(&mu_);
    if (n > used_) n = used_;
    if (cap_) tail_ = (tail_ + n) % cap_;
    used_ -= n;
    usedW_ = (uint32_t)used_;
    pthread_mutex_unlock(&mu_);
}

size_t LogRing::used() {
    pthread_mutex_lock(&mu_);
    const size_t u = used_;
    pthread_mutex_unlock(&mu_);
    return u;
}

uint64_t LogRing::droppedLines() {
    pthread_mutex_lock(&mu_);
    const uint64_t v = droppedLines_;
    pthread_mutex_unlock(&mu_);
    return v;
}

uint64_t LogRing::droppedBytes() {
    pthread_mutex_lock(&mu_);
    const uint64_t v = droppedBytes_;
    pthread_mutex_unlock(&mu_);
    return v;
}

// ---- file calls ---------------------------------------------------------------------------------------------------

int FileOps::openFile(const char* path) {
    int fd = ::open(path, O_WRONLY | O_CREAT | O_APPEND, 0666);
    cloexec(fd);
    return fd;
}
ssize_t FileOps::writeFile(int fd, const void* p, size_t n) { return ::write(fd, p, n); }
int FileOps::syncFile(int fd) { return ::fsync(fd); }
int FileOps::closeFile(int fd) { return ::close(fd); }

Config::Config()
    : mode(kUsb), forcedPath(NULL), mntRoot("/mnt"), usbEnabled(&usblog::enabled), build(""), ramPath(NULL),
      ramAppend(false), crashCopyPath(NULL), takeStderr(true), ringBytes(1024u * 1024u), readerPrio(22),
      writerPrio(10), slowMs(0), serviceMs(5000), fsyncMs(2000), summaryMs(60000), ops(NULL) {}

Config daemonConfig(Mode mode, const char* forcedPath, bool origIsRam, bool ramWanted, const char* build) {
    Config c;
    c.mode = mode;
    c.forcedPath = forcedPath;
    c.build = build;
    c.ramPath = ramWanted ? ramlog::kDaemonPath : NULL;
    c.ramAppend = origIsRam;
    c.crashCopyPath = "/dev/shmem/nbt-aad-crash.log";
    c.ringBytes = (size_t)envRange("NBT_LOG_RING_KB", 1024, 64, 8192) * 1024u;
    c.readerPrio = (int)envRange("NBT_LOG_READER_PRIO", 22, 0, 63);
    c.writerPrio = (int)envRange("NBT_LOG_WRITER_PRIO", 10, 0, 63);
    c.slowMs = (unsigned)envRange("NBT_LOG_SLOW_MS", 0, 0, 10000);
    return c;
}

// ---- the pump -----------------------------------------------------------------------------------------------------

namespace {

FileOps g_realOps;

struct Pump {
    Config cfg;
    std::string forcedPath, mntRoot, build, ramPath, crashPath;   // own copies of the Config strings
    LogRing* ring;
    FileOps* ops;
    int rfd;            // pipe read end
    int origOut;        // fd 1 as it was (restored by shutdown)
    int origErr;        // fd 2 as it was, when taken
    int ttyFd;          // terminal copy (reader only), non-blocking, its own open file
    int extraFd;        // the caller's own redirect (a file or a pipe), written by the writer

    pthread_t reader, writer;
    bool haveReader, haveWriter;
    pthread_mutex_t mu;
    pthread_cond_t cv;
    bool cvMono;

    // Under mu.
    bool readerDone, writerDone, stopping, shut;
    bool announced;     // the writer printed its "log pump on" line
    int readerPrioNow, writerPrioNow;
    bool readerReady;
    uint64_t rx, queued, done, ttyMissed, lostOnError;
    uint32_t fileWrites, slow100, slow1000, longestMs;
    bool fileOpenNow;
    char filePathNow[192];

    // Low 32 bits of rx, for drainForCrash (one writer, aligned word stores).
    volatile uint32_t rx32;

    // Writer only.
    int fileFd;
    char filePath[192];
    char stickDir[160];
    bool dirty;
    uint64_t lastFsync, nextService, nextSummary, lastSlowMsg;
    uint64_t ramBytes;
    ramlog::Capped ram;
    bool ramOn;
    bool openFailSaid;
    // what the last summary showed
    uint32_t sumSlow100;
    uint64_t sumDropped, sumTtyMissed, sumLost;

    Pump()
        : ring(NULL), ops(NULL), rfd(-1), origOut(-1), origErr(-1), ttyFd(-1), extraFd(-1), haveReader(false),
          haveWriter(false), cvMono(false), readerDone(false), writerDone(false), stopping(false), shut(false),
          announced(false),
          readerPrioNow(-1), writerPrioNow(-1), readerReady(false), rx(0), queued(0), done(0), ttyMissed(0),
          lostOnError(0), fileWrites(0), slow100(0), slow1000(0), longestMs(0), fileOpenNow(false), rx32(0),
          fileFd(-1), dirty(false), lastFsync(0), nextService(0), nextSummary(0),
          lastSlowMsg(0), ramBytes(0), ramOn(false), openFailSaid(false), sumSlow100(0), sumDropped(0),
          sumTtyMissed(0), sumLost(0) {
        filePathNow[0] = filePath[0] = stickDir[0] = 0;
    }
};

// Created once and never freed: the daemon leaves with _exit while these threads may still run.
Pump* volatile g_pump = NULL;

int threadPrio() {
    struct sched_param sp;
    int policy;
    return pthread_getschedparam(pthread_self(), &policy, &sp) == 0 ? sp.sched_priority : -1;
}

void setPrio(int prio, bool onlyRaise) {
#ifdef __QNX__
    if (prio <= 0) return;
    struct sched_param sp;
    int policy;
    if (pthread_getschedparam(pthread_self(), &policy, &sp) != 0) return;
    if (onlyRaise && sp.sched_priority >= prio) return;
    sp.sched_priority = prio;
    pthread_setschedparam(pthread_self(), policy, &sp);
#else
    (void)prio;
    (void)onlyRaise;
#endif
}

void waitMs(Pump& p, unsigned ms) {
    struct timespec ts;
    clock_gettime(p.cvMono ? CLOCK_MONOTONIC : CLOCK_REALTIME, &ts);
    ts.tv_sec += ms / 1000;
    ts.tv_nsec += (long)(ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec += 1;
        ts.tv_nsec -= 1000000000L;
    }
    pthread_cond_timedwait(&p.cv, &p.mu, &ts);
}

// The writer's own lines go through stdout like any other, so they keep their place and reach the terminal too.
// The reader never prints: it would be writing into the pipe it alone empties.
void say(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    fflush(stdout);
}

bool writeAll(FileOps* ops, int fd, const char* p, size_t n) {
    while (n > 0) {
        ssize_t w = ops->writeFile(fd, p, n);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) {
            if (w == 0) errno = EIO;
            return false;
        }
        p += w;
        n -= (size_t)w;
    }
    return true;
}

// A header line straight into the newly opened file, ahead of whatever waits in the ring.
void fileLine(Pump& p, const char* fmt, ...) {
    char line[384];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if (n <= 0 || p.fileFd < 0) return;
    if (n >= (int)sizeof(line)) n = (int)sizeof(line) - 1;
    if (writeAll(p.ops, p.fileFd, line, (size_t)n)) p.dirty = true;
}

void ramLine(Pump& p, const char* line) {
    if (!p.ramOn) return;
    ramlog::append(p.ram, p.ramPath.c_str(), line, strlen(line));
    if (p.ram.full) p.ramOn = false;
}

void publishFile(Pump& p) {
    pthread_mutex_lock(&p.mu);
    p.fileOpenNow = p.fileFd >= 0;
    snprintf(p.filePathNow, sizeof(p.filePathNow), "%s", p.fileFd >= 0 ? p.filePath : "");
    pthread_mutex_unlock(&p.mu);
}

const char* fileWord(const Pump& p) { return p.cfg.mode == kUsb ? "USB log" : "log file"; }

void noteTiming(Pump& p, uint64_t ms, size_t bytes, const char* what, bool isWrite) {
    pthread_mutex_lock(&p.mu);
    if (isWrite) ++p.fileWrites;
    if (ms >= 100) ++p.slow100;
    if (ms >= 1000) ++p.slow1000;
    if (ms > p.longestMs) p.longestMs = (uint32_t)ms;
    pthread_mutex_unlock(&p.mu);
    const uint64_t now = monoMs();
    if (ms >= 1000 && (p.lastSlowMsg == 0 || now - p.lastSlowMsg >= 10000)) {
        p.lastSlowMsg = now;
        say("[Log] a %s %s took %u ms (%u bytes)%s\n", fileWord(p), what, (unsigned)ms, (unsigned)bytes,
            p.cfg.mode == kUsb ? ": the USB stick is slow (nothing else waits for it)" : "");
    }
}

void timedFsync(Pump& p) {
    if (p.fileFd < 0) return;
    const uint64_t t0 = monoMs();
    p.ops->syncFile(p.fileFd);
    noteTiming(p, monoMs() - t0, 0, "fsync", false);
    p.dirty = false;
    p.lastFsync = monoMs();
}

void closeFile(Pump& p) {
    if (p.fileFd < 0) return;
    p.ops->closeFile(p.fileFd);
    p.fileFd = -1;
    p.dirty = false;
    publishFile(p);
}

// No file any more: back to the RAM log, when that is the fallback.
void backToRam(Pump& p) {
    if (p.ramPath.empty() || p.ram.full) return;
    p.ramOn = true;
    if (p.ram.opened) {
        char line[256];
        snprintf(line, sizeof(line), "[Log] back on the RAM log %s\n", p.ramPath.c_str());
        ramLine(p, line);
    }
}

bool openFile(Pump& p, const char* path) {
    const int fd = p.ops->openFile(path);
    if (fd < 0) {
        if (!p.openFailSaid) say("[Log] cannot open %s: %s (trying again every %u s)\n", path, strerror(errno),
                                 p.cfg.serviceMs / 1000);
        p.openFailSaid = true;
        return false;
    }
    p.openFailSaid = false;
    p.fileFd = fd;
    snprintf(p.filePath, sizeof(p.filePath), "%s", path);
    p.lastFsync = monoMs();
    if (p.ramOn && p.ram.opened) {
        char line[384];
        snprintf(line, sizeof(line), "[Log] %s: logging to %s (the RAM log %s stops here)\n",
                 p.cfg.mode == kUsb ? "USB stick found" : "log file open", path, p.ramPath.c_str());
        ramLine(p, line);
    }
    p.ramOn = false;
    fileLine(p, "---- nbt-aad log appended to %s (%swritten by the log pump, fsync every %u s) ----\n", path,
             p.cfg.mode == kUsb ? "Save logs to USB, " : "", p.cfg.fsyncMs / 1000);
    if (p.done > 0) {   // lines went elsewhere before this file was open
        char v[96];
        huVersion(v, sizeof(v));
        fileLine(p, "  Build: %s: %s while running, earlier lines were not saved here%s\n", p.build.c_str(),
                 p.cfg.mode == kUsb ? "USB stick found" : "log file opened", p.ramBytes ? " (see the RAM log)" : "");
        fileLine(p, "  HU software / nav version: %s\n", v);
    }
    publishFile(p);
    say("[Log] logging to %s\n", path);
    return true;
}

// Every serviceMs, on the writer: the only place that looks at the stick.
void service(Pump& p) {
    if (p.cfg.mode == kForced) {
        if (p.fileFd < 0) openFile(p, p.forcedPath.c_str());
        return;
    }
    const bool on = p.cfg.usbEnabled ? p.cfg.usbEnabled() : false;
    if (p.fileFd >= 0) {
        if (on && usblog::isWritableDir(p.stickDir)) return;
        if (!on) timedFsync(p);
        closeFile(p);
        backToRam(p);
        say("[Log] %s: USB log closed\n", on ? "USB stick gone" : "Save logs to USB turned off");
        return;
    }
    if (!on) return;
    char stick[160];
    if (!usblog::findStick(stick, sizeof(stick), p.mntRoot.c_str())) return;
    char path[192];
    usblog::logPath(path, sizeof(path), stick, usblog::kDaemonName);
    if (openFile(p, path)) snprintf(p.stickDir, sizeof(p.stickDir), "%s", stick);
}

void sinkWrite(Pump& p, const char* d, size_t k) {
    if (p.fileFd >= 0) {
        const uint64_t t0 = monoMs();
        if (p.cfg.slowMs) usleep((useconds_t)p.cfg.slowMs * 1000u);
        const bool ok = writeAll(p.ops, p.fileFd, d, k);
        const int err = errno;
        noteTiming(p, monoMs() - t0, k, "write", true);
        if (ok) {
            p.dirty = true;
        } else {
            closeFile(p);
            pthread_mutex_lock(&p.mu);
            p.lostOnError += k;
            pthread_mutex_unlock(&p.mu);
            backToRam(p);
            say("[Log] %s write failed (%s): file closed, %u bytes lost, trying again in %u s\n", fileWord(p),
                strerror(err), (unsigned)k, p.cfg.serviceMs / 1000);
        }
    } else if (p.ramOn) {
        ramlog::append(p.ram, p.ramPath.c_str(), d, k);
        p.ramBytes += k;
        if (p.ram.full) p.ramOn = false;
    }
    if (p.extraFd >= 0 && !writeAll(&g_realOps, p.extraFd, d, k)) {
        ::close(p.extraFd);
        p.extraFd = -1;
    }
}

void summary(Pump& p) {
    pthread_mutex_lock(&p.mu);
    const uint32_t writes = p.fileWrites, s100 = p.slow100, s1000 = p.slow1000, longest = p.longestMs;
    const uint64_t tty = p.ttyMissed, lost = p.lostOnError;
    pthread_mutex_unlock(&p.mu);
    const uint64_t dl = p.ring->droppedLines(), db = p.ring->droppedBytes();
    if (s100 == p.sumSlow100 && dl == p.sumDropped && tty == p.sumTtyMissed && lost == p.sumLost) return;
    p.sumSlow100 = s100;
    p.sumDropped = dl;
    p.sumTtyMissed = tty;
    p.sumLost = lost;
    say("[Log] log pump since start: %u file writes, %u slow (> 100 ms), %u over 1 s, longest %u ms; not saved %llu "
        "lines (%llu bytes), lost on write errors %llu bytes, terminal missed %llu bytes\n", writes, s100, s1000, longest,
        (unsigned long long)dl, (unsigned long long)db, (unsigned long long)lost, (unsigned long long)tty);
}

void* readerMain(void*) {
    Pump& p = *g_pump;
    setPrio(p.cfg.readerPrio, true);
    pthread_mutex_lock(&p.mu);
    p.readerPrioNow = threadPrio();
    p.readerReady = true;
    pthread_cond_broadcast(&p.cv);
    pthread_mutex_unlock(&p.mu);
    char buf[4096];
    for (;;) {
        const ssize_t n = ::read(p.rfd, buf, sizeof(buf));
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        uint64_t miss = 0;
        if (p.ttyFd >= 0) {
            const ssize_t w = ::write(p.ttyFd, buf, (size_t)n);
            if (w < 0) {
                miss = (uint64_t)n;
                if (errno != EAGAIN && errno != EINTR) {   // terminal gone (SSH closed): stop copying
                    ::close(p.ttyFd);
                    p.ttyFd = -1;
                }
            } else if (w < n) {
                miss = (uint64_t)(n - w);
            }
        }
        const size_t taken = p.ring->put(buf, (size_t)n);
        pthread_mutex_lock(&p.mu);
        p.rx += (uint64_t)n;
        p.queued += taken;
        p.ttyMissed += miss;
        p.rx32 = (uint32_t)p.rx;
        pthread_cond_broadcast(&p.cv);
        pthread_mutex_unlock(&p.mu);
    }
    pthread_mutex_lock(&p.mu);
    p.readerDone = true;
    pthread_cond_broadcast(&p.cv);
    pthread_mutex_unlock(&p.mu);
    return NULL;
}

void* writerMain(void*) {
    Pump& p = *g_pump;
    setPrio(p.cfg.writerPrio, false);
    const uint64_t t0 = monoMs();
    pthread_mutex_lock(&p.mu);
    p.writerPrioNow = threadPrio();
    while (!p.readerReady && monoMs() - t0 < 200) waitMs(p, 20);
    const int rprio = p.readerPrioNow, wprio = p.writerPrioNow;
    pthread_mutex_unlock(&p.mu);
    char extra[96] = "";
    if (p.cfg.slowMs) snprintf(extra, sizeof(extra), "; NBT_LOG_SLOW_MS=%u: every file write waits that long (test)",
                               p.cfg.slowMs);
    say("[Log] log pump on: %s; buffer %u KB, reader priority %d, writer priority %d%s%s%s\n",
        p.cfg.mode == kUsb ? "Save logs to USB (first writable stick under /mnt)" : p.forcedPath.c_str(),
        (unsigned)(p.ring->capacity() / 1024), rprio, wprio, p.ttyFd >= 0 ? ", copy to the terminal" : "",
        p.extraFd >= 0 ? ", copy to the original stdout" : "", extra);
    pthread_mutex_lock(&p.mu);
    p.announced = true;
    pthread_cond_broadcast(&p.cv);
    pthread_mutex_unlock(&p.mu);

    const uint64_t start = monoMs();
    p.nextSummary = start + p.cfg.summaryMs;
    uint64_t lastRx = 0, quietSince = start;
    bool stopSeen = false;
    pthread_mutex_lock(&p.mu);
    for (;;) {
        const uint64_t now = monoMs();
        if (p.rx != lastRx || (p.stopping && !stopSeen)) {   // quiet counts from the stop at the earliest: what is
            lastRx = p.rx;                                   // still in the pipe gets read first
            quietSince = now;
            stopSeen = p.stopping;
        }
        p.ring->flushNotice();
        const bool empty = p.ring->used() == 0;
        if (empty && p.readerDone) break;
        if (empty && p.stopping && now - quietSince >= 100) break;   // a child may still hold the pipe: don't wait for EOF
        const bool due = now >= p.nextService || now >= p.nextSummary ||
                         (p.fileFd >= 0 && p.dirty && now - p.lastFsync >= p.cfg.fsyncMs);
        if (empty && !due) {
            waitMs(p, p.stopping ? 20 : 250);
            continue;
        }
        pthread_mutex_unlock(&p.mu);

        if (now >= p.nextService) {
            service(p);
            p.nextService = monoMs() + p.cfg.serviceMs;
        }
        const char* d = NULL;
        const size_t k = p.ring->peek(&d, 32768);
        if (k) {
            sinkWrite(p, d, k);
            p.ring->consume(k);
            pthread_mutex_lock(&p.mu);
            p.done += k;
            pthread_mutex_unlock(&p.mu);
        }
        if (p.fileFd >= 0 && p.dirty && monoMs() - p.lastFsync >= p.cfg.fsyncMs) timedFsync(p);
        if (monoMs() >= p.nextSummary) {
            summary(p);
            p.nextSummary = monoMs() + p.cfg.summaryMs;
        }
        pthread_mutex_lock(&p.mu);
    }
    pthread_mutex_unlock(&p.mu);
    if (p.dirty) timedFsync(p);
    pthread_mutex_lock(&p.mu);
    p.writerDone = true;
    pthread_cond_broadcast(&p.cv);
    pthread_mutex_unlock(&p.mu);
    return NULL;
}

void crashDrain() { drainForCrash(1000); }

bool sameFile(int fd, const char* path) {
    struct stat a, b;
    return fstat(fd, &a) == 0 && stat(path, &b) == 0 && a.st_dev == b.st_dev && a.st_ino == b.st_ino;
}

} // namespace

bool start(const Config& c) {
    if (g_pump) return false;
    Pump* p = new Pump;
    p->cfg = c;
    p->forcedPath = c.forcedPath ? c.forcedPath : "";
    p->mntRoot = c.mntRoot ? c.mntRoot : "/mnt";
    p->build = c.build ? c.build : "";
    p->ramPath = c.ramPath ? c.ramPath : "";
    p->crashPath = c.crashCopyPath ? c.crashCopyPath : "";
    p->ops = c.ops ? c.ops : &g_realOps;
    p->ring = new LogRing(c.ringBytes);
    int pf[2];
    if (!p->ring->capacity() || pipe(pf) != 0) {
        delete p->ring;
        delete p;
        return false;
    }
    cloexec(pf[0]);
    p->rfd = pf[0];

    fflush(stdout);
    fflush(stderr);
    p->origOut = dup(1);
    cloexec(p->origOut);
    if (c.takeStderr) {
        p->origErr = dup(2);
        cloexec(p->origErr);
    }
    // Where stdout went so far keeps getting the lines, except /dev/null and the RAM log (the RAM sink has that).
    if (isatty(1)) {
        // Its own open file: O_NONBLOCK on a dup would also make the SSH login shell's terminal non-blocking.
        const char* tn = ttyname(1);
        p->ttyFd = tn ? ::open(tn, O_WRONLY | O_NONBLOCK | O_NOCTTY) : -1;
        cloexec(p->ttyFd);
        if (p->ttyFd < 0) {
            p->extraFd = dup(1);
            cloexec(p->extraFd);
        }
    } else if (!sameFile(1, "/dev/null") && !c.ramAppend) {
        p->extraFd = dup(1);
        cloexec(p->extraFd);
    }
    p->ramOn = c.ramPath != NULL;
    if (c.ramPath && c.ramAppend) {
        struct stat st;
        p->ram.opened = true;
        p->ram.used = stat(c.ramPath, &st) == 0 ? (long)st.st_size : 0;
        if (p->ram.used >= ramlog::kMaxBytes - ramlog::kReserve) {
            p->ram.full = true;
            p->ramOn = false;
        }
    }

    pthread_mutex_init(&p->mu, NULL);
    pthread_condattr_t ca;
    pthread_condattr_init(&ca);
    p->cvMono = pthread_condattr_setclock(&ca, CLOCK_MONOTONIC) == 0;
    if (pthread_cond_init(&p->cv, &ca) != 0) {
        p->cvMono = false;
        pthread_cond_init(&p->cv, NULL);
    }
    pthread_condattr_destroy(&ca);

    g_pump = p;
    if (dup2(pf[1], 1) < 0 || (c.takeStderr && dup2(pf[1], 2) < 0)) {
        dup2(p->origOut, 1);
        if (p->origErr >= 0) dup2(p->origErr, 2);
        ::close(pf[0]);
        ::close(pf[1]);
        g_pump = NULL;
        return false;
    }
    ::close(pf[1]);
    p->haveReader = pthread_create(&p->reader, NULL, &readerMain, NULL) == 0;
    p->haveWriter = p->haveReader && pthread_create(&p->writer, NULL, &writerMain, NULL) == 0;
    if (!p->haveWriter) {
        // Back as it was; a started reader sees EOF and ends. The Pump stays allocated for it.
        dup2(p->origOut, 1);
        if (p->origErr >= 0) dup2(p->origErr, 2);
        if (!p->haveReader) ::close(pf[0]);
        p->shut = true;
        return false;
    }
    if (!p->crashPath.empty()) g_crash_copy_path = p->crashPath.c_str();
    g_log_drain_hook = &crashDrain;
    return true;
}

bool running() {
    Pump* p = g_pump;
    if (!p) return false;
    pthread_mutex_lock(&p->mu);
    const bool r = !p->shut;
    pthread_mutex_unlock(&p->mu);
    return r;
}

bool shutdown(unsigned timeoutMs) {
    Pump* p = g_pump;
    if (!p || !running()) return true;
    fflush(stdout);
    fflush(stderr);
    g_log_drain_hook = NULL;
    const uint64_t t0 = monoMs();
    pthread_mutex_lock(&p->mu);
    while (!p->announced && !p->writerDone && monoMs() - t0 < timeoutMs) waitMs(*p, 20);   // its line is ours too
    p->stopping = true;
    p->shut = true;
    pthread_cond_broadcast(&p->cv);
    pthread_mutex_unlock(&p->mu);
    // Our write ends go; the reader then sees EOF (unless a child still holds one: the writer stops on quiet).
    dup2(p->origOut, 1);
    if (p->origErr >= 0) dup2(p->origErr, 2);

    pthread_mutex_lock(&p->mu);
    while (!p->writerDone && monoMs() - t0 < timeoutMs) waitMs(*p, 20);
    const bool done = p->writerDone;
    const bool readerDone = p->readerDone;
    const bool all = done && p->ring->used() == 0 && p->ring->droppedBytes() == 0 && p->lostOnError == 0;
    pthread_mutex_unlock(&p->mu);
    if (done) pthread_join(p->writer, NULL);
    if (readerDone) pthread_join(p->reader, NULL);
    return all;
}

void drainForCrash(unsigned timeoutMs) {
    Pump* p = g_pump;
    if (!p) return;
    uint32_t lastRx = p->rx32;
    unsigned quiet = 0;
    for (unsigned t = 0; t < timeoutMs; t += 20) {
        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = 20L * 1000000L;
        nanosleep(&ts, NULL);
        const uint32_t rx = p->rx32;
        if (rx != lastRx) {
            lastRx = rx;
            quiet = 0;
            continue;
        }
        quiet += 20;
        if (quiet >= 100 && p->ring->usedNoLock() == 0) return;   // consumed only after the write returned
    }
}

Stats stats() {
    Stats s;
    memset(&s, 0, sizeof(s));
    Pump* p = g_pump;
    if (!p) return s;
    pthread_mutex_lock(&p->mu);
    s.received = p->rx;
    s.queued = p->queued;
    s.written = p->done;
    s.ttyMissed = p->ttyMissed;
    s.lostOnError = p->lostOnError;
    s.fileWrites = p->fileWrites;
    s.slow100 = p->slow100;
    s.slow1000 = p->slow1000;
    s.longestMs = p->longestMs;
    s.fileOpen = p->fileOpenNow;
    snprintf(s.filePath, sizeof(s.filePath), "%s", p->filePathNow);
    pthread_mutex_unlock(&p->mu);
    s.droppedLines = p->ring->droppedLines();
    s.droppedBytes = p->ring->droppedBytes();
    s.ringUsed = p->ring->used();
    return s;
}

} // namespace logpump
} // namespace nbt
