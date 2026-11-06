// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "audio/audio_acks.hpp"
#include "nbt_env.hpp"
#include <stdio.h>
#include <sys/time.h>
#include <time.h>

namespace nbt {

// The ledgers and counters are indexed by AudioStream (audio/channels.hpp); lines that list the streams walk
// kAudioLogOrder, so acks go out and log lines read guidance, media, system.

static uint64_t monoNs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

// Wall clock in ns, like the DHU's getCurrentNanos() (gettimeofday): the clock of Ack field 3.
static uint64_t wallNs() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000000ull + (uint64_t)tv.tv_usec * 1000ull;
}

AudioAcks::AudioAcks()
    : defer_(false), session_defer_(false), max_unacked_(10), force_ns_(1000000000ull), ack_times_(true), stats_s_(0),
      forced_logged_(0), underflow_(false), session_underflow_(false) {
    for (int i = 0; i < AUDIO_NSTREAMS; ++i) underflow_pending_[i] = underflow_sent_[i] = 0;
    pthread_mutex_init(&mutex_, NULL);
}

AudioAcks::~AudioAcks() {
    pthread_mutex_destroy(&mutex_);
}

// At least 4: with deferred acks a frame is acked only once fully played, and a 42.7 ms media frame spans about two
// 23 ms mixer blocks. With 1-2 frames the mixer waits for audio the phone can't send until it acks the very frame
// being played, so every frame ends in a padded gap (and an underflow report). 4 frames = ~170 ms of slack.
uint32_t AudioAcks::window() { return (uint32_t)envRange("NBT_AA_MAX_UNACKED", 10, 4, 64); }

void AudioAcks::startSession() {
    session_defer_ = defer_;
    max_unacked_ = window();
    force_ns_ = (uint64_t)envRange("NBT_AA_ACK_FORCE_MS", 1000, 0, 60000) * 1000000ull;
    ack_times_ = envFlag("NBT_AA_ACK_TIMES") != 0;
    pthread_mutex_lock(&mutex_);
    for (int i = 0; i < AUDIO_NSTREAMS; ++i) {
        ledger_[i].reset();
        ledger_[i].resetTotals();
        ledger_[i].setWindow(max_unacked_);
        underflow_pending_[i] = 0;
        underflow_sent_[i] = 0;
    }
    session_underflow_ = underflow_;
    pthread_mutex_unlock(&mutex_);
    stats_s_ = (double)monoNs() / 1e9;
    forced_logged_ = 0;
}

bool AudioAcks::onFrame(uint8_t channel, size_t len) {
    const int i = session_defer_ ? audioStreamForChannel(channel) : -1;
    if (i < 0) return false;
    pthread_mutex_lock(&mutex_);
    ledger_[i].onFrame(len, monoNs());
    pthread_mutex_unlock(&mutex_);
    return true;
}

void AudioAcks::onStreamStart(uint8_t channel, SendAckFn send, void* user) {
    const int i = session_defer_ ? audioStreamForChannel(channel) : -1;
    if (i < 0) return;
    // A new stream: ack what the old one still has pending (under its session id), start counting afresh. A tail of
    // the old stream still in the ring then counts towards the new frames (acks a little early, never late).
    std::vector<uint64_t> times;
    pthread_mutex_lock(&mutex_);
    const uint32_t left = ledger_[i].releaseAll(&times);
    pthread_mutex_unlock(&mutex_);
    const uint64_t to_wall = wallNs() - monoNs();
    for (uint32_t k = 0; k < left; ++k)
        send(channel, (ack_times_ && k < times.size()) ? times[k] + to_wall : 0, user);
}

bool AudioAcks::consumed(uint8_t channel, size_t bytes) {
    const int i = audioStreamForChannel(channel);
    if (i < 0 || bytes == 0) return false;
    pthread_mutex_lock(&mutex_);
    ledger_[i].onConsumed(bytes);
    const bool wake = ledger_[i].ready();
    pthread_mutex_unlock(&mutex_);
    return wake;
}

bool AudioAcks::postUnderflow(uint8_t channel) {
    const int i = audioStreamForChannel(channel);
    if (i < 0) return false;
    pthread_mutex_lock(&mutex_);
    const bool on = session_underflow_;
    if (on) ++underflow_pending_[i];
    pthread_mutex_unlock(&mutex_);
    return on;
}

bool AudioAcks::hasPending() {
    if (!session_defer_) return false;
    pthread_mutex_lock(&mutex_);
    bool any = ledger_[0].pending() || ledger_[1].pending() || ledger_[2].pending();
    pthread_mutex_unlock(&mutex_);
    return any;
}

void AudioAcks::drain(SendAckFn send, void* user) {
    if (!session_defer_) return;
    const uint64_t now = monoNs();
    // Arrival times are kept on the monotonic clock (forcing); Ack field 3 is wall clock: one offset per drain.
    const uint64_t to_wall = wallNs() - now;   // unsigned wrap-around is fine: added back below
    uint32_t n[AUDIO_NSTREAMS], forced[AUDIO_NSTREAMS];
    std::vector<uint64_t> times[AUDIO_NSTREAMS];
    pthread_mutex_lock(&mutex_);
    for (int i = 0; i < AUDIO_NSTREAMS; ++i) {
        const uint32_t before = ledger_[i].forced();
        n[i] = ledger_[i].take(now, force_ns_, &times[i]);
        forced[i] = ledger_[i].forced() - before;
    }
    pthread_mutex_unlock(&mutex_);
    for (int o = 0; o < AUDIO_NSTREAMS; ++o) {
        const int i = kAudioLogOrder[o];
        for (uint32_t k = 0; k < n[i]; ++k) {
            const uint64_t rx = (ack_times_ && k < times[i].size()) ? times[i][k] + to_wall : 0;
            if (!send(audioChannelForStream(i), rx, user)) break;
        }
        if (forced[i] && forced_logged_ < 5) {
            ++forced_logged_;
            printf("[AASession] %s: %u audio ack(s) forced after %u ms unacked (the mixer is not taking this stream: "
                   "no output device, or the car has not granted the source yet)%s\n", audioStreamName(i), forced[i],
                   (unsigned)(force_ns_ / 1000000ull), forced_logged_ == 5 ? "; not logged again this session" : "");
        }
    }
    logStats(false, false);
}

void AudioAcks::logStats(bool final, bool had_audio) {
    if (!session_defer_) {
        if (final && had_audio)
            printf("[Feature] deferred_acks=off (audio acked on arrival)\n");
        return;
    }
    const double now = (double)monoNs() / 1e9;
    if (!final && now - stats_s_ < 30.0) return;
    const double span = now - stats_s_;
    stats_s_ = now;
    char part[AUDIO_NSTREAMS][128];
    bool any = false;
    pthread_mutex_lock(&mutex_);
    for (int o = 0; o < AUDIO_NSTREAMS; ++o) {   // part[] in log order
        const int i = kAudioLogOrder[o];
        const MediaAckLedger& l = ledger_[i];
        const uint32_t acked = final ? l.acked() : l.ackedIv();
        part[o][0] = 0;
        if (!acked && !l.pending()) continue;
        any = true;
        snprintf(part[o], sizeof(part[o]), " %s: acked=%u forced=%u window_full=%u max_unacked=%u/%u;", audioStreamName(i),
                 acked, final ? l.forced() : l.forcedIv(), final ? l.windowFull() : l.windowFullIv(),
                 final ? l.maxPending() : l.maxPendingIv(), l.window());
    }
    for (int i = 0; i < AUDIO_NSTREAMS; ++i) ledger_[i].resetInterval();
    pthread_mutex_unlock(&mutex_);
    if (final)
        printf("[Feature] deferred_acks=on force_ms=%u%s%s%s%s\n", (unsigned)(force_ns_ / 1000000ull), part[0],
               part[1], part[2], any ? "" : " no audio");
    else if (any)
        printf("[AASession] deferred audio acks, last %.0f s:%s%s%s\n", span, part[0], part[1], part[2]);
}

void AudioAcks::drainUnderflows(SendUnderflowFn send, void* user) {
    if (!session_underflow_) return;
    uint32_t n[AUDIO_NSTREAMS];
    pthread_mutex_lock(&mutex_);
    for (int i = 0; i < AUDIO_NSTREAMS; ++i) { n[i] = underflow_pending_[i]; underflow_pending_[i] = 0; }
    pthread_mutex_unlock(&mutex_);
    for (int o = 0; o < AUDIO_NSTREAMS; ++o) {
        const int i = kAudioLogOrder[o];
        if (!n[i]) continue;
        // One report stands for all posted since the last drain: the phone learns the stream ran dry, not how often.
        const bool ok = send(audioChannelForStream(i), user);
        const uint32_t k = ++underflow_sent_[i];
        if (k <= 5 || (k % 50) == 0)
            printf("[AASession] %s ran dry: AudioUnderflowNotification (0x800b) #%u to the phone %s\n", audioStreamName(i),
                   k, ok ? "sent" : "SEND FAILED");
    }
}

void AudioAcks::logSessionEnd(bool had_audio) {
    logStats(true, had_audio);
    if (session_underflow_)
        printf("[Feature] underflow_reports=on guidance=%u media=%u system=%u (0x800b sent this session)\n",
               underflow_sent_[AUDIO_GUIDANCE], underflow_sent_[AUDIO_MEDIA], underflow_sent_[AUDIO_SYSTEM]);
}

} // namespace nbt
