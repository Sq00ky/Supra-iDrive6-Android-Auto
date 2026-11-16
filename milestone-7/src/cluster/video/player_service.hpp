#pragma once
#include "service_spawn.hpp"
#include "service_log.hpp"
#include "recovery.hpp"
#include <sys/wait.h>
#include <signal.h>

namespace aa_cluster {

// Used only by the socket worker. The supervisor owns its child, alias and
// lease; a replacement may start only after that cleanup owner has exited.
class PlayerService {
    const char* path_;
    bool legacy_, external_, restarting_;
    pid_t pid_;
    ServiceLog logs_;
    Recovery recovery_;

public:
    PlayerService(const char* path, bool legacy = false, bool external = false)
        : path_(path), legacy_(legacy), external_(external), restarting_(false), pid_(-1) {}
    bool running() const { return pid_ > 0; }

    void stop() {
        if (pid_ > 0 && waitpid(pid_, 0, WNOHANG) == 0) {
            kill(pid_, SIGTERM);
            for (unsigned i = 0; i < 120; ++i) {
                if (waitpid(pid_, 0, WNOHANG) == pid_) {
                    pid_ = -1;
                    break;
                }
                usleep(100000);
            }
            if (pid_ > 0) {
                printf("[AA cluster] service cleanup still pending pid=%ld\n", (long)pid_);
                return; // Never kill the cleanup owner or launch a competitor.
            }
        }
        pid_ = -1;
        logs_.finish();
    }

    void disconnected() {
        if (external_ || restarting_ || pid_ <= 0) return;
        restarting_ = true;
        kill(pid_, SIGTERM); // Supervisor bounds child shutdown, then cleans.
        printf("[AA cluster] requesting player recovery after local transport failure\n");
    }

    bool tick(bool session_live, bool wanted, uint32_t now) {
        if (external_) return true;
        int status = 0;
        if (pid_ > 0 && waitpid(pid_, &status, WNOHANG) == pid_) {
            logs_.finish();
            const int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
            // 90..98 are supervisor preflight/ownership failures; 3 is failed
            // player cleanup. Neither should trigger repeated ownership grabs.
            const bool retry = code == 0 || code == 1 ||
                (restarting_ && (code == 130 || code == 137 || code == 143));
            const unsigned delay = recovery_.failed(now, retry);
            printf("[AA cluster] service ended pid=%ld exit=%d signal=%d retry_ms=%u blocked=%u\n",
                   (long)pid_, code, WIFSIGNALED(status) ? WTERMSIG(status) : 0,
                   delay, unsigned(!retry));
            pid_ = -1;
            restarting_ = false;
        }
        if (!session_live) {
            stop();
            recovery_ = Recovery();
            restarting_ = false;
            return false;
        }
        if (!wanted) {
            if (pid_ <= 0) recovery_ = Recovery();
            return pid_ > 0 && !restarting_;
        }
        if (pid_ > 0) {
            if (!restarting_) recovery_.healthy(now);
            return !restarting_;
        }
        if (!recovery_.ready(now)) return false;
        if (access(path_, legacy_ ? R_OK : X_OK)) {
            printf("[AA cluster] player/supervisor missing: %s\n", path_);
            recovery_.failed(now, false);
            return false;
        }
        int log_fd = -1;
        pid_ = spawn_service(path_, legacy_, &log_fd);
        if (pid_ < 0) {
            printf("[AA cluster] service spawn failed errno=%d: %s\n", errno, strerror(errno));
            recovery_.failed(now, true);
            return false;
        }
        if (!logs_.start(log_fd)) {
            printf("[AA cluster] kombi-player log relay failed; stopping helper\n");
            stop();
            recovery_.failed(now, false);
            return false;
        }
        recovery_.started(now);
        printf("[AA cluster] service start pid=%ld method=qnx_spawn\n", (long)pid_);
        return true;
    }
};
} // namespace aa_cluster
