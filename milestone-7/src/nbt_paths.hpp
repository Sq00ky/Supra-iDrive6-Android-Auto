// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef NBT_PATHS_HPP
#define NBT_PATHS_HPP

// Where NBT-AAD keeps its files. Shared by the daemon and the iDrive app (header-only, C library only: the daemon is
// built with QNX's Dinkumware C++ library, the app with GNU libstdc++; the app includes it through usb_log.hpp).
//
// One build serves two installs, decided once at start:
//
//              hand install   package install (an NBTApps agent puts the files there)
//   app dir    /opt/aa        /var/opt/nbtapp/apps/com.github.sq00ky.android-auto   nbt-aad, flag files made by hand
//   data dir   /var/opt/aa    /var/opt/nbtapp/data/com.github.sq00ky.android-auto   what the checkboxes write
//   conf dir   /opt/aa        the data dir                                         nbt-aad.env, pf.bak
//
// The conf dir holds the two files under /opt/aa that the app itself writes (pf_aa adds NBT_KEEP_PF to nbt-aad.env and
// keeps the stock pf.conf in pf.bak); in a package the app dir is the agent's, so they go with the settings.
//
// Daemon: initPaths(argv[0]) first thing in main(). The iDrive app starts it by its whole path, so a daemon inside the
// package's app dir is a package install; anything else (/opt/aa/nbt-aad, ./nbt-aad from a shell) is a hand install.
// iDrive app: initPathsForApp() from the library's load-time constructor. Both installs put the plugin in the same
// Plugins folder, so the app goes by the daemon: an executable nbt-aad in the package's app dir = package install.
// Until either runs (host tests, tools) every path is the hand install's.
//
// The package's folders are written out whole below, so each program carries them as one string: the packaging job
// recognises a build that knows them by finding both in nbt-aad and in libNbtAad.so.

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define NBT_PKG_ID        "com.github.sq00ky.android-auto"
#define NBT_PKG_APP_DIR   "/var/opt/nbtapp/apps/" NBT_PKG_ID
#define NBT_PKG_DATA_DIR  "/var/opt/nbtapp/data/" NBT_PKG_ID
#define NBT_HAND_APP_DIR  "/opt/aa"
#define NBT_HAND_DATA_DIR "/var/opt/aa"
#define NBT_DAEMON_NAME   "nbt-aad"

namespace nbt {

static const char* const kPackageId      = NBT_PKG_ID;
static const char* const kPackageAppDir  = NBT_PKG_APP_DIR;
static const char* const kPackageDataDir = NBT_PKG_DATA_DIR;
static const char* const kHandAppDir     = NBT_HAND_APP_DIR;
static const char* const kHandDataDir    = NBT_HAND_DATA_DIR;
static const char* const kDaemonEnvName  = "nbt-aad.env";   // in the conf dir
static const char* const kPfBackupName   = "pf.bak";        // in the conf dir

// The decision: 0 = hand install (also before any decision), 1 = package install. A plain int with a constant
// initialiser, so it is set before any constructor runs, and one object for the whole program or library.
inline int& pathsPackagedFlag() {
    static int s = 0;
    return s;
}
inline void setPackaged(bool on) { pathsPackagedFlag() = on ? 1 : 0; }
inline bool packaged() { return pathsPackagedFlag() == 1; }

// Pure (host-tested): whether a program path lies inside the package's app dir. Only the whole absolute path can say
// so: a daemon started by hand as ./nbt-aad counts as a hand install.
inline bool pathIsPackaged(const char* program) {
    static const char kDir[] = NBT_PKG_APP_DIR;
    const size_t n = sizeof(kDir) - 1;
    return program && strncmp(program, kDir, n) == 0 && program[n] == '/' && program[n + 1] != 0;
}

// An executable nbt-aad in `dir` (the package's app dir for the iDrive app; host tests pass their own).
inline bool daemonExecutableIn(const char* dir) {
    char p[160];
    snprintf(p, sizeof(p), "%s/%s", dir, NBT_DAEMON_NAME);
    struct stat st;
    return stat(p, &st) == 0 && S_ISREG(st.st_mode) && access(p, X_OK) == 0;
}

// Daemon: from its own argv[0], before anything reads a setting.
inline void initPaths(const char* argv0) { setPackaged(pathIsPackaged(argv0)); }
// iDrive app: from the daemon it would start.
inline void initPathsForApp() { setPackaged(daemonExecutableIn(NBT_PKG_APP_DIR)); }

inline const char* appDir() { return packaged() ? NBT_PKG_APP_DIR : NBT_HAND_APP_DIR; }
inline const char* dataDir() { return packaged() ? NBT_PKG_DATA_DIR : NBT_HAND_DATA_DIR; }
inline const char* confDir() { return packaged() ? NBT_PKG_DATA_DIR : NBT_HAND_APP_DIR; }
// The daemon's whole path (a literal, so it can stand in a spawn argv for as long as needed).
inline const char* daemonPath() {
    return packaged() ? NBT_PKG_APP_DIR "/" NBT_DAEMON_NAME : NBT_HAND_APP_DIR "/" NBT_DAEMON_NAME;
}
inline const char* installName() { return packaged() ? "package install" : "hand install"; }

// <dir>/<name> in a buffer of its own, for one call: fopen(nbt::dataFile("spp-model").c_str(), "r"). Use c_str()
// for printf as well: the object itself must not go through "...".
class FilePath {
public:
    FilePath(const char* dir, const char* name) { snprintf(buf_, sizeof(buf_), "%s/%s", dir, name); }
    const char* c_str() const { return buf_; }
private:
    char buf_[160];
};
inline FilePath appFile(const char* name) { return FilePath(appDir(), name); }
inline FilePath dataFile(const char* name) { return FilePath(dataDir(), name); }
inline FilePath confFile(const char* name) { return FilePath(confDir(), name); }

// A checkbox setting: on while <data dir>/<name> (the checkbox's own file) or <app dir>/<name> (made by hand) exists.
inline bool flagSet(const char* name) {
    return access(dataFile(name).c_str(), F_OK) == 0 || access(appFile(name).c_str(), F_OK) == 0;
}

// Make the data dir before writing into it (EEXIST is fine). For a package its parents too, in case the agent has not
// made them yet; a hand install only ever made /var/opt/aa itself.
inline void makeDataDir() {
    if (packaged()) {
        mkdir("/var/opt/nbtapp", 0755);
        mkdir("/var/opt/nbtapp/data", 0755);
    }
    mkdir(dataDir(), 0755);
}

} // namespace nbt

#endif // NBT_PATHS_HPP
