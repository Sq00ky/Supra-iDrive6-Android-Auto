// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef AUDIO_STREAM_RATE_HPP
#define AUDIO_STREAM_RATE_HPP

// The input rate of each AA audio stream: declared by the phone, or measured from the frame timestamps.

#include <stdint.h>
#include <stddef.h>

namespace nbt {

// Works out the sample rate of an AA PCM stream from its frame timestamps (microseconds). The head unit offers the
// phone a list of AudioConfigs per sink and the phone doesn't say which one it took: the DHU manifest offers ch 8
// 16 kHz and 48 kHz mono, and the car showed the phone sending 48 kHz (2048 samples per 42.7 ms frame, 2026-11-01)
// where 16 kHz was assumed, so prompts played 3x slow. The rate is bytes before the latest frame / its timestamp
// distance from the first, snapped to a standard rate; it is known once the frames span 200 ms (at least 3 frames).
// The first timestamps of a stream are uneven (car 2026-11-02, media ch 9: frame gaps 41.9 / 43.7 / 44.4 / 59.2 /
// 41.7 ms, i.e. 44.4 kHz over the first 231 ms of a 48 kHz stream, snapped to 44100: music 8 % slow and the ring
// overflowing = crackling). So a stream should be given the rates it was actually offered (setCandidates): the
// result is then only ever one of those, the nearest within 25 %.
class PtsRateMeter {
public:
    PtsRateMeter();
    void reset();   // a new stream (AV start): measure again (keeps the candidates)
    // Restricts the result to these rates (the AudioConfigs offered for this sink, at most 4). n = 0: any standard rate
    // within 8 % (snap()).
    void setCandidates(const uint32_t* rates, size_t n);
    // Feeds one frame. Returns the snapped rate once known (and on every later frame), 0 until then or when the
    // timestamps match none of the rates.
    uint32_t add(uint64_t pts_us, size_t bytes, uint32_t channels);

    static uint32_t snap(uint32_t measured);   // nearest standard rate within 8 %, else 0
    // Nearest of rates[0..n) within pct %, else 0.
    static uint32_t snapTo(uint32_t measured, const uint32_t* rates, size_t n, uint32_t pct);

private:
    uint64_t first_pts_;
    uint64_t bytes_;       // bytes of the frames before the latest one
    uint32_t frames_;
    uint32_t rate_;        // result (0 = not known)
    bool done_;
    uint32_t cand_[4];
    size_t ncand_;
};
// The input rate of one AA audio stream. Each stream start may carry the phone's declared format (START_INDICATION
// configuration_index -> the offered AudioConfig): that is used from the first frame, and the timestamps are then
// only a cross-check (a disagreement is reported once, never acted on). Without a declaration the rate is measured
// from the timestamps, limited to the offered rates (PtsRateMeter::setCandidates).
class StreamRate {
public:
    StreamRate();
    void configure(uint32_t default_rate, const uint32_t* offered, size_t n, uint32_t channels);
    void reset();                      // session ended: back to the default, nothing declared
    void start(uint32_t declared);     // stream start; declared = the phone's rate, 0 = none
    // One frame. Returns true when rate() changed. *mismatch (optional) is set to the measured rate, once per start,
    // if the timestamps disagree with a declaration (0 otherwise).
    bool frame(uint64_t pts_us, size_t bytes, uint32_t* mismatch);
    uint32_t rate() const { return rate_; }
    bool declared() const { return declared_; }

private:
    PtsRateMeter meter_;
    uint32_t default_, rate_, ch_;
    bool declared_, warned_;
};

} // namespace nbt

#endif // AUDIO_STREAM_RATE_HPP
