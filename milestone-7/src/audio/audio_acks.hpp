// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef AUDIO_AUDIO_ACKS_HPP
#define AUDIO_AUDIO_ACKS_HPP

// The AA audio acks and underflow reports of one session, as Google's head unit does them (browser audio mode):
//   - deferred acks: a PCM frame on ch 8 / 9 / 16 is acked once the consumer reports its bytes taken (consumed()),
//     or after NBT_AA_ACK_FORCE_MS (default 1000) unacked; the phone may have NBT_AA_MAX_UNACKED frames outstanding.
//     Off: AASession acks on arrival.
//   - underflow reports: AudioUnderflowNotification (0x800b) once per dry spell the consumer reports.
// AASession owns one, records frames and sends what it releases (its SendAckFn / SendUnderflowFn); the consumer
// (mixer thread) reports into it. The per-stream ledgers and counters are guarded by one mutex; the session settings
// are copied at startSession() and read by the session thread only. Log lines keep their [AASession] / [Feature] text.

#include "audio/channels.hpp"
#include "audio/media_ack.hpp"
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <vector>

namespace nbt {

class AudioAcks {
public:
    // Session thread: one audio ack on `channel`; rx_wall_ns = the frame's arrival time for Ack field 3 (0 = none).
    // Returns false if it could not be sent (the rest of that stream's batch is dropped, as before).
    typedef bool (*SendAckFn)(uint8_t channel, uint64_t rx_wall_ns, void* user);
    // Session thread: one AudioUnderflowNotification on `channel`. Returns whether it was sent.
    typedef bool (*SendUnderflowFn)(uint8_t channel, void* user);

    AudioAcks();
    ~AudioAcks();

    // Before AASession::start(); applies from the next session.
    void setDeferred(bool on) { defer_ = on; }
    void setUnderflowReports(bool on) { underflow_ = on; }

    // The ack window audio sinks get (NBT_AA_MAX_UNACKED, default 10, 4..64): how many frames the phone may have
    // unacked. The drift loop's target cap covers it too.
    static uint32_t window();

    // Session start (before the session thread runs): this session's settings, ledgers and counters afresh.
    void startSession();
    bool deferred() const { return session_defer_; }         // this session
    uint32_t maxUnacked() const { return max_unacked_; }      // window our AV setup response gives audio sinks
    bool ackTimes() const { return ack_times_; }              // NBT_AA_ACK_TIMES: acks carry the arrival time

    // Session thread: an audio PCM frame of len bytes arrived, before the consumer can take it. True = recorded:
    // ack it later (drain), not now.
    bool onFrame(uint8_t channel, size_t len);
    // Session thread: AV START_INDICATION on `channel`: ack what the old stream still has pending, start afresh.
    void onStreamStart(uint8_t channel, SendAckFn send, void* user);

    // Consumer (mixer thread). True = wake the session thread to send.
    bool consumed(uint8_t channel, size_t bytes);
    bool postUnderflow(uint8_t channel);

    // Session thread.
    bool hasPending();                                        // any unacked deferred frame
    void drain(SendAckFn send, void* user);                   // send the acks released / forced, 30 s stats line
    void drainUnderflows(SendUnderflowFn send, void* user);   // one report per stream that ran dry since the last
    // Session end: the [Feature] summaries. had_audio = the session received audio frames.
    void logSessionEnd(bool had_audio);

private:
    AudioAcks(const AudioAcks&);
    AudioAcks& operator=(const AudioAcks&);

    void logStats(bool final, bool had_audio);   // 30 s interval line, or the session summary

    bool defer_;                         // setDeferred(); copied to session_defer_ at startSession()
    bool session_defer_;                 // this session (session thread)
    uint32_t max_unacked_;               // NBT_AA_MAX_UNACKED (default 10)
    uint64_t force_ns_;                  // NBT_AA_ACK_FORCE_MS in ns (0 = never force)
    bool ack_times_;                     // NBT_AA_ACK_TIMES (default on)
    MediaAckLedger ledger_[AUDIO_NSTREAMS];   // per AudioStream, guarded by mutex_
    pthread_mutex_t mutex_;
    double stats_s_;                     // last periodic ack stats line (session thread)
    uint32_t forced_logged_;             // forced acks printed individually this session (session thread)

    bool underflow_;                     // setUnderflowReports(); copied to session_underflow_ at startSession()
    bool session_underflow_;             // guarded by mutex_ (the consumer reads it)
    uint32_t underflow_pending_[AUDIO_NSTREAMS];   // reports posted by the consumer, not sent yet (mutex_)
    uint32_t underflow_sent_[AUDIO_NSTREAMS];      // this session (session thread)
};

} // namespace nbt

#endif // AUDIO_AUDIO_ACKS_HPP
