#pragma once
#include "hardware_session.hpp"
#include <errno.h>
#include <time.h>

namespace aa_cluster { namespace hardware {

// Input and capture have separate owners. All encoded reference frames go
// straight to mdev on the caller's thread. One worker owns Screen scaling and
// readback/packing, publishing only the newest complete picture in bounded RAM.
template<class Api> class AsyncSession {
    typedef nbt::cluster::VideoProfile P;
    enum { PictureBytes = P::ContentWidth * P::ContentHeight * 4 };
    Session<Api> session_;
    pthread_mutex_t mutex_;
    pthread_cond_t wake_;
    pthread_t thread_;
    bool mutex_ready_, condition_ready_, started_, stopping_, failed_, pending_;
    uint8_t *capture_, *latest_;

    AsyncSession(const AsyncSession&);
    AsyncSession& operator=(const AsyncSession&);

    static void* entry(void* self) {
        static_cast<AsyncSession*>(self)->run();
        return 0;
    }
    void run() {
        pthread_mutex_lock(&mutex_);
        while (!stopping_) {
            pthread_mutex_unlock(&mutex_);
            // No mailbox/input lock is held during potentially slow work.
            const int result = session_.poll(capture_);
            pthread_mutex_lock(&mutex_);
            if (stopping_) break;
            if (result < 0) { failed_ = true; break; }
            if (result > 0) {
                uint8_t* previous = latest_;
                latest_ = capture_; capture_ = previous;
                pending_ = true;
            }
            // Wake promptly on close; idle maps still get asynchronous output
            // without needing another encoded packet. Session caps readbacks.
            timespec until = {};
            if (clock_gettime(CLOCK_REALTIME, &until)) { failed_ = true; break; }
            until.tv_nsec += 20000000;
            if (until.tv_nsec >= 1000000000) { ++until.tv_sec; until.tv_nsec -= 1000000000; }
            while (!stopping_) {
                int result = pthread_cond_timedwait(&wake_, &mutex_, &until);
                if (result == ETIMEDOUT) break;
                if (result) { failed_ = true; stopping_ = true; }
            }
        }
        pthread_mutex_unlock(&mutex_);
    }

public:
    explicit AsyncSession(Api& api) : session_(api),
        mutex_ready_(pthread_mutex_init(&mutex_, 0) == 0),
        condition_ready_(pthread_cond_init(&wake_, 0) == 0),
        started_(false), stopping_(false), failed_(false), pending_(false),
        capture_(0), latest_(0) {}
    ~AsyncSession() {
        close();
        if (condition_ready_) pthread_cond_destroy(&wake_);
        if (mutex_ready_) pthread_mutex_destroy(&mutex_);
    }
    bool open() {
        close();
        if (!mutex_ready_ || !condition_ready_ || !session_.open()) { close(); return false; }
        capture_ = static_cast<uint8_t*>(malloc(PictureBytes));
        latest_ = static_cast<uint8_t*>(malloc(PictureBytes));
        stopping_ = failed_ = pending_ = false;
        if (!capture_ || !latest_ || pthread_create(&thread_, 0, entry, this)) {
            close(); return false;
        }
        started_ = true;
        printf("AA_HW_CAPTURE_WORKER started=1 latest_picture_slots=1\n");
        return true;
    }
    void close() {
        if (started_) {
            pthread_mutex_lock(&mutex_);
            stopping_ = true;
            pthread_cond_signal(&wake_);
            pthread_mutex_unlock(&mutex_);
            // The native readback has its own bounded QNX timeout. Never
            // destroy its Screen/renderer objects while it is still using them.
            pthread_join(thread_, 0);
            started_ = false;
        }
        session_.close();
        free(capture_); free(latest_); capture_ = latest_ = 0;
        pending_ = false;
    }
    int decode(const uint8_t* bytes, unsigned size, uint64_t pts, uint8_t* pixels, bool present) {
        return started_ ? session_.decode(bytes, size, pts, pixels, present) : -1;
    }
    int poll(uint8_t* pixels, bool present = true) {
        if (!started_) return 0;
        // Incoming packet assembly must never wait for a capture. The worker
        // holds this lock only to swap pointers/status, never for readback.
        if (pthread_mutex_trylock(&mutex_)) return 0;
        int result = failed_ ? -1 : 0;
        if (!failed_ && pending_ && present) {
            memcpy(pixels, latest_, PictureBytes);
            pending_ = false;
            result = 1;
        }
        pthread_mutex_unlock(&mutex_);
        return result;
    }
    unsigned unchanged() const { return session_.unchanged(); }
    unsigned coalesced() const { return session_.coalesced(); }
};
} } // namespace aa_cluster::hardware
