// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef AUDIO_PLAYOUT_HPP
#define AUDIO_PLAYOUT_HPP

// What the browser mixer plays of each stream per block: gap counters, the late-input wait, pre-roll.

#include <stdint.h>
#include <stddef.h>

namespace nbt {

// Health counters for one AA stream in the mixer (stats interval + session totals). The 10 s stats line only sampled
// the ring once, so a dip in between left no trace; this counts every mixer block of a started stream:
//   short = the block got some audio but not a whole block (the rest was padded)
//   empty = the block got no audio at all
// and keeps the lowest ring fill (bytes, read before the block) seen in the interval.
class StreamWatch {
public:
    StreamWatch();
    void resetInterval();      // after each stats line
    void resetAll();           // session ended
    // One mixer block. started = the stream is between START and STOP; queued = ring bytes before the pull;
    // got / need = converted samples obtained / wanted. Blocks of a stopped stream (its tail) are not counted.
    void onPull(bool started, size_t queued, size_t got, size_t need);

    uint32_t pulls() const { return pulls_; }
    uint32_t shortPulls() const { return short_; }
    uint32_t emptyPulls() const { return empty_; }
    bool hasMin() const { return pulls_ > 0; }
    size_t minQueued() const { return min_queued_; }
    uint64_t totalGaps() const { return total_gaps_; }   // short + empty since the session started

private:
    uint32_t pulls_, short_, empty_;
    size_t min_queued_;
    uint64_t total_gaps_;
};

// Waiting for late input instead of padding with silence (browser mode). A wait of at most `budget` ms for a frame
// that is a few ms late is inaudible as long as the device still holds more than that, while padding puts a hole in
// the audio at once. Waiting is allowed only when waitIsSafe() says the device holds enough (can_wait), so waits can
// never drain it: otherwise the block is padded, which refills it. After a wait runs out the stream counts as starved
// and is padded at once until a whole block is available again (data really stopped, e.g. a pause without STOP).

// Whether the mixer may wait for late input before this block. Either its previous write blocked (the device was
// full and has just freed one of its 5 fragments, so it still holds ~4, ~92 ms), or it is lead_ms ahead of real time
// (frames written since the device opened minus the time since then) by at least the wait budget plus a block plus
// a 20 ms margin. The second case is the car's: the shim's writes return at once and the mixer is paced by its own
// 150 ms lead, so a write never blocks and the first rule alone never allowed a wait (car logs 2026-11-03: 0 waits).
// The lead is a floor for what the device holds: if the device ever ran dry it played less than the time elapsed.
bool waitIsSafe(bool last_write_blocked, int64_t lead_ms, uint32_t budget_ms, uint32_t block_ms);

// Pre-roll (browser mode). The phone sends each stream in real time, one 42.7 ms frame per frame period with no
// burst at the start, so a stream played the moment its first frame arrives has no cushion: a frame a few ms late
// leaves a block short. In the car logs that is "ran dry: a block got 857 of 1024 frames" (one frame gives 1881.6
// output frames at 44.1 kHz; 1881.6 - 1024 = 857), a ~4 ms hole heard as crackle in prompts and Assistant replies.
// The gate holds a started stream until target output frames are queued, or its first queued audio has waited
// hold_ms (a sound shorter than the target still plays, however late the phone sends STOP), then lets it play.
// A stream that runs empty while playing (a whole block with no audio) is held again to rebuild the cushion; a short
// block is not, since the phone's next frame comes on its own clock and the cushion recovers by itself.
// A stopped stream is never held, so the tail of a prompt plays out.
class PrerollGate {
public:
    PrerollGate();
    void configure(size_t target_frames, uint32_t hold_ms);   // target 0 = off: every stream plays at once
    void restart();                                            // a new START: hold this stream again
    void reset();                                              // session ended: restart() and clear the counters
    // Before a block: true = mix this stream now. started = between START and STOP; avail = output frames queued.
    bool ready(bool started, size_t avail_frames, uint64_t now_ms);
    // After a block this stream played while started: got 0 frames = it ran empty, hold it again.
    void onPulled(bool started, size_t got_frames);
    bool holding() const { return !open_; }
    uint32_t holds() const { return holds_; }               // holds that ended since resetInterval()
    uint64_t totalHolds() const { return total_holds_; }    // since the session started
    void resetInterval() { holds_ = 0; }

private:
    size_t target_;
    uint32_t hold_ms_;
    bool open_;
    uint64_t since_ms_;   // when the first queued audio of the current hold was seen (0 = nothing queued yet)
    uint32_t holds_;
    uint64_t total_holds_;
};

// Output frames that queued input will give after conversion: ring bytes at src_rate / src_ch, plus already
// converted pending samples (out_ch interleaved).
size_t estimateOutFrames(size_t ring_bytes, size_t pending_samples, uint32_t src_rate, uint32_t src_ch,
                         uint32_t out_rate, uint32_t out_ch);

class UnderrunGuard {
public:
    explicit UnderrunGuard(uint32_t budget_ms = 30);
    void setBudget(uint32_t ms) { budget_ms_ = ms; }   // 0 = never wait (pad at once, the old behaviour)
    void reset();                                       // session ended
    // Before a block, after waited_ms of waiting for it: true = wait a little longer for this stream. can_wait =
    // waitIsSafe() (the device holds enough to cover the wait); without it the block is padded.
    bool shouldWait(bool started, size_t avail_frames, size_t need_frames, uint32_t waited_ms, bool can_wait);
    // After the block's pull: a whole block clears the starved state.
    void onPulled(bool started, size_t got_frames, size_t need_frames);
    bool starved() const { return starved_; }
    uint32_t timeouts() const { return timeouts_; }    // waits that ran out (the block was padded)

private:
    uint32_t budget_ms_;
    bool starved_;
    uint32_t timeouts_;
};

} // namespace nbt

#endif // AUDIO_PLAYOUT_HPP
