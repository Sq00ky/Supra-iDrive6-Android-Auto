// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef BROWSER_AUDIO_HPP
#define BROWSER_AUDIO_HPP

// Browser audio mode (NBT_AUDIO_MODE=browser, EXPERIMENT, not car-verified): Android Auto sound over the AA
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
//   NBT_BROWSER_RATE     device rate to ask for (default 48000)
//   NBT_BROWSER_IDLE_S   remove the connection after this many seconds without sound once AA media has played
//                        (default 10, 0 = keep it until the session ends)
//   NBT_BROWSER_PROMPT_IDLE_MS  when no AA media is playing (e.g. the user is on the radio), remove it this many ms
//                        after a prompt / system sound ends, so the radio is cut only for the prompt (default 1000,
//                        0 = use NBT_BROWSER_IDLE_S)
//   NBT_BROWSER_REFOCUS  build the connection again when the phone closes the mic (default on)
//   NBT_DUCK_DB          media level while ducked, dB (default -12)
//   NBT_DUCK_ATTACK_MS / NBT_DUCK_RELEASE_MS / NBT_DUCK_HOLD_MS   ramp down / up / hold (default 80 / 500 / 600)
//   NBT_MEDIA_GAIN_DB / NBT_GUIDANCE_GAIN_DB / NBT_SYSTEM_GAIN_DB fixed per-stream gains (default 0)

#include "audio_mixer.hpp"
#include "audio_route.hpp"
#include "pcm_utils.hpp"
#include <pthread.h>
#include <stdint.h>
#include <stddef.h>
#include <vector>

namespace nbt {

class BrowserAudio {
public:
    enum Stream { MEDIA = 0, GUIDANCE = 1, SYSTEM = 2, NSTREAMS = 3 };

    BrowserAudio();
    ~BrowserAudio();

    // Reads the environment, loads the libasound shim and finds audioconn. Plays nothing yet.
    bool init();

    // Session thread. channel = AA channel (9 media, 8 guidance, 16 system audio); others are ignored. pts = the
    // frame's timestamp (us); it tells which of the offered sample rates the phone chose (0 = unknown).
    void onAaAudio(uint8_t channel, const uint8_t* pcm, size_t len, uint64_t pts = 0);
    void onAaStreamState(uint8_t channel, bool started);
    // Session ended (session thread): stop the mixer, close the device, remove the connection.
    void onSessionEnded();
    // Phone closed the mic (Assistant): ask for the connection again in case the voice scenario moved the car off it.
    void refocus(const char* why);
    // Daemon shutdown.
    void stop();

    static int streamForChannel(uint8_t channel);
    static const char* streamName(int s);

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
    bool pull(int s, size_t frames, int16_t* out, bool* audible);   // mixer thread
    void requestRoute(const char* why);
    void releaseRoute(const char* why);
    bool routedForPlayback();
    void logStats(bool force);

    // config
    uint32_t conn_;
    uint32_t want_rate_;
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
    PtsRateMeter meter_[NSTREAMS];             // session thread
    volatile uint32_t src_rate_[NSTREAMS];     // AA sample rate per stream (set by the session thread)
    uint32_t rs_rate_[NSTREAMS];               // the rate rs_ is configured for (mixer thread)
    volatile bool started_[NSTREAMS];
    volatile bool flush_[NSTREAMS];
    uint32_t frames_in_[NSTREAMS];

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
