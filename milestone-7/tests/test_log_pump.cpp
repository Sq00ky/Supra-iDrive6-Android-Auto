// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test for src/log_pump.cpp: the ring's drop policy, and the pump with slow, failing and blocking files, a stick
// that comes and goes, a terminal copy and a crash. Each pump scenario runs in its own child process (the pump takes
// over fd 1, once per process), under a private temporary directory.
#include "log_pump.hpp"
#include "crash_handler.hpp"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <string>

using namespace nbt::logpump;

static std::string g_dir;

static uint64_t nowMs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000);
}

static void sleepMs(unsigned ms) { usleep(ms * 1000u); }

static std::string readFile(const std::string& path) {
    std::string s;
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return s;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) s.append(buf, n);
    fclose(f);
    return s;
}

static bool has(const std::string& s, const char* needle) { return s.find(needle) != std::string::npos; }
static bool has(const std::string& s, const std::string& needle) { return s.find(needle) != std::string::npos; }

static std::string drain(LogRing& r) {
    std::string out;
    const char* p;
    size_t k;
    while ((k = r.peek(&p, 100)) > 0) {   // small pieces: crosses the wrap point
        out.append(p, k);
        r.consume(k);
    }
    return out;
}

// A test config: no stderr (the asserts must reach the test log), quick checks, priorities left alone.
static Config testConfig(Mode mode, const std::string& file) {
    static std::string keep;
    keep = file;
    Config c;
    c.mode = mode;
    c.forcedPath = keep.c_str();
    c.build = "test-build";
    c.takeStderr = false;
    c.readerPrio = 0;
    c.writerPrio = 0;
    c.serviceMs = 50;
    c.fsyncMs = 100;
    c.summaryMs = 3600000;
    return c;
}

// ---- the ring ---------------------------------------------------------------------------------------------------

static void testRing() {
    std::string line(99, 'a');
    line += '\n';   // 100 bytes
    {
        LogRing r(1024);
        assert(r.capacity() == 1024);
        for (int i = 0; i < 10; ++i) assert(r.put(line.data(), line.size()) == 100);
        assert(r.used() == 1000);
        assert(r.put(line.data(), line.size()) == 0);           // 24 free: dropped whole
        assert(r.droppedLines() == 1 && r.droppedBytes() == 100);
        assert(r.used() == 1000);
        std::string got = drain(r);
        assert(got.size() == 1000);
        assert(r.put("next\n", 5) == 5);                        // the notice goes in first
        got = drain(r);
        assert(got.find("[Log] 1 lines (100 bytes) not saved") == 0);
        assert(got.size() > 5 && got.compare(got.size() - 5, 5, "next\n") == 0);
    }
    {
        LogRing r(1024);
        for (int i = 0; i < 10; ++i) r.put(line.data(), line.size());
        assert(r.put(std::string(30, 'b').c_str(), 30) == 0);   // a cut line: no '\n'
        drain(r);
        assert(r.put("rest of b\nnew\n", 14) == 4);             // "rest of b\n" dropped with it, then the notice
        std::string got = drain(r);
        assert(got.find("[Log] 1 lines (40 bytes) not saved") == 0);
        assert(got.compare(got.size() - 4, 4, "new\n") == 0);
        assert(r.droppedLines() == 1 && r.droppedBytes() == 40);
    }
    {
        LogRing r(1024);                                         // flushNotice: no new line needed
        for (int i = 0; i < 10; ++i) r.put(line.data(), line.size());
        r.put(line.data(), line.size());
        drain(r);
        r.flushNotice();
        std::string got = drain(r);
        assert(got.find("[Log] 1 lines (100 bytes) not saved") == 0);
        r.flushNotice();
        assert(r.used() == 0);                                   // once only
    }
    {
        LogRing r(1024);                                         // wrap-around keeps the bytes in order
        std::string all, got;
        for (int i = 0; i < 200; ++i) {
            char l[40];
            int n = snprintf(l, sizeof(l), "line %d\n", i);
            assert(r.put(l, (size_t)n) == (size_t)n);
            all.append(l, (size_t)n);
            if (i % 7 == 6) got += drain(r);
        }
        got += drain(r);
        assert(got == all);
        assert(r.droppedBytes() == 0);
    }
    {
        LogRing r(1024);                                         // a partial line in the ring gets a newline before the notice
        r.put("half", 4);
        r.put(std::string(1100, 'c').c_str(), 1100);
        r.put("x\n", 2);   // skipped: the end of the dropped line
        r.put("y\n", 2);
        std::string got = drain(r);
        assert(got.find("half\n[Log] 1 lines") == 0);
        assert(got.compare(got.size() - 2, 2, "y\n") == 0);
    }
    fprintf(stderr, "[ok] ring\n");
}

// ---- pump scenarios (each in a child) -------------------------------------------------------------------------

static void basicForced() {
    const std::string f = g_dir + "/forced.log";
    Config c = testConfig(kForced, f);
    assert(start(c));
    assert(running());
    assert(!start(c));   // once per process
    for (int i = 0; i < 100; ++i) printf("line %03d\n", i);
    assert(shutdown(2000));
    assert(!running());
    std::string s = readFile(f);
    assert(s.find("---- nbt-aad log appended to " + f) == 0);
    assert(has(s, "[Log] log pump on: " ));
    size_t at = 0;
    for (int i = 0; i < 100; ++i) {
        char l[16];
        snprintf(l, sizeof(l), "line %03d\n", i);
        size_t k = s.find(l, at);
        assert(k != std::string::npos);
        at = k;
    }
    Stats st = stats();
    assert(st.droppedBytes == 0 && st.lostOnError == 0 && st.received == st.written);
}

// Four threads print 480 KB into a 64 KB ring while every file write takes 300 ms: no printf may wait for the file.
static uint64_t g_worstUs = 0;
static pthread_mutex_t g_worstMu = PTHREAD_MUTEX_INITIALIZER;

static void* printer(void* arg) {
    const int id = (int)(long)arg;
    uint64_t worst = 0;
    for (int i = 0; i < 2000; ++i) {
        struct timespec a, b;
        clock_gettime(CLOCK_MONOTONIC, &a);
        printf("printer %d line %04d ................................................\n", id, i);
        clock_gettime(CLOCK_MONOTONIC, &b);
        uint64_t us = (uint64_t)(b.tv_sec - a.tv_sec) * 1000000u + (uint64_t)((b.tv_nsec - a.tv_nsec) / 1000);
        if (us > worst) worst = us;
    }
    pthread_mutex_lock(&g_worstMu);
    if (worst > g_worstUs) g_worstUs = worst;
    pthread_mutex_unlock(&g_worstMu);
    return NULL;
}

static void slowFile() {
    const std::string f = g_dir + "/slow.log";
    Config c = testConfig(kForced, f);
    c.ringBytes = 64 * 1024;
    c.slowMs = 300;
    assert(start(c));
    sleepMs(100);
    pthread_t t[4];
    for (long i = 0; i < 4; ++i) assert(pthread_create(&t[i], NULL, &printer, (void*)i) == 0);
    for (int i = 0; i < 4; ++i) pthread_join(t[i], NULL);
    fprintf(stderr, "  slow file: worst printf %llu us\n", (unsigned long long)g_worstUs);
    assert(g_worstUs < 50000);   // 300 ms per file write; a printf that waited for one would show it
    Stats st = stats();
    assert(st.droppedLines > 0);
    const uint64_t t0 = nowMs();
    while (stats().ringUsed > 0 && nowMs() - t0 < 10000) sleepMs(50);   // the writer empties the ring, 300 ms a write
    assert(stats().ringUsed == 0);
    printf("last line\n");
    shutdown(10000);
    st = stats();
    std::string s = readFile(f);
    assert(has(s, "not saved: the log writer fell behind (log buffer of 64 KB full)"));
    assert(has(s, "last line\n"));
    assert(st.slow100 > 0 && st.longestMs >= 300);
    assert(has(s, "NBT_LOG_SLOW_MS=300"));
}

// A file write fails: the file is closed, lines go to the RAM fallback, and the file is opened again.
struct FailOnMarker : FileOps {
    int fails;
    FailOnMarker() : fails(0) {}
    ssize_t writeFile(int fd, const void* p, size_t n) {
        if (memmem(p, n, "LINE-B", 6)) {
            ++fails;
            errno = EIO;
            return -1;
        }
        return FileOps::writeFile(fd, p, n);
    }
};

static void writeError() {
    const std::string f = g_dir + "/fail.log", ram = g_dir + "/fail-ram.log";
    static FailOnMarker ops;
    Config c = testConfig(kForced, f);
    c.ops = &ops;
    c.ramPath = ram.c_str();
    assert(start(c));
    printf("LINE-A\n");
    sleepMs(200);
    printf("LINE-B\n");
    sleepMs(300);
    printf("LINE-C\n");
    shutdown(2000);
    assert(ops.fails == 1);
    std::string s = readFile(f), r = readFile(ram);
    assert(has(s, "LINE-A") && !has(s, "LINE-B") && has(s, "LINE-C"));
    assert(has(s, "earlier lines were not saved here"));   // the second header
    // The failure line lands in the RAM log, or in the file if the reopen came first.
    assert(has(r, "[Log] log file write failed (") || has(s, "[Log] log file write failed ("));
    assert(stats().lostOnError > 0);
}

// "Save logs to USB": lines go to the RAM log until a stick appears, then to the stick; the stick goes away; the box
// is unticked.
static pthread_mutex_t g_usbMu = PTHREAD_MUTEX_INITIALIZER;
static bool g_usbOn = true;
static bool usbOn() {
    pthread_mutex_lock(&g_usbMu);
    const bool on = g_usbOn;
    pthread_mutex_unlock(&g_usbMu);
    return on;
}
static void setUsbOn(bool on) {
    pthread_mutex_lock(&g_usbMu);
    g_usbOn = on;
    pthread_mutex_unlock(&g_usbMu);
}

static void stickLifecycle() {
    const std::string mnt = g_dir + "/mnt", ram = g_dir + "/usb-ram.log", stick = mnt + "/umass01200t7";
    assert(mkdir(mnt.c_str(), 0755) == 0);
    Config c = testConfig(kUsb, "");
    static std::string mntKeep;
    mntKeep = mnt;
    c.mntRoot = mntKeep.c_str();
    c.usbEnabled = &usbOn;
    c.ramPath = ram.c_str();
    assert(start(c));
    printf("before the stick\n");
    sleepMs(200);
    assert(!stats().fileOpen);
    assert(mkdir(stick.c_str(), 0755) == 0);
    sleepMs(200);
    Stats st = stats();
    assert(st.fileOpen && std::string(st.filePath) == stick + "/nbt-aad.log");
    printf("on the stick\n");
    sleepMs(200);
    std::string s = readFile(stick + "/nbt-aad.log");
    assert(s.find("---- nbt-aad log appended to " + stick + "/nbt-aad.log (Save logs to USB,") == 0);
    assert(has(s, "  Build: test-build: USB stick found while running, earlier lines were not saved here (see the RAM log)"));
    assert(has(s, "on the stick\n") && !has(s, "before the stick"));

    assert(system(("rm -rf " + stick).c_str()) == 0);       // pulled
    sleepMs(200);
    assert(!stats().fileOpen);
    printf("after the stick\n");
    sleepMs(200);
    std::string r = readFile(ram);
    assert(has(r, "before the stick\n"));
    assert(has(r, "[Log] USB stick found: logging to " + stick + "/nbt-aad.log (the RAM log "));
    assert(has(r, "[Log] back on the RAM log "));
    assert(has(r, "[Log] USB stick gone: USB log closed"));
    assert(has(r, "after the stick\n") && !has(r, "on the stick\n"));

    assert(mkdir(stick.c_str(), 0755) == 0);                // back, then the box is unticked
    sleepMs(200);
    assert(stats().fileOpen);
    setUsbOn(false);
    sleepMs(200);
    assert(!stats().fileOpen);
    printf("unticked\n");
    shutdown(2000);
    r = readFile(ram);
    if (!has(r, "[Log] Save logs to USB turned off: USB log closed") || !has(r, "unticked\n"))
        fprintf(stderr, "---- RAM log ----\n%s---- end ----\n", r.c_str());
    assert(has(r, "[Log] Save logs to USB turned off: USB log closed") && has(r, "unticked\n"));
}

// A file write that never returns: shutdown still comes back on time.
struct Blocking : FileOps {
    ssize_t writeFile(int fd, const void* p, size_t n) {
        if (memmem(p, n, "STUCK", 5)) sleepMs(5000);
        return FileOps::writeFile(fd, p, n);
    }
};

static void boundedShutdown() {
    static Blocking ops;
    Config c = testConfig(kForced, g_dir + "/blocked.log");
    c.ops = &ops;
    assert(start(c));
    printf("STUCK\n");
    sleepMs(200);   // the writer is in the write now
    const uint64_t t0 = nowMs();
    assert(!shutdown(300));
    const uint64_t took = nowMs() - t0;
    fprintf(stderr, "  bounded shutdown: %llu ms\n", (unsigned long long)took);
    assert(took < 1000);
    assert(!running());
}

// Started on a terminal: every line is copied there at once, a stalled terminal only loses its copy, and the
// terminal's own open file stays blocking (the SSH shell shares it).
static void terminalCopy() {
    int master = posix_openpt(O_RDWR | O_NOCTTY);
    assert(master >= 0 && grantpt(master) == 0 && unlockpt(master) == 0);
    int slave = open(ptsname(master), O_RDWR | O_NOCTTY);
    assert(slave >= 0);
    assert(dup2(slave, 1) == 1);
    const std::string f = g_dir + "/tty.log";
    Config c = testConfig(kForced, f);
    assert(start(c));
    assert((fcntl(slave, F_GETFL) & O_NONBLOCK) == 0);
    printf("TTY-LINE\n");
    std::string seen;
    const uint64_t t0 = nowMs();
    while (!has(seen, "TTY-LINE") && nowMs() - t0 < 2000) {
        struct pollfd pf = { master, POLLIN, 0 };
        if (poll(&pf, 1, 100) > 0) {
            char buf[512];
            ssize_t n = read(master, buf, sizeof(buf));
            if (n > 0) seen.append(buf, (size_t)n);
        }
    }
    assert(has(seen, "TTY-LINE"));
    // Nobody reads the terminal now: it fills, the file still gets everything.
    for (int i = 0; i < 20000; ++i) printf("filler %05d ..................................................\n", i);
    printf("TTY-LAST\n");
    shutdown(5000);
    Stats st = stats();
    assert(st.ttyMissed > 0);
    assert(st.droppedBytes == 0);
    std::string s = readFile(f);
    assert(has(s, "filler 19999") && has(s, "TTY-LAST\n"));
    assert(has(s, ", copy to the terminal"));
}

// A fatal signal: the report reaches the file through the pump, and the RAM copy.
static void crashReport() {
    struct rlimit rl = { 0, 0 };
    setrlimit(RLIMIT_CORE, &rl);
    nbt::installProcessGuards();
    const std::string f = g_dir + "/crash.log", copy = g_dir + "/crash-copy.log";
    static std::string copyKeep;
    copyKeep = copy;
    Config c = testConfig(kForced, f);
    c.crashCopyPath = copyKeep.c_str();
    c.slowMs = 200;   // the report has to wait for the pump
    assert(start(c));
    printf("before the crash\n");
    raise(SIGSEGV);
}

typedef void (*Scenario)();

static void runChild(const char* name, Scenario fn, int wantSignal) {
    fflush(stdout);
    fflush(stderr);
    pid_t pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
        alarm(60);
        setvbuf(stdout, NULL, _IOLBF, 0);   // as the daemon's installProcessGuards (the test log is a file)
        int nul = open("/dev/null", O_WRONLY);   // as the iDrive app starts the daemon: no copy of every line here
        if (nul >= 0) { dup2(nul, 1); close(nul); }
        fn();
        _exit(0);
    }
    int status = 0;
    assert(waitpid(pid, &status, 0) == pid);
    if (wantSignal) {
        if (!WIFSIGNALED(status) || WTERMSIG(status) != wantSignal) {
            fprintf(stderr, "[FAIL] %s: status 0x%x, wanted signal %d\n", name, status, wantSignal);
            exit(1);
        }
    } else if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fprintf(stderr, "[FAIL] %s: status 0x%x\n", name, status);
        exit(1);
    }
    fprintf(stderr, "[ok] %s\n", name);
}

int main() {
    char tmpl[] = "/tmp/nbt_logpump_XXXXXX";
    assert(mkdtemp(tmpl));
    g_dir = tmpl;

    testRing();
    runChild("forced file", &basicForced, 0);
    runChild("slow file", &slowFile, 0);
    runChild("write error", &writeError, 0);
    runChild("stick lifecycle", &stickLifecycle, 0);
    runChild("bounded shutdown", &boundedShutdown, 0);
    runChild("terminal copy", &terminalCopy, 0);
    runChild("crash report", &crashReport, SIGSEGV);
    {
        std::string s = readFile(g_dir + "/crash.log"), copy = readFile(g_dir + "/crash-copy.log");
        assert(has(s, "before the crash\n"));
        assert(has(s, "[CRASH] fatal signal 11 (SIGSEGV)"));
        assert(has(copy, "[CRASH] fatal signal 11 (SIGSEGV)"));
        fprintf(stderr, "[ok] crash report reached the file and the RAM copy\n");
    }

    assert(system(("rm -rf " + g_dir).c_str()) == 0);
    printf("test_log_pump: all passed\n");
    return 0;
}
