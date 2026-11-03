// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef BROWSER_AUDIO_HPP
#define BROWSER_AUDIO_HPP

// Browser audio mode (NBT_AUDIO_MODE=browser, car-verified; the default in v1.8.61..v1.8.65): Android Auto sound over the AA
// connection instead of Bluetooth A2DP, played through the car's *browser* audio source.
//
// Why the browser source (static RE of the firmware, 2026-11-01): the CarPlay sources (connections 0x314-0x31e)
// refuse to allocate unless an iPhone is connected, so PCM written to /input_stereo_AirPlayMain stays silent.
// The browser source has no such gate:
//   - ConnectionMaster connection 0xb8 = eSRC_BROWSER (0x51) -> speakers, plugin BrowserFront.DMMAudioSource,
//     served by /opt/conn/bin/Online. Online's own CAudioCtrl builds exactly this id; its preAllocateAction
//     always succeeds. Class "interrupting entertainment": it pauses the current main source (radio, BT) while
//     it is built, the way the Owner's Manual audio does.
//   - the browser renders audio through the shim /opt/mm/lib/libasound/libasound.so, a multi-client front end
//     of dev-libasound, which mixes its clients into A2I input_stereo_3 (Connectivity.cfg: dev-libasound
//     -x input_stereo_3). We are one more client of it, opened like the browser's GStreamer sink does
//     (snd_pcm_open_preferred, or WKT_SOUND_CONFIG-style card:device).
//   - that input_stereo_3 is the physical input of connection 0xb8 is inferred from the config, not proven.
//
// Pipeline: session thread -> per-stream raw rings (AA format) -> mixer thread: resample to the device rate,
// mix MEDIA (ch 9) + GUIDANCE (ch 8) + SYSTEM_AUDIO (ch 16) with media ducked under the other two
// (audio_mixer.hpp), write blocking to the shim. The connection is built when the first stream needs it and
// removed after NBT_BROWSER_IDLE_S without sound; queued audio waits for the grant, so a prompt is not cut.
//
// Environment:
//   NBT_BROWSER_CONN     connection id (default 0xb8; 0 = do not route, just play)
//   NBT_BROWSER_ASOUND   shim path (default /opt/mm/lib/libasound/libasound.so)
//   NBT_BROWSER_PCM      "card:device" instead of snd_pcm_open_preferred
//   NBT_BROWSER_RATE     device rate to ask for (default 48000; lowered to the device's reported maximum, 44100 here)
//   NBT_BROWSER_FORCE_RATE  experiment: ask for this rate even outside the device's reported range (e.g. 48000, to
//                        play the 48 kHz AA streams without resampling). Off by default. If the car refuses it the
//                        device does not open and there is no AA audio that session; the reason is in sloginfo.
//   NBT_BROWSER_IDLE_S   remove the connection after this many seconds without sound once AA media has played
//                        (default 10, 0 = keep it until the session ends)
//   NBT_BROWSER_PROMPT_IDLE_MS  when no AA media is playing (e.g. the user is on the radio), remove it this many ms
//                        after a prompt / system sound ends, so the radio is cut only for the prompt (default 1000,
//                        0 = use NBT_BROWSER_IDLE_S)
//   NBT_BROWSER_REFOCUS  build the connection again when the phone closes the mic (default on)
//   NBT_BROWSER_WAIT_MS  wait up to this long for a block of a playing stream that is a little late, instead of padding
//                        it with silence (default 30, max 60, 0 = pad at once). Only while the device is full (the
//                        previous write blocked), so it still holds ~92 ms and waits cannot add up.
//   NBT_BROWSER_DRIFT    0 = no clock-drift correction for AA media (default on: after learning the level the phone
//                        keeps queued for 10 s, the media resampler is trimmed by up to +/-500 ppm to hold it)
//   NBT_DUCK_DB          media level while ducked, dB (default -12)
//   NBT_DUCK_ATTACK_MS / NBT_DUCK_RELEASE_MS / NBT_DUCK_HOLD_MS   ramp down / up / hold (default 80 / 500 / 600)
//   NBT_MEDIA_GAIN_DB / NBT_GUIDANCE_GAIN_DB / NBT_SYSTEM_GAIN_DB fixed per-stream gains (default 0)

#include "audio/audio_mixer.hpp"
#include "audio/audio_route.hpp"
#include "audio/backend.hpp"
#include "audio/channels.hpp"
#include "audio/pcm_utils.hpp"
#include <pthread.h>
#include <stdint.h>
#include <stddef.h>
#include <vector>

namespace nbt {

class BrowserAudio : public AudioBackend {
public:
    // Stream indices: the shared AudioStream table (audio/channels.hpp).
    enum Stream { MEDIA = AUDIO_MEDIA, GUIDANCE = AUDIO_GUIDANCE, SYSTEM = AUDIO_SYSTEM, NSTREAMS = AUDIO_NSTREAMS };

    BrowserAudio();
    ~BrowserAudio();

    // Reads the environment, loads the libasound shim and finds audioconn. Plays nothing yet.
    bool init();

    // Session thread. channel = AA channel (9 media, 8 guidance, 16 system audio); others are ignored. pts = the
    // frame's timestamp (us): measures the rate when the stream start declared none, else cross-checks it (0 = none).
    virtual void onFrame(uint8_t channel, const uint8_t* pcm, size_t len, uint64_t pts);
    // rate / channels: the format the phone declared for this stream in START_INDICATION (0 = it gave none: the rate
    // is then measured from the timestamps, among the rates offered for the stream).
    virtual void onStreamState(uint8_t channel, bool started, uint32_t rate, uint32_t channels);
    // Session ended (session thread): stop the mixer, close the device, remove the connection.
    virtual void onSessionEnded();
    // Phone closed the mic (Assistant): refocus("mic closed").
    virtual void onMicClosed();
    // Ask for the connection again in case the voice scenario moved the car off it.
    void refocus(const char* why);
    // Daemon shutdown.
    virtual void stop();

    // Every byte of AA PCM this object takes from a stream, oldest first: read by the mixer, dropped by a full ring,
    // or discarded (media paused, no grant, no device). AASession acks a frame once all of its bytes are reported
    // (deferred acks, media_ack.hpp). Called from the mixer and session threads. Set before the first session.
    typedef void (*ConsumedFn)(uint8_t channel, size_t bytes, void* user);
    void setConsumedCallback(ConsumedFn fn, void* user) { consumed_fn_ = fn; consumed_user_ = user; }
    // With deferred acks: the ack window in frames (0 = acks on arrival). A phone held to it keeps about window x
    // frame length of media queued, so the drift loop's target cap is raised to cover that. Set before the first session.
    void setAckWindow(uint32_t frames) { ack_window_ = frames; }

    // A started stream that had already sent audio ran dry: a mixer block got less than a whole block and was padded.
    // Called once per dry spell (again only after a whole block was available), from the mixer thread. AASession
    // reports it to the phone (AudioUnderflowNotification). Set before the first session.
    typedef void (*UnderflowFn)(uint8_t channel, void* user);
    void setUnderflowCallback(UnderflowFn fn, void* user) { underflow_fn_ = fn; underflow_user_ = user; }

    // Mixer thread (internal).
    void mixerLoop();
    void routeGranted();
    void releaseDone();

private:
    BrowserAudio(const BrowserAudio&);
    BrowserAudio& operator=(const BrowserAudio&);

    // RELEASING: `audioconn -r` queued; no new build until it has run, so a build can never be overtaken by it.
    enum RouteState { ROUTE_IDLE = 0, ROUTE_BUILDING = 1, ROUTE_ROUTED = 2, ROUTE_RELEASING = 3 };

    bool loadShim();
    bool openPcm();
    bool openPcmWith(int frag, int frags);
    void closePcm();
    bool writePcm(const int16_t* buf, size_t frames);
    void ensureMixer();
    void stopMixer();
    // mixer thread; *got = converted samples obtained (the rest of the block is zero-padded)
    bool pull(int s, size_t frames, int16_t* out, bool* audible, size_t* got);
    void requestRoute(const char* why);
    void releaseRoute(const char* why);
    bool routedForPlayback();
    void logStats(bool force);
    void consumed(int s, size_t bytes);   // reports to consumed_fn_
    void discard(int s);                  // clears stream s's ring, reporting the bytes

    ConsumedFn consumed_fn_;
    void* consumed_user_;
    uint32_t ack_window_;                  // setAckWindow (0 = acks on arrival)
    volatile uint32_t media_frame_us_;     // length of the last AA media frame (session thread writes)

    // Resampler engine (NBT_BROWSER_RESAMPLER, default speex; the switch file flips it at run time for an A/B by ear).
    void setResampler(PcmResampler::Engine e, const char* why);   // mixer thread (or before it runs)
    void checkResamplerSwitch(uint64_t now);                       // mixer thread, once a second
    void checkResamplerHealth(uint64_t interval_ms);                // mixer thread, each stats line
    PcmResampler::Engine rs_engine_;     // in use
    PcmResampler::Engine rs_engine_cfg_; // NBT_BROWSER_RESAMPLER (what a missing switch file means)
    int rs_quality_;                     // NBT_BROWSER_SPEEX_Q
    const char* rs_switch_path_;         // NBT_BROWSER_RESAMPLER_FILE ("" = none)
    uint64_t rs_switch_check_ms_;
    uint64_t rs_us_;                     // stats interval: time spent converting (us)
    bool rs_failed_;                     // speex fell back to linear this session

    // Mixer thread scheduling: NBT_BROWSER_PRIO (applied by the mixer thread itself) and how late its writes come.
    void raiseMixerPriority();
    int prio_want_;                      // NBT_BROWSER_PRIO (0 = leave the thread's priority alone)
    int prio_now_;                       // the mixer thread's priority after raiseMixerPriority (-1 = unknown)
    uint64_t last_write_ms_;             // start of the previous device write (0 = none yet)
    uint32_t gap_max_ms_;                // stats interval: longest time between two device write starts
    uint32_t gap_late_;                  // stats interval: gaps over twice the block period
    uint32_t gap_late_total_;            // session

    // Underflow reports (mixer thread, except frames_since_start_).
    void checkUnderflow(int s, bool playing, size_t got, size_t need, uint64_t now);   // playing = started, not held
    void logUnderflowFollowUp(uint64_t now);
    UnderflowFn underflow_fn_;
    void* underflow_user_;
    volatile uint32_t frames_since_start_[NSTREAMS];   // session thread counts, resets at each start
    bool dry_[NSTREAMS];                 // in a dry spell that was already reported
    uint64_t report_ms_[NSTREAMS];       // last report (0 = no follow-up pending)
    uint64_t report_written_[NSTREAMS];  // ring bytes written at that report
    uint32_t reports_[NSTREAMS];         // this session

    // config
    uint32_t conn_;
    uint32_t want_rate_;
    uint32_t force_rate_;    // NBT_BROWSER_FORCE_RATE: ask the device for this rate whatever it reports (0 = off)
    int idle_s_;
    int prompt_idle_ms_;
    bool refocus_;
    DuckConfig duck_cfg_;
    const char* shim_path_;

    // libasound shim (dlopen'ed, function pointers held as void* to keep the asound types out of this header)
    void* lib_;
    void* fn_[11];
    void* pcm_;
    uint32_t rate_;          // device rate actually set up
    uint32_t frag_bytes_;

    // streams
    PcmRing* ring_[NSTREAMS];
    PcmResampler rs_[NSTREAMS];
    std::vector<int16_t> pending_[NSTREAMS];   // converted (rate_, stereo), mixer thread only
    StreamRate srate_[NSTREAMS];               // declared / measured input rate per stream (session thread)
    volatile uint32_t src_rate_[NSTREAMS];     // srate_[s].rate() published for the mixer thread
    uint32_t rs_rate_[NSTREAMS];               // the rate rs_ is configured for (mixer thread)
    volatile bool started_[NSTREAMS];
    volatile bool flush_[NSTREAMS];
    uint32_t frames_in_[NSTREAMS];
    StreamWatch watch_[NSTREAMS];              // per-block gap counters for the stats line (mixer thread)
    UnderrunGuard guard_[NSTREAMS];            // wait for late input instead of padding (mixer thread)
    bool gap_[NSTREAMS];                       // the last block of this stream was padded: fade the next one in
    uint32_t wait_ms_;                         // NBT_BROWSER_WAIT_MS (0 = pad at once)
    DriftControl drift_;                       // media clock-drift trim (mixer thread)
    DriftPi drift_pi_;                         // its proportional-integral version, fed drift_'s error (mixer thread)
    bool drift_pi_on_;                         // NBT_BROWSER_DRIFT_LOOP: pi (default) applies drift_pi_, p drift_
    bool drift_on_;                            // NBT_BROWSER_DRIFT (default on)
    volatile bool drift_reset_;                // set by the session thread at a media start
    int32_t drift_applied_;                    // trim currently set on rs_[MEDIA] (mixer thread)
    uint32_t waited_blocks_, waited_ms_;       // stats interval (mixer thread)
    uint64_t waited_blocks_total_;             // session (mixer thread)
    PrerollGate gate_[NSTREAMS];               // pre-roll: hold a started stream until it has a cushion (mixer thread)
    uint32_t preroll_ms_;                      // NBT_BROWSER_PREROLL_MS (0 = off)
    volatile uint32_t start_seq_[NSTREAMS];    // bumped by the session thread at every START
    uint32_t seen_seq_[NSTREAMS];              // the last start_seq_ the mixer saw (mixer thread)
    // Whether waiting for late input is safe (waitIsSafe), per stats interval (mixer thread): device writes, the ones
    // that blocked, and the lowest / highest lead over real time seen before a block.
    uint32_t writes_, writes_blocked_;
    int64_t lead_min_, lead_max_;
    bool lead_seen_;

    // mixer thread
    pthread_t thread_;
    bool thread_valid_;
    volatile bool stop_;
    volatile bool pcm_failed_;
    AudioMixer mixer_;

    // route
    AudioRoute route_;
    pthread_mutex_t lock_;
    RouteState route_state_;
    uint64_t route_since_ms_;
    uint64_t route_backoff_until_ms_;   // after a connection that was never granted
    uint64_t last_sound_ms_;
    uint64_t last_media_ms_;            // last AA media sound or open media stream (0 = none this session)

    // stats
    uint64_t blocks_;
    uint32_t write_errors_;
    uint32_t prepares_;
    uint64_t last_stats_ms_;
    bool was_ducking_;
};

} // namespace nbt

#endif // BROWSER_AUDIO_HPP
