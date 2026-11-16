#pragma once
#include "process.hpp"
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

namespace aa_cluster {
// Start the owner process, optionally returning a pipe for both output streams.
// On success the caller owns *output and must drain/close it (ServiceLog does so).
// On failure all local descriptors are closed and spawn's errno is preserved.
inline pid_t spawn_service(const char* path, bool legacy_script = false, int* output = 0) {
    if (output) {
        *output = -1;
    }
    int input = open("/dev/null", O_RDONLY);
    if (input < 0) {
        return -1;
    }
    int log_pipe[2] = {-1, -1};
    if (output) {
        if (pipe(log_pipe)) {
            int saved_errno = errno;
            close(input);
            errno = saved_errno;
            return -1;
        }
        fcntl(log_pipe[0], F_SETFD, FD_CLOEXEC);
        fcntl(log_pipe[1], F_SETFD, FD_CLOEXEC);
    }
    char owner_pid[32];
    snprintf(owner_pid, sizeof(owner_pid), "%ld", (long)getpid());
    char* native_args[] = {const_cast<char*>(path), const_cast<char*>("--supervise"), owner_pid, 0};
    char* legacy_args[] = {const_cast<char*>("sh"), const_cast<char*>(path), owner_pid, 0};
    pid_t pid = spawn_mapped(
        legacy_script ? "/bin/sh" : path, legacy_script ? legacy_args : native_args, input,
        output ? log_pipe[1] : STDOUT_FILENO, output ? log_pipe[1] : STDERR_FILENO);
    int saved_errno = errno;
    close(input);
    if (output) {
        close(log_pipe[1]);
        if (pid < 0) {
            close(log_pipe[0]);
        } else {
            *output = log_pipe[0];
        }
    }
    errno = saved_errno;
    return pid;
}
} // namespace aa_cluster
