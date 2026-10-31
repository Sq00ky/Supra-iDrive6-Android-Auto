// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef NBT_USB_LOG_HPP
#define NBT_USB_LOG_HPP

// "Save logs to USB" (iDrive app checkbox, v1.8.23 / rhmiapp-10). Shared by the daemon and the iDrive app
// (header-only, the app includes it as "../src/usb_log.hpp").
//
// Off (default): nothing is logged to a file; nothing is kept in RAM (/dev/shmem). The app still writes slog.
// On: the flag file exists; the daemon and the app look for a writable USB stick under /mnt (iDrive mounts sticks
// as /mnt/umass<...>) and append to <stick>/nbt-aad.log and <stick>/nbt-aad-app.log. A stick plugged in later is
// picked up within a few seconds; a removed stick stops the file logging until one is back.
// NBT_LOG_FILE=<path> in nbt-aad.env still forces a daemon log file regardless of the checkbox.

#include <dirent.h>
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
} // namespace nbt

#endif
