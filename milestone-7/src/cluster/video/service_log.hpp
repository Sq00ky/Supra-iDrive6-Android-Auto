#pragma once
#include "atomic.hpp"
#include <pthread.h>
#include <poll.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>

namespace aa_cluster {
// Drain independently of video decoding/RPC waits. Emit through the daemon's
// current stdout so RAM limits, USB logging and logging-off apply to both.
class ServiceLog {
    enum { MaxLineBytes = 2048, MaxShutdownBytes = 65536, PollIntervalMs = 100 };

    int input_fd_;
    pthread_t thread_;
    bool running_, line_truncated_;
    unsigned stop_requested_, line_size_;
    char line_[MaxLineBytes];
    void emit_line() {
        if (line_size_ || line_truncated_) {
            printf("[kombi-player] %.*s%s\n", int(line_size_), line_,
                   line_truncated_ ? " [line truncated]" : "");
            fflush(stdout);
        }
        line_size_ = 0;
        line_truncated_ = false;
    }
    void consume_bytes(const char* data, unsigned n) {
        for (unsigned i = 0; i < n; ++i) {
            if (data[i] == '\n') {
                emit_line();
            } else if (data[i] != '\r' && !line_truncated_) {
                if (line_size_ < sizeof(line_)) {
                    line_[line_size_++] = data[i];
                } else {
                    line_truncated_ = true;
                }
            }
        }
    }
    // Once stop is requested, drain only a bounded tail. A child retaining or
    // continuously writing the pipe cannot hold daemon shutdown indefinitely.
    void run() {
        unsigned shutdown_bytes = 0;
        for (;;) {
            char data[4096];
            ssize_t n = read(input_fd_, data, sizeof(data));
            if (n > 0) {
                consume_bytes(data, unsigned(n));
                if (atomic_load(&stop_requested_)) {
                    shutdown_bytes += unsigned(n);
                    if (shutdown_bytes >= MaxShutdownBytes) {
                        break;
                    }
                }
                continue;
            }
            if (!n) {
                break;
            }
            if (errno == EINTR) {
                continue;
            }
            if (errno != EAGAIN || atomic_load(&stop_requested_)) {
                break;
            }
            pollfd p = {input_fd_, POLLIN, 0};
            poll(&p, 1, PollIntervalMs);
        }
        emit_line();
        close(input_fd_);
    }
    static void* worker(void* self) {
        static_cast<ServiceLog*>(self)->run();
        return 0;
    }

public:
    ServiceLog()
        : input_fd_(-1), thread_(), running_(false), line_truncated_(false), stop_requested_(0),
          line_size_(0) {}
    ~ServiceLog() {
        finish();
    }
    // Takes ownership of input, including failure paths. The reader thread
    // closes it; finish() joins the thread before the object can be reused.
    bool start(int input) {
        finish();
        input_fd_ = input;
        line_size_ = 0;
        line_truncated_ = false;
        atomic_store(&stop_requested_, 0);
        if (input_fd_ < 0) {
            return false;
        }
        if (fcntl(input_fd_, F_SETFL, O_NONBLOCK) || pthread_create(&thread_, 0, worker, this)) {
            close(input_fd_);
            input_fd_ = -1;
            return false;
        }
        running_ = true;
        return true;
    }
    void finish() {
        if (running_) {
            atomic_store(&stop_requested_, 1);
            pthread_join(thread_, 0);
            running_ = false;
            input_fd_ = -1;
        }
    }

private:
    ServiceLog(const ServiceLog&);
    ServiceLog& operator=(const ServiceLog&);
};
} // namespace aa_cluster
