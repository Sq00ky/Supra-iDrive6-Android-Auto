#pragma once
#include <spawn.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#ifndef __QNXNTO__
extern char** environ;
#endif

namespace aa_cluster {
// QNX cannot fork a multithreaded daemon. Pass only stdin/out/err to children.
inline pid_t spawn_mapped(const char* path, char* const argv[], int input, int output, int error) {
#ifdef __QNXNTO__
    int descriptors[3] = {input, output, error};
    struct inheritance inherit;
    memset(&inherit, 0, sizeof(inherit));
    inherit.flags = SPAWN_SETSIGDEF | SPAWN_SETSIGMASK;
    // Do not inherit the daemon worker's blocked signals or signal handlers.
    sigfillset(&inherit.sigdefault);
    sigemptyset(&inherit.sigmask);
    return spawn(path, 3, descriptors, &inherit, argv, 0);
#else
    // Host regression runner; production always uses the QNX branch above.
    posix_spawn_file_actions_t files;
    posix_spawnattr_t attr;
    posix_spawn_file_actions_init(&files);
    posix_spawnattr_init(&attr);
    posix_spawn_file_actions_adddup2(&files, input, STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&files, output, STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&files, error, STDERR_FILENO);
    sigset_t defaults, mask;
    sigfillset(&defaults);
    sigemptyset(&mask);
    posix_spawnattr_setsigdefault(&attr, &defaults);
    posix_spawnattr_setsigmask(&attr, &mask);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETSIGMASK);
    pid_t pid = -1;
    int rc = posix_spawn(&pid, path, &files, &attr, argv, environ);
    posix_spawn_file_actions_destroy(&files);
    posix_spawnattr_destroy(&attr);
    if (rc) {
        errno = rc;
        return -1;
    }
    return pid;
#endif
}
} // namespace aa_cluster
