// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef AUDIO_STREAM_INPUT_HPP
#define AUDIO_STREAM_INPUT_HPP

// One AA audio stream inside the browser mixer (BrowserAudio keeps one per AudioStream). The fields are grouped by
// the thread that writes them, so it stays visible which ones cross between the session and mixer threads:
//   in    written by the session thread, read by the mixer (flags are volatile: no atomics on QNX GCC 4.4; the
//         ring is thread-safe itself)
//   sess  session thread only
//   mix   mixer thread only (also touched while the mixer is stopped: init, session end)

#include "audio/pcm_ring.hpp"
#include "audio/playout.hpp"
#include "audio/resampler.hpp"
#include "audio/stream_rate.hpp"
#include <stddef.h>
#include <stdint.h>
#include <vector>

namespace nbt {

struct StreamInput {
    struct In {
        PcmRing* ring;                          // raw AA PCM (allocated by BrowserAudio::init)
        volatile uint32_t src_rate;             // sess.rate.rate() published for the mixer
        volatile bool started;                  // between START and STOP
        volatile bool flush;                    // drop what the mixer has converted (media paused)
        volatile uint32_t frames_since_start;   // AA frames since the last START (no underflow before the first)
        volatile uint32_t start_seq;            // bumped at every START: the mixer holds the stream for pre-roll again
        uint32_t frames_in;                     // AA frames this session (the stats line)
        In() : ring(NULL), src_rate(0), started(false), flush(false), frames_since_start(0), start_seq(0),
               frames_in(0) {}
    } in;

    struct Sess {
        StreamRate rate;                        // declared / measured input rate
    } sess;

    struct Mix {
        PcmResampler rs;
        uint32_t rs_rate;                       // the rate rs is configured for
        std::vector<int16_t> pending;           // converted (device rate, stereo), not played yet
        StreamWatch watch;                      // per-block gap counters for the stats line
        UnderrunGuard guard;                    // wait for late input instead of padding
        PrerollGate gate;                       // pre-roll: hold a started stream until it has a cushion
        uint32_t seen_seq;                      // the last in.start_seq seen
        bool gap;                               // the last block was padded: fade the next one in
        // Underflow reports to the phone
        bool dry;                               // in a dry spell that was already reported
        uint64_t report_ms;                     // last report (0 = no follow-up pending)
        uint64_t report_written;                // ring bytes written at that report
        uint32_t reports;                       // this session
        Mix() : rs_rate(0), seen_seq(0), gap(false), dry(false), report_ms(0), report_written(0), reports(0) {}
    } mix;

private:
    StreamInput(const StreamInput&);
    StreamInput& operator=(const StreamInput&);
public:
    StreamInput() {}
};

} // namespace nbt

#endif // AUDIO_STREAM_INPUT_HPP
