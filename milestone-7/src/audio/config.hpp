// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef AUDIO_CONFIG_HPP
#define AUDIO_CONFIG_HPP

// The audio settings, read from the environment in one place (host-tested: tests/test_audio_config.cpp). main reads
// the mode and the selected mode's config once at start-up and hands it over. Every setting keeps the parsing,
// default and range it had where it used to be read. Not here: the per-session ack settings (NBT_AA_MAX_UNACKED,
// NBT_AA_ACK_FORCE_MS, NBT_AA_ACK_TIMES: AudioAcks reads them at each session start), NBT_MIC (mic_uplink),
// NBT_VOICE_SCENARIO, NBT_BT_PLAY, the audioconn wrapper's NBT_AUDIOCONN* and the A2I mode's own settings.

#include "nbt_env.hpp"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

namespace nbt {

// Bluetooth audio mode (default ON since v1.7.5; car-verified 2026-10-23): leaves Android Auto media to the
// phone's Bluetooth A2DP link. The ServiceDiscovery omits the AA media sink (channel 9) so the phone plays
// media over A2DP, and the daemon asks the AudioManager for the car's Bluetooth audio
// source (`audioconn -b 0x3a 0x1`, car-verified to work from outside the HMI). The A2I pipeline is not
// opened in this mode. Overrides: NBT_BT_SRC (default 0x3a eSRC_A2DP), NBT_BT_SINK (default 0x1
// eSNK_SPEAKERS), NBT_BT_RELEASE=1 (release the source when the phone disconnects), NBT_SDR_DROP.
// Audio path for Android Auto sound. NBT_AUDIO_MODE picks it; when unset, the older NBT_AUDIO_BT switch decides
// (on = bt, 0/off = a2i) and with neither set the default is bt again (v1.8.66; browser was the default in
// v1.8.61..v1.8.65 and stays available with NBT_AUDIO_MODE=browser).
//   bt       (default) media, nav prompts and Assistant speech stay on the phone's Bluetooth A2DP link
//   browser  the AA media (ch 9), guidance (ch 8) and system-audio (ch 16) streams are mixed in the
//            daemon, with media ducked under guidance/system sounds, and played through the car's browser audio
//            source (dev-libasound -> A2I input_stereo_3, ConnectionMaster connection 0xb8). See browser_audio.hpp.
//   a2i      the old staged A2I pipeline (a2i_audio.cpp, NBT_AUDIO_STAGE); not audible on the car
enum AudioMode { AUDIO_MODE_BT = 0, AUDIO_MODE_BROWSER = 1, AUDIO_MODE_A2I = 2 };

inline AudioMode audioMode() {
    const char* m = getenv("NBT_AUDIO_MODE");
    if (m && *m) {
        if (strcmp(m, "browser") == 0) return AUDIO_MODE_BROWSER;
        if (strcmp(m, "a2i") == 0) return AUDIO_MODE_A2I;
        if (strcmp(m, "bt") == 0) return AUDIO_MODE_BT;
    }
    int bt = envFlag("NBT_AUDIO_BT");
    return bt == 0 ? AUDIO_MODE_A2I : AUDIO_MODE_BT;
}

inline const char* audioModeName(AudioMode m) {
    return m == AUDIO_MODE_BROWSER ? "browser" : (m == AUDIO_MODE_A2I ? "a2i" : "bt");
}

inline bool btAudioEnabled() {
    return audioMode() == AUDIO_MODE_BT;
}

inline bool browserAudioEnabled() {
    return audioMode() == AUDIO_MODE_BROWSER;
}

// Browser audio mode (browser_audio.hpp has what each setting does).
struct BrowserConfig {
    uint32_t conn;              // NBT_BROWSER_CONN: connection id (0xb8; 0 = do not route, just play)
    uint32_t want_rate;         // NBT_BROWSER_RATE: device rate to ask for (48000)
    uint32_t force_rate;        // NBT_BROWSER_FORCE_RATE: experiment, ask for this rate whatever (0 = off)
    int idle_s;                 // NBT_BROWSER_IDLE_S: release after this long without sound once media played (10)
    int prompt_idle_ms;         // NBT_BROWSER_PROMPT_IDLE_MS: release this long after a prompt alone (1000)
    uint32_t wait_ms;           // NBT_BROWSER_WAIT_MS: late-input wait per block (30, 0..60)
    uint32_t preroll_ms;        // NBT_BROWSER_PREROLL_MS: pre-roll per stream (60, 0..200)
    bool drift;                 // NBT_BROWSER_DRIFT: media clock-drift correction (on)
    bool drift_pi;              // NBT_BROWSER_DRIFT_LOOP: proportional-integral (default) or p
    int prio;                   // NBT_BROWSER_PRIO: mixer thread priority (15, 0..63; 0 = leave alone)
    bool resampler_linear;      // NBT_BROWSER_RESAMPLER=linear (default speexdsp)
    int speex_q;                // NBT_BROWSER_SPEEX_Q (5, 0..10)
    const char* resampler_file; // NBT_BROWSER_RESAMPLER_FILE: run-time engine switch (/tmp/nbt_resampler; "" = off)
    bool refocus;               // NBT_BROWSER_REFOCUS: build the connection again when the mic closes (on)
    const char* shim_path;      // NBT_BROWSER_ASOUND (/opt/mm/lib/libasound/libasound.so)
    const char* pcm;            // NBT_BROWSER_PCM: "card:device" instead of snd_pcm_open_preferred (NULL = unset)
    int frag;                   // NBT_BROWSER_FRAG: fragment bytes (4096; the device rounds and clamps it)
    int frags;                  // NBT_BROWSER_FRAGS: fragments (5; clamped to 1..256)
    int duck_db;                // NBT_DUCK_DB: media level while ducked (-12)
    uint32_t duck_attack_ms;    // NBT_DUCK_ATTACK_MS (80)
    uint32_t duck_release_ms;   // NBT_DUCK_RELEASE_MS (500)
    uint32_t duck_hold_ms;      // NBT_DUCK_HOLD_MS (600)
    int media_gain_db;          // NBT_MEDIA_GAIN_DB (0)
    int guidance_gain_db;       // NBT_GUIDANCE_GAIN_DB (0)
    int system_gain_db;         // NBT_SYSTEM_GAIN_DB (0)
    bool defer_acks;            // NBT_AA_DEFER_ACKS: ack AA audio once played (on)
    bool underflow_reports;     // NBT_AA_UNDERFLOW_REPORT: AudioUnderflowNotification to the phone (on)
    BrowserConfig();            // the defaults
};
BrowserConfig readBrowserConfig();

// NBT_SPEECH (Bluetooth mode only, speech_out.hpp).
struct SpeechConfig {
    bool enabled;               // NBT_SPEECH=1
    const char* endpoint;       // NBT_SPEECH_EP (NULL / empty = the factory alternate nav endpoint)
    uint32_t src;               // NBT_SPEECH_SRC (0 = no audioconn request)
    uint32_t sink;              // NBT_SPEECH_SINK (0x1)
    SpeechConfig();
};

// Bluetooth audio mode (bt_audio.hpp).
struct BtConfig {
    uint32_t src;               // NBT_BT_SRC: focus-form source (0x3a eSRC_A2DP)
    uint32_t sink;              // NBT_BT_SINK: focus-form sink (0x1 eSNK_SPEAKERS)
    uint32_t conn;              // NBT_BT_CONN: ConnectionMaster connection (0xc7; 0 = focus form)
    int route_delay_ms;         // NBT_BT_ROUTE_DELAY_MS (1500, 0..10000)
    bool refocus;               // NBT_BT_REFOCUS: request the source again when the mic closes (on)
    bool release;               // NBT_BT_RELEASE=1: release the source when the session ends
    const char* sdr_drop;       // NBT_SDR_DROP as given, for the start-up line ("9" when unset)
    SpeechConfig speech;
    BtConfig();
};
BtConfig readBtConfig();

} // namespace nbt

#endif // AUDIO_CONFIG_HPP
