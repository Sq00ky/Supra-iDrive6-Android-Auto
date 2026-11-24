// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#pragma once
// Follows the DAB and FM tuners and the audible source on the observe-only MOST client; sends nothing.
#include "radio/radio_state.hpp"
#include "radio/wall_time.hpp"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string>

namespace nbt {

namespace most {
class Client;
}

class RadioTap {
public:
    typedef void (*ChangeCb)(void* user);   // DSI thread, must not block

    RadioTap() : cb_(NULL), user_(NULL) { pthread_mutex_init(&mu_, NULL); }
    ~RadioTap() { pthread_mutex_destroy(&mu_); }

    // Before the client's start(). False: a route could not be added.
    bool attach(most::Client& c, ChangeCb cb, void* user);

    void setCallback(ChangeCb cb, void* user) {
        pthread_mutex_lock(&mu_);
        cb_ = cb;
        user_ = user;
        pthread_mutex_unlock(&mu_);
    }

    // nowMs: CLOCK_MONOTONIC ms, the session's clock. On a visible change one log line, then the callback.
    void apply(uint8_t fblock, uint16_t fkt, const uint8_t* p, size_t n, uint32_t nowMs) {
        char line[512];
        pthread_mutex_lock(&mu_);
        const bool ch = applyMost(state_, fblock, fkt, p, n, nowMs);
        if (ch) describe(fblock, line, sizeof(line));
        const ChangeCb cb = cb_;
        void* const user = user_;
        pthread_mutex_unlock(&mu_);
        if (!ch) return;
        fputs(line, stdout);
        if (cb) cb(user);
    }

    RadioState snapshot() {
        pthread_mutex_lock(&mu_);
        RadioState s = state_;
        pthread_mutex_unlock(&mu_);
        return s;
    }

private:
    RadioTap(const RadioTap&);
    RadioTap& operator=(const RadioTap&);

    // About the first 60 bytes of s, not cutting a UTF-8 sequence.
    static int logTextLen(const std::string& s) {
        size_t n = 60;
        if (s.size() <= n) return (int)s.size();
        while (n > 0 && ((uint8_t)s[n] & 0xC0) == 0x80) --n;
        return (int)n;
    }

    // Under mu_: the content of the tuner that sent, for C03 of the audible one.
    void describe(uint8_t fblock, char* line, size_t size) const {
        const int kind = fblock == kMostFBlockDabTuner ? kTunerDab
                         : fblock == kMostFBlockFmTuner ? kTunerFm : state_.tuner;
        const TunerContent none;
        const TunerContent& t = kind == kTunerDab ? state_.dab : (kind == kTunerFm ? state_.fm : none);
        const int k = snprintf(line, size, "[RadioTap] %s tuner %s audible %d name \"%s\" title \"%s\" artist \"%s\" "
                               "text \"%.*s\" slide %u logo %u\n", WallTime().hms,
                               kind == kTunerDab ? "dab" : (kind == kTunerFm ? "fm" : "?"), state_.audible,
                               t.name.c_str(), t.title.c_str(), t.artist.c_str(), logTextLen(t.text), t.text.c_str(),
                               (unsigned)t.slide.size(), (unsigned)t.logo.size());
        if (k < 0 || k >= (int)size) {   // cut: keep the line end
            line[size - 2] = '\n';
            line[size - 1] = '\0';
        }
    }

    pthread_mutex_t mu_;
    RadioState state_;
    ChangeCb cb_;
    void* user_;
};

}  // namespace nbt
