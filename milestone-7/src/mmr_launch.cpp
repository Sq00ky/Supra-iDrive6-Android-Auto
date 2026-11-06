// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// QNX side of mmr_launch.hpp: spawning, finding and stopping /opt/conn/bin/carplay/mm-renderer.
#include "mmr_launch.hpp"

#include <dirent.h>
#include <devctl.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sched.h>
#include <signal.h>
#include <spawn.h>
#include <sys/procfs.h>
#include <sys/wait.h>
#include <unistd.h>

namespace nbt {
namespace mmr {

namespace {

// Connectivity.cfg environment entries 21-24 (process 18 needs no LD_PRELOAD).
const char* const kEnv[] = {
    "LD_LIBRARY_PATH=/opt/conn/lib/carplay:/opt/conn/lib/carplay/mmedia:/boot/lib:/lib:/usr/lib:/opt/os/lib:"
        "/opt/sys/lib:/opt/core/lib:/opt/car/lib:/lib/graphics/omap5430",
    "MM_RENDERER_CONFIG=/opt/conn/etc/carplay/mm-renderer.conf",
    "MM_PLAYLIST_CONFIG=/opt/conn/etc/carplay/mm-playlist.conf",
    "MM_INIT=/opt/conn/lib/carplay/mm",
};
const int kPrio = 10;   // Connectivity.cfg <Prio>

// Executable path of a process: /proc/<pid>/exefile, else the as-loaded path pidin shows (DCMD_PROC_MAPDEBUG_BASE).
bool exePath(int pid, char* buf, size_t len) {
    if (readExeFile("/proc", pid, buf, len)) return true;
    char p[48];
    snprintf(p, sizeof(p), "/proc/%d/as", pid);
    int fd = open(p, O_RDONLY);
    if (fd < 0) return false;
    struct {
        procfs_debuginfo info;
        char buff[PATH_MAX];
    } map;
    memset(&map, 0, sizeof(map));
    bool ok = devctl(fd, DCMD_PROC_MAPDEBUG_BASE, &map, sizeof(map), 0) == EOK;
    close(fd);
    if (!ok) return false;
    snprintf(buf, len, "%s", map.info.path);
    return buf[0] != 0;
}

} // namespace

bool Launcher::pidIsRenderer(int pid) {
    char path[PATH_MAX];
    return exePath(pid, path, sizeof(path)) && exePathIsRenderer(path);
}

namespace {
// First process in /proc whose executable path matches (0 = none).
int findProcess(bool (*match)(const char*)) {
    DIR* d = opendir("/proc");
    if (!d) return 0;
    int found = 0;
    struct dirent* de;
    char path[PATH_MAX];
    while (!found && (de = readdir(d)) != NULL) {
        char* end = NULL;
        long pid = strtol(de->d_name, &end, 10);
        if (pid <= 0 || !end || *end) continue;
        if (exePath((int)pid, path, sizeof(path)) && match(path)) found = (int)pid;
    }
    closedir(d);
    return found;
}
} // namespace

int Launcher::findRunning() { return findProcess(&exePathIsRenderer); }

int Launcher::findCarPlayHelper() { return findProcess(&exePathIsCarPlayHelper); }

void Launcher::forget() {
    pid_ = 0;
    child_ = false;
    unlink(kMarker);
}

bool Launcher::adopt() {
    if (owns()) return true;
    int pid = markerRead(kMarker);
    if (!pid) return false;
    if (!pidIsRenderer(pid)) {
        printf("[MMR] Marker %s names pid %d, which is no longer mm-renderer: removed.\n", kMarker, pid);
        unlink(kMarker);
        return false;
    }
    pid_ = pid;
    child_ = false;
    printf("[MMR] Adopted mm-renderer pid %d that an earlier nbt-aad started: stopped when nbt-aad exits.\n", pid);
    return true;
}

bool Launcher::start() {
    if (owns()) return true;
    int null_fd = open("/dev/null", O_RDWR);
    if (null_fd < 0) {
        printf("[MMR] Cannot open /dev/null (errno=%d): mm-renderer not started.\n", errno);
        return false;
    }
    // The child gets fds 0-2 on /dev/null and nothing else: not our USB (/dev/oap*), TCP, PPS or mdev handles.
    int fd_map[3] = { null_fd, null_fd, null_fd };

    struct inheritance inh;
    memset(&inh, 0, sizeof(inh));
    // Own session: no SIGHUP/SIGINT from the shell that started nbt-aad. Every signal back to SIG_DFL (we ignore
    // SIGPIPE), none blocked. Priority as HMood gives it.
    inh.flags = SPAWN_SETSID | SPAWN_SETSIGDEF | SPAWN_SETSIGMASK | SPAWN_EXPLICIT_SCHED;
    sigfillset(&inh.sigdefault);
    sigemptyset(&inh.sigmask);
    inh.policy = SCHED_RR;
    inh.param.sched_priority = kPrio;

    // Factory args (Connectivity.cfg process 18) plus -f: stay in the foreground, so this pid IS the server.
    char* const argv[] = {
        (char*)kRendererPath, (char*)"-f", (char*)"-vvvvvv", (char*)"-r", (char*)"/pps/services/multimedia/renderer", NULL
    };
    char path_env[512];
    const char* path = getenv("PATH");
    snprintf(path_env, sizeof(path_env), "PATH=%s", (path && *path) ? path : "/proc/boot:/bin:/usr/bin:/sbin:/usr/sbin");
    char* const envp[] = { (char*)kEnv[0], (char*)kEnv[1], (char*)kEnv[2], (char*)kEnv[3], path_env, NULL };

    pid_t pid = spawn(kRendererPath, 3, fd_map, &inh, argv, envp);
    int err = errno;
    close(null_fd);
    if (pid < 0) {
        printf("[MMR] spawn(%s) failed (errno=%d: %s).\n", kRendererPath, err, strerror(err));
        return false;
    }
    pid_ = pid;
    child_ = true;
    if (!markerWrite(kMarker, pid))
        printf("[MMR] Warning: cannot write %s (errno=%d): a later nbt-aad will not know it may stop pid %d.\n",
               kMarker, errno, pid);
    printf("[MMR] Started %s -f -vvvvvv -r /pps/services/multimedia/renderer: pid %d.\n", kRendererPath, pid);
    return true;
}

bool Launcher::alive() {
    if (!owns()) return false;
    if (child_) {
        int st = 0;
        pid_t r = waitpid(pid_, &st, WNOHANG);
        if (r == pid_) {
            if (WIFEXITED(st)) printf("[MMR] mm-renderer pid %d exited with status %d.\n", pid_, WEXITSTATUS(st));
            else if (WIFSIGNALED(st)) printf("[MMR] mm-renderer pid %d killed by signal %d.\n", pid_, WTERMSIG(st));
            else printf("[MMR] mm-renderer pid %d ended.\n", pid_);
            forget();
            return false;
        }
        if (r == 0) return true;
        // ECHILD: reaped elsewhere. Fall through to the /proc check.
    }
    if (pidIsRenderer(pid_)) return true;
    printf("[MMR] mm-renderer pid %d is gone.\n", pid_);
    forget();
    return false;
}

void Launcher::stop() {
    if (!alive()) return;   // none owned, or already gone (logged, marker removed)
    int pid = pid_;
    // An adopted pid could in theory have been reused: alive() just checked it is still mm-renderer.
    printf("[MMR] Stopping mm-renderer pid %d (we started it).\n", pid);
    kill(pid, SIGTERM);
    for (int i = 0; i < 10; ++i) {   // up to 500 ms: the iDrive app SIGKILLs nbt-aad 10 s after 'quit'
        usleep(50000);
        if (!alive()) { printf("[MMR] mm-renderer pid %d stopped.\n", pid); return; }
    }
    printf("[MMR] mm-renderer pid %d ignored SIGTERM: SIGKILL.\n", pid);
    kill(pid, SIGKILL);
    for (int i = 0; i < 10 && alive(); ++i) usleep(20000);
    if (owns()) {   // still not reaped/gone: never leave a marker that could name a reused pid later
        printf("[MMR] mm-renderer pid %d still listed after SIGKILL.\n", pid);
        forget();
    }
}

} // namespace mmr
} // namespace nbt
