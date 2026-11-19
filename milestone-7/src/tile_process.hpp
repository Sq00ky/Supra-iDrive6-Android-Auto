// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef NBT_TILE_PROCESS_HPP
#define NBT_TILE_PROCESS_HPP

#include <spawn.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>

extern char** environ;
namespace nbt {
// Owns only the child it spawned. No factory boot hooks, shell commands or respawn loops.
// Call on the daemon's main thread after writing its connection-state record.
class TileProcess {
public:
    TileProcess() : pid_(-1), attempted_(false), inputSerial_(0) {}
    pid_t pid() const { return pid_; }
    bool attempted() const { return attempted_; }
    void service(bool connected, const char* path, const char* logPath, unsigned inputSerial=0) {
        if (pid_ > 0) {
            int status;
            pid_t result = waitpid(pid_, &status, WNOHANG);
            if (result == pid_ || (result < 0 && errno == ECHILD)) {
                printf("[TileProcess] helper ended; waiting for a new input before retry\n");
                pid_ = -1;
            }
        }
        if(inputSerial && inputSerial!=inputSerial_){
            inputSerial_=inputSerial;
            if(pid_<=0)attempted_=false; // Once per new touch, never a timer respawn loop.
        }
        if (!connected || attempted_) return;
        attempted_ = true;
        if (!path || !*path || access(path, X_OK) != 0) {
            printf("[TileProcess] helper absent/disabled; Android Auto continues normally\n");
            return;
        }
        int input = open("/dev/null", O_RDONLY);
        int output = open(logPath, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (input < 0 || output < 0) {
            if (input >= 0) close(input);
            if (output >= 0) close(output);
            printf("[TileProcess] cannot open helper log\n");
            return;
        }
        posix_spawn_file_actions_t actions;
        int error = posix_spawn_file_actions_init(&actions);
        if (!error) {
            error = posix_spawn_file_actions_adddup2(&actions, input, 0);
            if (!error) error = posix_spawn_file_actions_adddup2(&actions, output, 1);
            if (!error) error = posix_spawn_file_actions_adddup2(&actions, output, 2);
            char owner[24]; snprintf(owner, sizeof(owner), "%d", (int)getpid());
            char* args[] = { const_cast<char*>(path), const_cast<char*>("follow"),
                             const_cast<char*>("0"), owner, NULL };
            pid_t child = -1;
            if (!error) error = posix_spawn(&child, path, &actions, NULL, args, environ);
            posix_spawn_file_actions_destroy(&actions);
            if (!error) pid_ = child;
        }
        close(input); close(output);
        if (error) printf("[TileProcess] launch failed (%d); Android Auto continues normally\n", error);
        else printf("[TileProcess] helper pid %d: follow mode, no time limit\n", (int)pid_);
    }
    void stop() {
        if (pid_ <= 0) return;
        // Reap first: never signal a PID after a child has already been collected elsewhere.
        int status;
        pid_t result = waitpid(pid_, &status, WNOHANG);
        if (result == 0) {
            kill(pid_, SIGTERM);
            for (int i = 0; i < 15; ++i) {
                result = waitpid(pid_, &status, WNOHANG);
                if (result != 0) break;
                usleep(50000);
            }
            if (result == 0) {
                kill(pid_, SIGKILL);
                while (waitpid(pid_, &status, 0) < 0 && errno == EINTR) {}
            }
        }
        pid_ = -1;
    }
private:
    TileProcess(const TileProcess&);
    TileProcess& operator=(const TileProcess&);
    pid_t pid_;
    bool attempted_;
    unsigned inputSerial_;
};
}
#endif
