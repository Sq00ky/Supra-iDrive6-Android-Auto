// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef MMR_LAUNCH_HPP
#define MMR_LAUNCH_HPP

// Starting the factory mm-renderer when nobody else did, and stopping it again on exit.
//
// mm-renderer (/opt/conn/bin/carplay/mm-renderer) is the hardware video decoder service. HMood starts it only as
// part of the CONN_CARPLAY package (Connectivity.cfg process 18, env entries 21-24), so on a car without CarPlay
// enabled it never runs: mmr_connect() fails (ESRCH, or ENXIO once an earlier renderer left its PPS object) and
// Android Auto stays black. VideoManager::init() then starts one itself, but only on a car WITHOUT CarPlay coded (no
// carplay.ini, carPlayCoded()) and only if no mm-renderer process exists: there HMood never starts one. On a coded
// car HMood owns it and we only wait (CarPlay and Android Auto on the same car is not a case we support).
// It uses the factory environment and arguments plus -f (stay in the foreground: without it mm-renderer calls
// procmgr_daemon() and the pid spawn() returns is gone at once, so we would not know whom to stop).
//
// The pid of a renderer WE started is kept in kMarker. VideoManager::stop() stops that one (and only that one).
// If the daemon dies without clean-up (crash, SIGKILL 10 s after 'quit') the renderer keeps running; the next
// daemon finds the marker, adopts the renderer and stops it on its own exit. /dev/shmem is wiped at reboot, so a
// marker never outlives the boot it was written in.
//
// NBT_MMR_SPAWN=0: never start it.
//
// This header is host-testable (tests/test_mmr_launch.cpp); mmr_launch.cpp holds the QNX parts.

#include "car_config.hpp"   // kCarPlayIniPaths

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

namespace nbt {
namespace mmr {

// True if the car has CarPlay coded. Connectivity's CConnOnOffDipoClient ("Dipo" = CarPlay) runs only on the
// CarPlay-coded path (IsCarPlayCoded / SetCodingDipo / CheckFileSystemCarPlay) and creates carplay.ini from the coding
// ("#Coded is: def,1280_480,touch_screen"); it lives on /var, so it is there from boot on every later start. Not the
// NBT_CARPLAY_INI override: that one is for testing the screen size, not a statement about the car.
inline bool carPlayCoded(const char* const* paths = kCarPlayIniPaths) {
    for (int i = 0; paths && paths[i]; ++i) {
        FILE* f = fopen(paths[i], "r");
        if (f) { fclose(f); return true; }
    }
    return false;
}

static const char* const kRendererPath = "/opt/conn/bin/carplay/mm-renderer";
static const char* const kMarker = "/dev/shmem/nbt-aad-mmr.pid";

// True if an executable path (as /proc/<pid>/exefile or procfs_debuginfo reports it, trailing newline allowed)
// is mm-renderer. Matched on the tail so /fs/sda0/repository/istep/opt/conn/... counts too.
inline bool exePathIsRenderer(const char* path) {
    if (!path) return false;
    static const char kTail[] = "/conn/bin/carplay/mm-renderer";
    size_t n = strlen(path);
    while (n > 0 && (path[n - 1] == '\n' || path[n - 1] == '\r' || path[n - 1] == ' ')) --n;
    size_t t = sizeof(kTail) - 1;
    return n >= t && strncmp(path + n - t, kTail, t) == 0;
}

// Reads <proc_root>/<pid>/exefile into buf. False if the process (or the file) does not exist.
inline bool readExeFile(const char* proc_root, int pid, char* buf, size_t len) {
    if (pid <= 0 || len == 0) return false;
    char p[96];
    snprintf(p, sizeof(p), "%s/%d/exefile", proc_root, pid);
    FILE* f = fopen(p, "r");
    if (!f) return false;
    size_t n = fread(buf, 1, len - 1, f);
    fclose(f);
    buf[n] = 0;
    return n > 0;
}

// The marker holds one decimal pid. 0 = no (valid) marker.
inline int markerRead(const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) return 0;
    int pid = 0;
    if (fscanf(f, "%d", &pid) != 1 || pid <= 0) pid = 0;
    fclose(f);
    return pid;
}

inline bool markerWrite(const char* path, int pid) {
    FILE* f = fopen(path, "w");
    if (!f) return false;
    bool ok = fprintf(f, "%d\n", pid) > 0;
    return fclose(f) == 0 && ok;
}

// The mm-renderer this daemon owns: one it started, or one an earlier run of the daemon started (marker).
class Launcher {
public:
    Launcher() : pid_(0), child_(false) {}

    // Takes over the renderer an earlier daemon run started, if the marker names a running mm-renderer.
    // A stale marker is removed. Returns true if we now own one.
    bool adopt();
    // Starts mm-renderer with the factory environment (-f, see above). Returns false if spawn() failed.
    bool start();
    // False once the renderer we own has exited (our child: reaped, exit status logged; marker removed).
    bool alive();
    // Stops the renderer we own: SIGTERM, up to 500 ms, then SIGKILL. Removes the marker. No-op if we own none.
    void stop();

    bool owns() const { return pid_ > 0; }
    int pid() const { return pid_; }

    // First running mm-renderer in /proc (0 = none).
    static int findRunning();
    static bool pidIsRenderer(int pid);

private:
    void forget();
    pid_t pid_;
    bool child_;   // spawned by this process (waitpid() works) rather than adopted
};

} // namespace mmr
} // namespace nbt

#endif // MMR_LAUNCH_HPP
