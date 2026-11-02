// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef NBT_USB_LOG_HPP
#define NBT_USB_LOG_HPP

// "Save logs to USB" (iDrive app checkbox, v1.8.23 / rhmiapp-10). Shared by the daemon and the iDrive app
// (header-only, the app includes it as "../src/usb_log.hpp").
//
// Off (default): no USB file. The daemon and the app log to RAM instead (/dev/shmem/nbt-aad.log and
// /dev/shmem/nbt-aad-app.log, v1.8.67): each file is started fresh per run and capped at 5 MB; once the next line
// would pass the cap, logging to RAM stops for that run (a last "[Log] RAM log full" line says so; nothing more is
// written until the next start). NBT_RAM_LOG=0 in nbt-aad.env turns the RAM log off. The app still writes slog.
// On: the flag file exists; the daemon and the app look for a writable USB stick under /mnt (iDrive mounts sticks
// as /mnt/umass<...>) and append to <stick>/nbt-aad.log and <stick>/nbt-aad-app.log. A stick plugged in later is
// picked up within a few seconds; a removed stick stops the file logging until one is back.
// NBT_LOG_FILE=<path> in nbt-aad.env still forces a daemon log file regardless of the checkbox.

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

namespace nbt {
namespace usblog {

static const char* const kFlag       = "/var/opt/aa/usblog";   // written by the checkbox (persistent partition)
static const char* const kFlagManual = "/opt/aa/usblog";       // hand-made flag (read only)
static const char* const kFlagDir    = "/var/opt/aa";
static const char* const kDaemonName = "nbt-aad.log";
static const char* const kAppName    = "nbt-aad-app.log";
static const long        kMaxBytes   = 64L * 1024L * 1024L;    // bigger at open -> renamed to *.old.log

inline bool enabled() { return access(kFlag, F_OK) == 0 || access(kFlagManual, F_OK) == 0; }

inline bool isWritableDir(const char* p) {
    struct stat st;
    return stat(p, &st) == 0 && S_ISDIR(st.st_mode) && access(p, W_OK) == 0;
}

// First writable USB stick (/mnt/umass*), copied to out. root = "/mnt" (host tests pass their own directory).
inline bool findStick(char* out, size_t n, const char* root = "/mnt") {
    if (!out || n == 0) return false;
    out[0] = 0;
    DIR* d = opendir(root);
    if (!d) return false;
    bool ok = false;
    struct dirent* e;
    while ((e = readdir(d)) != NULL) {
        if (strncmp(e->d_name, "umass", 5) != 0) continue;
        snprintf(out, n, "%s/%s", root, e->d_name);
        if (isWritableDir(out)) { ok = true; break; }
    }
    closedir(d);
    if (!ok) out[0] = 0;
    return ok;
}

// <stick>/<name>; a file over kMaxBytes is first renamed to <stick>/<name without .log>.old.log (replacing it).
inline void logPath(char* out, size_t n, const char* stick, const char* name, long maxBytes = kMaxBytes) {
    snprintf(out, n, "%s/%s", stick, name);
    struct stat st;
    if (stat(out, &st) == 0 && (long)st.st_size > maxBytes) {
        char old[160];
        size_t l = strlen(name);
        if (l > 4 && strcmp(name + l - 4, ".log") == 0) snprintf(old, sizeof(old), "%s/%.*s.old.log", stick, (int)(l - 4), name);
        else snprintf(old, sizeof(old), "%s/%s.old", stick, name);
        unlink(old);
        rename(out, old);
    }
}

} // namespace usblog

// ---- RAM log (/dev/shmem, cleared at reboot), hard cap kMaxBytes per file ------------------------------------------
namespace ramlog {

static const char* const kDaemonPath = "/dev/shmem/nbt-aad.log";
static const char* const kAppPath    = "/dev/shmem/nbt-aad-app.log";
static const long        kMaxBytes   = 5L * 1024L * 1024L;     // 5 MB: never exceeded
static const long        kReserve    = 160;                    // room kept for the final "RAM log full" line
// The daemon logs with printf, so it checks the file size from its 250 ms main loop instead of per line: it stops
// once the file is within kDaemonHeadroom of the cap (far more than the daemon can print in 250 ms).
static const long        kDaemonHeadroom = 256L * 1024L;
static const char* const kFullLine   = "[Log] RAM log full (5 MB cap): no more lines are logged to RAM until the next start\n";

// Capped writer for line-at-a-time logs (the iDrive app). fresh: truncate on first use. A line that would take the
// file past maxBytes - kReserve is dropped, the "full" line is written once, and the log stays off (full = true).
struct Capped {
    long used;
    bool full;
    bool opened;
    Capped() : used(0), full(false), opened(false) {}
};

inline bool append(Capped& c, const char* path, const char* buf, size_t n, long maxBytes = kMaxBytes) {
    if (c.full || !path) return false;
    int flags = O_WRONLY | O_CREAT | (c.opened ? O_APPEND : O_TRUNC);
    bool over = c.used + (long)n > maxBytes - kReserve;
    int fd = open(path, flags, 0666);
    if (fd < 0) return false;
    c.opened = true;
    bool ok = false;
    if (over) {
        size_t fl = strlen(kFullLine);
        if (c.used + (long)fl <= maxBytes) { ssize_t w = write(fd, kFullLine, fl); if (w > 0) c.used += (long)w; }
        c.full = true;
    } else {
        ssize_t w = write(fd, buf, n);
        if (w > 0) { c.used += (long)w; ok = (size_t)w == n; }
    }
    close(fd);
    return ok;
}

// Daemon: true once a log of this size must stop (checked every main-loop tick).
inline bool daemonMustStop(long size, long maxBytes = kMaxBytes) { return size >= maxBytes - kDaemonHeadroom; }

} // namespace ramlog

// ---- head-unit software version (the value iDrive shows under Navigation > Settings > Position and software
// version > Software version, e.g. NBTevo_T21424I). Same file the factory navigation (apnnavc) and Connectivity
// (CarPlay FirmwareRev) read. First line, trimmed; "unknown" if missing.
inline void huVersion(char* out, size_t n, const char* path = NULL) {
    static const char* const kPaths[] = { "/opt/sys/etc/nbt_version.txt", "/etc/nbt_version.txt" };
    if (!out || n == 0) return;
    snprintf(out, n, "unknown");
    for (int i = 0; i < 2; ++i) {
        const char* p = path ? path : kPaths[i];
        FILE* f = fopen(p, "r");
        if (f) {
            char line[96];
            if (fgets(line, sizeof(line), f)) {
                size_t l = strlen(line);
                while (l && (line[l - 1] == '\n' || line[l - 1] == '\r' || line[l - 1] == ' ')) line[--l] = 0;
                if (l) { snprintf(out, n, "%s", line); fclose(f); return; }
            }
            fclose(f);
        }
        if (path) return;
    }
}

} // namespace nbt

#endif
