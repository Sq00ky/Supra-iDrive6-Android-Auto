// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "a2i_stream.hpp"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>

namespace nbt {

namespace {

const unsigned kMaxPrimeBuffers = 64;      // sanity bound on the silence-prime loop
const uint32_t kLogEveryWrites  = 1000;    // ~5 s at 1 KiB buffers / 48 kHz stereo

void fillConfig(a2i_client_config_t* out, uint32_t direction, const A2IStreamConfig& cfg,
                const char* endpoint, uint32_t cookie) {
    memset(out, 0, sizeof(*out));
    out->magic            = 0x02000000;
    out->direction        = direction;
    out->field_08         = 1;
    out->mode             = 2;
    out->buffer_size      = cfg.buffer_size;
    out->channels         = cfg.channels;
    out->bytes_per_sample = 2;                         // 16-bit PCM
    out->sample_rate      = cfg.sample_rate;
    out->field_20         = 0;
    out->endpoint_name    = endpoint;
    out->channel_type     = (cfg.channels == 1) ? 0 : 1;
    out->field_2c         = 0;
    out->sample_bits      = A2I_BITS_16;
    out->field_34         = cookie;
    out->timeout_ms       = cfg.timeout_ms;
}

} // namespace


namespace {
struct OpenWatch { volatile int done; const char* ep; };
void* openWatchThread(void* arg) {
    OpenWatch* w = static_cast<OpenWatch*>(arg);
    int secs = 0;
    while (!w->done) {
        struct timespec ts; ts.tv_sec = 2; ts.tv_nsec = 0;
        nanosleep(&ts, NULL);
        if (w->done) break;
        secs += 2;
        printf("[A2I] Still blocked in A2I_Client_Open('%s') after %d s\n", w->ep, secs);
        fflush(stdout);
    }
    delete w;
    return NULL;
}
} // namespace

int a2iOpenWithFallback(uint32_t direction, const A2IStreamConfig& cfg,
                        a2i_client_handle_t* handle,
                        const char** used_endpoint, uint32_t* used_cookie) {
    const char* endpoints[2] = { cfg.endpoint, cfg.fallback_endpoint };
    uint32_t cookies[2]      = { cfg.cookie_primary, cfg.cookie_alternate };
    int rc = -1;

    for (int e = 0; e < 2; ++e) {
        if (!endpoints[e]) continue;
        for (int c = 0; c < 2; ++c) {
            if (cookies[c] == A2I_COOKIE_NONE) continue;
            a2i_client_config_t raw;
            fillConfig(&raw, direction, cfg, endpoints[e], cookies[c]);
            a2i_client_handle_t h = NULL;
            printf("[A2I] Calling A2I_Client_Open('%s', cookie 0x%08X) from tid %d\n",
                   endpoints[e], cookies[c], (int)pthread_self());
            fflush(stdout);
            OpenWatch* watch = new OpenWatch;
            watch->done = 0; watch->ep = endpoints[e];
            pthread_t wt; bool wtv = (pthread_create(&wt, NULL, openWatchThread, watch) == 0);
            if (wtv) pthread_detach(wt);
            rc = A2I_Client_Open(&h, &raw);
            if (wtv) watch->done = 1; else delete watch;
            printf("[A2I] A2I_Client_Open returned rc=%d\n", rc);
            fflush(stdout);
            if (rc == 0 && h) {
                *handle = h;
                if (used_endpoint) *used_endpoint = endpoints[e];
                if (used_cookie) *used_cookie = cookies[c];
                return 0;
            }
            printf("[A2I] Open '%s' (cookie 0x%08X, %u Hz x%u) failed rc=%d\n",
                   endpoints[e], cookies[c], cfg.sample_rate, cfg.channels, rc);
        }
    }
    return rc == 0 ? -1 : rc;
}

// ---------------------------------------------------------------------------
// Playback
// ---------------------------------------------------------------------------

A2IPlaybackStream::A2IPlaybackStream(const char* log_name, size_t ring_bytes)
    : name_(log_name),
      handle_(NULL),
      used_endpoint_(NULL),
      used_cookie_(0),
      ring_(ring_bytes),
      thread_(),
      thread_valid_(false),
      stop_(false),
      running_(false),
      feed_(false),
      writes_(0),
      primed_(0),
      underruns_(0),
      notify_fail_(0),
      getbuf_fail_(0),
      write_fail_(0) {
    memset(&cfg_, 0, sizeof(cfg_));
}

A2IPlaybackStream::~A2IPlaybackStream() {
    stop();
}

bool A2IPlaybackStream::open(const A2IStreamConfig& cfg) {
    if (handle_) return true;
    cfg_ = cfg;
    int rc = a2iOpenWithFallback(A2I_DIR_OUTPUT_DOWNLINK, cfg, &handle_, &used_endpoint_, &used_cookie_);
    if (rc != 0) {
        handle_ = NULL;
        return false;
    }
    printf("[%s] A2I playback open OK: %s (%u Hz x%u, buf=%u, cookie=0x%08X)\n",
           name_, used_endpoint_, cfg_.sample_rate, cfg_.channels, cfg_.buffer_size, used_cookie_);
    return true;
}

bool A2IPlaybackStream::start(bool feed) {
    if (!handle_ || thread_valid_) return false;
    feed_ = feed;
    stop_ = false;
    ring_.clear();
    writes_ = 0;
    primed_ = 0;
    underruns_ = 0;
    notify_fail_ = getbuf_fail_ = write_fail_ = 0;
    if (pthread_create(&thread_, NULL, &A2IPlaybackStream::threadStub, this) != 0) {
        printf("[%s] Failed to spawn playback worker\n", name_);
        return false;
    }
    thread_valid_ = true;
    return true;
}

void A2IPlaybackStream::stop() {
    if (thread_valid_) {
        stop_ = true;
        pthread_join(thread_, NULL);
        thread_valid_ = false;
    }
    running_ = false;
    if (handle_) {
        // Factory behaviour: playback streams are only Closed, never Stopped.
        int rc = A2I_Client_Close(&handle_);
        if (rc != 0) printf("[%s] A2I_Client_Close rc=%d\n", name_, rc);
        handle_ = NULL;
    }
}

void A2IPlaybackStream::write(const uint8_t* pcm, size_t len) {
    if (!pcm || len == 0 || !feed_) return;
    ring_.write(pcm, len);
}

void* A2IPlaybackStream::threadStub(void* arg) {
    reinterpret_cast<A2IPlaybackStream*>(arg)->workerLoop();
    return NULL;
}

void A2IPlaybackStream::workerLoop() {
    // 1. Prime every free A2I buffer with silence before Start.
    for (unsigned i = 0; i < kMaxPrimeBuffers && !stop_; ++i) {
        void* buf = NULL;
        size_t size = 0;
        int rc = A2I_Client_GetBuffer(handle_, &buf, &size);
        if (rc == A2I_ERR_NO_BUFFER) break;
        if (rc != 0 || !buf || size == 0) {
            printf("[%s] prime: GetBuffer rc=%d buf=%p size=%u - aborting worker\n",
                   name_, rc, buf, static_cast<unsigned>(size));
            return;
        }
        memset(buf, 0, size);
        rc = A2I_Client_Out_Write(handle_, buf, size, 0);
        if (rc != 0) {
            printf("[%s] prime: Out_Write failed (%s) - aborting worker\n",
                   name_, A2I_Client_GetErrorDescription(rc));
            return;
        }
        ++primed_;
    }

    // 2. Start only after the pipeline is full.
    // liba2i_client (decompiled, A2I_Client_Out_Write): with the legacy cookie 0x05555555 the library starts the
    // server stream ITSELF on the 2nd Out_Write (internal A2I_Client_Out_Write_start = devctl 0x8004111e
    // DCMD_A2I_SERV_START_STOP, state -> 4). Calling A2I_Client_Start afterwards sends a second START to
    // dev-ioamp-router. The factory CarPlay code uses cookie 0x0ABABABA (no auto-start) and calls Start once,
    // so only call Start when the library has not already done it.
    int rc = 0;
    const bool auto_started = (used_cookie_ == A2I_COOKIE_LEGACY_PLAYBACK) && (primed_ >= 2);
    if (!auto_started) {
        rc = A2I_Client_Start(handle_);
        if (rc != 0) {
            printf("[%s] A2I_Client_Start failed rc=%d - aborting worker\n", name_, rc);
            return;
        }
    }
    running_ = true;
    printf("[%s] %s after priming %u buffer(s); feed=%s\n", name_,
           auto_started ? "stream auto-started by the library (legacy cookie), no extra Start" : "A2I_Client_Start OK",
           static_cast<unsigned>(primed_), feed_ ? "AA PCM" : "silence");

    // 3. Steady state: wait for a free buffer, fill it, hand it back.
    while (!stop_) {
        if (A2I_Client_GetNotification(handle_) != 0) {
            ++notify_fail_;
            usleep(1000);
            continue;
        }

        void* buf = NULL;
        size_t size = 0;
        if (A2I_Client_GetBuffer(handle_, &buf, &size) != 0 || !buf || size == 0) {
            // Never spin: a notification without a free buffer would otherwise busy-loop this thread
            // (and every failed call can write a slog line inside the library).
            if ((getbuf_fail_++ % 500) == 0)
                printf("[%s] GetBuffer failed after a notification (%u so far)\n", name_, getbuf_fail_);
            usleep(2000);
            continue;
        }

        size_t got = feed_ ? ring_.read(static_cast<uint8_t*>(buf), size) : 0;
        if (got < size) {
            memset(static_cast<uint8_t*>(buf) + got, 0, size - got);
            if (feed_ && got == 0) ++underruns_;
        }

        if (A2I_Client_Out_Write(handle_, buf, size, 0) != 0) {
            ++write_fail_;
            continue;
        }
        ++writes_;

        if ((writes_ % kLogEveryWrites) == 0) {
            printf("[%s] writes=%u underruns=%u ring=%u/%u dropped=%llu notify_fail=%u getbuf_fail=%u write_fail=%u\n",
                   name_, static_cast<unsigned>(writes_), static_cast<unsigned>(underruns_),
                   static_cast<unsigned>(ring_.size()), static_cast<unsigned>(ring_.capacity()),
                   static_cast<unsigned long long>(ring_.droppedBytes()),
                   notify_fail_, getbuf_fail_, write_fail_);
        }
    }

    running_ = false;
}

// ---------------------------------------------------------------------------
// Capture
// ---------------------------------------------------------------------------

A2ICaptureStream::A2ICaptureStream(const char* log_name)
    : name_(log_name),
      handle_(NULL),
      used_endpoint_(NULL),
      used_cookie_(0),
      thread_(),
      thread_valid_(false),
      stop_(false),
      running_(false),
      started_(false),
      cb_(NULL),
      cb_user_(NULL),
      chunks_(0) {
    memset(&cfg_, 0, sizeof(cfg_));
}

A2ICaptureStream::~A2ICaptureStream() {
    stop();
}

bool A2ICaptureStream::open(const A2IStreamConfig& cfg) {
    if (handle_) return true;
    cfg_ = cfg;
    int rc = a2iOpenWithFallback(A2I_DIR_INPUT_UPLINK, cfg, &handle_, &used_endpoint_, &used_cookie_);
    if (rc != 0) {
        handle_ = NULL;
        return false;
    }
    printf("[%s] A2I capture open OK: %s (%u Hz x%u, buf=%u, cookie=0x%08X)\n",
           name_, used_endpoint_, cfg_.sample_rate, cfg_.channels, cfg_.buffer_size, used_cookie_);
    return true;
}

bool A2ICaptureStream::start(A2ICaptureCallback cb, void* user_data) {
    if (!handle_ || thread_valid_ || !cb) return false;
    cb_ = cb;
    cb_user_ = user_data;
    stop_ = false;
    started_ = false;
    chunks_ = 0;
    if (pthread_create(&thread_, NULL, &A2ICaptureStream::threadStub, this) != 0) {
        printf("[%s] Failed to spawn capture worker\n", name_);
        return false;
    }
    thread_valid_ = true;
    return true;
}

void A2ICaptureStream::stop() {
    if (thread_valid_) {
        stop_ = true;
        pthread_join(thread_, NULL);
        thread_valid_ = false;
    }
    running_ = false;
    if (handle_) {
        // Factory behaviour: capture streams are Stopped and then Closed.
        if (started_) {
            int rc = A2I_Client_Stop(handle_);
            if (rc != 0) printf("[%s] A2I_Client_Stop rc=%d\n", name_, rc);
        }
        int rc = A2I_Client_Close(&handle_);
        if (rc != 0) printf("[%s] A2I_Client_Close rc=%d\n", name_, rc);
        handle_ = NULL;
        started_ = false;
    }
}

void* A2ICaptureStream::threadStub(void* arg) {
    reinterpret_cast<A2ICaptureStream*>(arg)->workerLoop();
    return NULL;
}

void A2ICaptureStream::workerLoop() {
    int rc = A2I_Client_Start(handle_);
    if (rc != 0) {
        printf("[%s] A2I_Client_Start failed rc=%d - capture worker aborting\n", name_, rc);
        return;
    }
    started_ = true;
    running_ = true;
    printf("[%s] capture started\n", name_);

    uint32_t notify_fail = 0, getbuf_fail = 0;
    while (!stop_) {
        if (A2I_Client_GetNotification(handle_) != 0) {
            ++notify_fail;
            usleep(1000);
            continue;
        }
        void* buf = NULL;
        size_t size = 0;
        if (A2I_Client_GetBuffer(handle_, &buf, &size) != 0 || !buf || size == 0) {
            ++getbuf_fail;
            continue;
        }
        ++chunks_;
        cb_(static_cast<const uint8_t*>(buf), size, cb_user_);
    }

    printf("[%s] capture stopped (chunks=%u notify_fail=%u getbuf_fail=%u)\n",
           name_, static_cast<unsigned>(chunks_), notify_fail, getbuf_fail);
    running_ = false;
}

} // namespace nbt
