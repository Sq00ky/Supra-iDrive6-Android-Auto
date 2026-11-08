// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef NBT_LOG_PUMP_HPP
#define NBT_LOG_PUMP_HPP

// Log pump (v1.9.58): file logging in which no logging thread ever waits for the file.
//
// The daemon logs with printf to a single line-buffered stdout. Writing that straight to a file means every '\n' is a
// write() made while holding the stdio lock, so one slow write blocks every thread that prints: the AA session thread
// (whose PingResponse waits behind its log line), the browser mixer, the main loop. The head unit mounts USB sticks
// with delayed writes off (etc/umass-enum.cfg: delwri=0), so a slow or failing stick stalls Android Auto itself.
//
// While a log file is in use (NBT_LOG_FILE, or "Save logs to USB"), fds 1 and 2 are the write end of a pipe instead:
//
//   printf (any thread), child processes ──► pipe ──► reader thread ──► RAM ring ──► writer thread ──► the file
//                                                       └──► terminal copy (non-blocking, when started from a shell)
//
// - The reader runs at a high priority and does nothing that can block except read() on the pipe: it copies to the
//   terminal with a non-blocking write and puts the bytes in the ring. When the ring is full it drops whole lines and
//   later puts a "[Log] N lines ... not saved" notice in their place.
// - The writer is the only code that touches the file: it opens (and rotates) the stick's file, fsyncs every 2 s,
//   checks the checkbox flag and the stick every 5 s, times every write and reports slow ones. With no file open,
//   lines go to the RAM log (/dev/shmem, 5 MB cap) when the daemon was using it, else nowhere but the terminal.
// - Once started, the pump stays until the daemon exits (shutdown(), bounded, so a dead stick cannot hang the exit).
//   The fatal-signal handler waits up to 1 s for the pump to drain (drainForCrash) and also writes its line to a RAM
//   copy, so the crash report survives a hung stick.
//
// NBT_LOG_RING_KB (64..8192, default 1024): ring size. NBT_LOG_SLOW_MS (0..10000): sleep this long before every file
// write, to prove on the car that a slow stick no longer stalls anything. NBT_LOG_READER_PRIO (default 22, above the
// USB reader's 21) and NBT_LOG_WRITER_PRIO (default 10); 0 = leave the thread's priority alone.

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

namespace nbt {
namespace logpump {

// Byte ring for whole log lines: one producer (the reader), one consumer (the writer). put() never blocks on I/O,
// it takes a chunk whole or drops it whole; peek()/consume() let the consumer write from the ring without a copy
// (the producer only ever writes into free space).
class LogRing {
public:
    explicit LogRing(size_t capacity);   // at least 1024 bytes, so the drop notice always fits
    ~LogRing();

    // Takes all n bytes, or none. While dropping, everything up to the next '\n' is dropped too (no half lines), and
    // the first chunk that fits again is preceded by the notice. Returns the bytes taken.
    size_t put(const char* p, size_t n);
    // Places a pending notice if there is room now (the writer calls it when it has caught up).
    void flushNotice();
    // Contiguous readable bytes from the oldest (at most max); *p points into the ring.
    size_t peek(const char** p, size_t max);
    void consume(size_t n);

    size_t used();
    // used(), read without the lock (one aligned word): for the fatal-signal handler, which must not lock.
    uint32_t usedNoLock() const { return usedW_; }
    size_t capacity() const { return cap_; }
    uint64_t droppedLines();
    uint64_t droppedBytes();

private:
    LogRing(const LogRing&);
    LogRing& operator=(const LogRing&);
    void copyIn(const char* p, size_t n);
    size_t notice(char* out, size_t n) const;
    void drop(const char* p, size_t n);

    pthread_mutex_t mu_;
    char* buf_;
    size_t cap_, head_, tail_, used_;
    volatile uint32_t usedW_;   // copy of used_
    bool dropping_;     // dropped since the last notice
    bool skipToNl_;      // the last dropped chunk ended mid-line: drop up to the next '\n'
    bool lastNl_;        // the last byte taken was '\n'
    uint64_t pendLines_, pendBytes_;           // for the next notice
    uint64_t droppedLines_, droppedBytes_;     // totals
};

// The file calls the writer makes; tests override them with slow or failing ones.
struct FileOps {
    virtual ~FileOps() {}
    virtual int openFile(const char* path);    // O_WRONLY | O_CREAT | O_APPEND, close-on-exec
    virtual ssize_t writeFile(int fd, const void* p, size_t n);
    virtual int syncFile(int fd);
    virtual int closeFile(int fd);
};

enum Mode { kUsb, kForced };   // "Save logs to USB" (stick found by the writer) / NBT_LOG_FILE=<path>

struct Config {
    Mode mode;
    const char* forcedPath;      // kForced: the file
    const char* mntRoot;         // kUsb: where sticks are mounted (/mnt)
    bool (*usbEnabled)();        // kUsb: the checkbox flag (usblog::enabled)
    const char* build;           // "v1.9.58-logpump (date time)", for the header of a file opened while running
    const char* ramPath;         // fallback while no file is open; NULL = none
    bool ramAppend;              // stdout was the RAM log already: append to it, its size counts toward the cap
    const char* crashCopyPath;   // the fatal-signal handler also writes its line here; NULL = no copy
    bool takeStderr;             // fd 2 into the pipe too (the daemon has always had stderr on stdout)
    size_t ringBytes;
    int readerPrio, writerPrio;  // 0 = leave alone
    unsigned slowMs;             // sleep before every file write (NBT_LOG_SLOW_MS)
    unsigned serviceMs;          // flag / stick check (5000)
    unsigned fsyncMs;            // fsync of the open file (2000)
    unsigned summaryMs;          // slow-write / drop summary, only when something changed (60000)
    FileOps* ops;                // NULL = the real calls
    Config();
};

// The daemon's settings, from the environment. ramWanted: the RAM log is the right fallback (stdout was the RAM log,
// or /dev/null with NBT_RAM_LOG not off and the RAM log not full).
Config daemonConfig(Mode mode, const char* forcedPath, bool origIsRam, bool ramWanted, const char* build);

// Moves fds 1 (and 2) onto the pipe and starts both threads. Prints nothing itself. False (stdout unchanged) if the
// pump is already running or a pipe / thread could not be made. Once per process.
bool start(const Config& c);
bool running();

// Ends the pump for exit: fds 1 and 2 go back to what they were, then up to timeoutMs for the writer to write what
// is left. True if everything was written. The pump is not running afterwards either way.
bool shutdown(unsigned timeoutMs);

// For the fatal-signal handler: async-signal-safe, waits up to timeoutMs until the pump has written all it was given.
void drainForCrash(unsigned timeoutMs);

struct Stats {
    uint64_t received;       // bytes read from the pipe
    uint64_t queued;         // of those, taken by the ring
    uint64_t written;        // taken from the ring by the writer (to the file, the RAM log or nowhere)
    uint64_t droppedLines, droppedBytes;
    uint64_t ttyMissed;      // bytes the terminal copy could not take
    uint64_t lostOnError;    // bytes of failed file writes
    uint32_t fileWrites, slow100, slow1000, longestMs;
    size_t ringUsed;         // bytes waiting in the ring
    bool fileOpen;
    char filePath[192];
};
Stats stats();

} // namespace logpump
} // namespace nbt

#endif
