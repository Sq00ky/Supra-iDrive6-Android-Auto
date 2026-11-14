// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "audio/config.hpp"

namespace nbt {

namespace {

// A whole number (strtol base 0); unset or unparsable = def. Not clamped (envRange clamps).
int envInt(const char* name, int def) {
    const char* e = getenv(name);
    if (!e || !*e) return def;
    char* end = NULL;
    long v = strtol(e, &end, 0);
    return (end && end != e) ? (int)v : def;
}

} // namespace

BrowserConfig::BrowserConfig()
    : conn(0xb8), want_rate(48000), force_rate(0), idle_s(10), prompt_idle_ms(1000), wait_ms(30), preroll_ms(60),
      drift(true), drift_pi(true), prio(15), resampler_linear(false), speex_q(5), resampler_file("/tmp/nbt_resampler"),
      refocus(true), shim_path("/opt/mm/lib/libasound/libasound.so"), pcm(NULL), frag(4096), frags(5), duck_db(-12),
      duck_attack_ms(80), duck_release_ms(500), duck_hold_ms(600), media_gain_db(0), guidance_gain_db(0),
      system_gain_db(0), defer_acks(true), underflow_reports(true) {}

BrowserConfig readBrowserConfig() {
    BrowserConfig c;
    c.conn = (uint32_t)envHex("NBT_BROWSER_CONN", 0xb8);
    c.want_rate = (uint32_t)envInt("NBT_BROWSER_RATE", 48000);
    {
        const int fr = envInt("NBT_BROWSER_FORCE_RATE", 0);
        c.force_rate = fr > 0 ? (uint32_t)fr : 0u;
    }
    c.idle_s = envInt("NBT_BROWSER_IDLE_S", 10);
    c.prompt_idle_ms = envInt("NBT_BROWSER_PROMPT_IDLE_MS", 1000);
    {
        // Above ~60 ms a wait could outlast what the device still holds when the mixer wakes (~92 ms).
        int w = envInt("NBT_BROWSER_WAIT_MS", 30);
        c.wait_ms = w < 0 ? 0u : (w > 60 ? 60u : (uint32_t)w);
    }
    // Pre-roll: a started stream plays once this much is queued (or its first audio has waited this long). The phone
    // sends in real time, so without it a stream has no cushion and every late frame is a hole.
    c.preroll_ms = (uint32_t)envRange("NBT_BROWSER_PREROLL_MS", 60, 0, 200);
    c.drift = envFlag("NBT_BROWSER_DRIFT") != 0;   // unset (-1) or on (1): correct media clock drift
    {
        // Which loop sets the trim: pi (default: holds the queue on target, its integral reads the phone's clock
        // error) or p (the proportional trim alone, ~0.1 ms off target per ppm of drift).
        const char* e = getenv("NBT_BROWSER_DRIFT_LOOP");
        c.drift_pi = !(e && strcmp(e, "p") == 0);
    }
    // Mixer thread priority: 15 by default, above the default 10 that the HMI and video threads compete at, below
    // the 21 of the USB reader. 0 = leave it alone. The car's own audio threads should stay above it (check with pidin).
    c.prio = (int)envRange("NBT_BROWSER_PRIO", 15, 0, 63);
    // Resampler: speexdsp by default (flat to ~19.5 kHz, no aliasing, no treble wobble under the drift trim);
    // NBT_BROWSER_RESAMPLER=linear goes back to linear interpolation.
    {
        const char* e = getenv("NBT_BROWSER_RESAMPLER");
        c.resampler_linear = (e && strcmp(e, "linear") == 0);
    }
    c.speex_q = (int)envRange("NBT_BROWSER_SPEEX_Q", 5, 0, 10);
    c.resampler_file = getenv("NBT_BROWSER_RESAMPLER_FILE");
    if (!c.resampler_file) c.resampler_file = "/tmp/nbt_resampler";
    c.refocus = envFlag("NBT_BROWSER_REFOCUS") != 0;
    c.shim_path = getenv("NBT_BROWSER_ASOUND");
    if (!c.shim_path || !*c.shim_path) c.shim_path = "/opt/mm/lib/libasound/libasound.so";
    c.pcm = getenv("NBT_BROWSER_PCM");
    c.frag = envInt("NBT_BROWSER_FRAG", 4096);
    c.frags = envInt("NBT_BROWSER_FRAGS", 5);
    c.duck_db = envInt("NBT_DUCK_DB", -12);
    c.duck_attack_ms = (uint32_t)envInt("NBT_DUCK_ATTACK_MS", 80);
    c.duck_release_ms = (uint32_t)envInt("NBT_DUCK_RELEASE_MS", 500);
    c.duck_hold_ms = (uint32_t)envInt("NBT_DUCK_HOLD_MS", 600);
    c.media_gain_db = envInt("NBT_MEDIA_GAIN_DB", 0);
    c.guidance_gain_db = envInt("NBT_GUIDANCE_GAIN_DB", 0);
    c.system_gain_db = envInt("NBT_SYSTEM_GAIN_DB", 0);
    // Deferred acks (default on): an AA audio frame is acked once the mixer has taken it, so the phone is paced by the
    // car's clock instead of running ahead on its own. Underflow reports (default on): tell the phone when a stream
    // ran dry (0x800b).
    c.defer_acks = envFlag("NBT_AA_DEFER_ACKS") != 0;
    c.underflow_reports = envFlag("NBT_AA_UNDERFLOW_REPORT") != 0;
    c.speech = readSpeechConfig();
    return c;
}

SpeechConfig::SpeechConfig() : enabled(false), endpoint(NULL), src(0), sink(0x1), hold_ms(500) {}

SpeechConfig readSpeechConfig() {
    SpeechConfig c;
    const char* e = getenv("NBT_SPEECH");
    c.enabled = e && strcmp(e, "1") == 0;
    c.endpoint = getenv("NBT_SPEECH_EP");
    c.src = (uint32_t)envHex("NBT_SPEECH_SRC", 0);
    c.sink = (uint32_t)envHex("NBT_SPEECH_SINK", 0x1);
    c.hold_ms = (int)envRange("NBT_SPEECH_HOLD_MS", 500, 0, 5000);
    return c;
}

BtConfig::BtConfig()
    : src(0x3a), sink(0x1), conn(0xc7), route_delay_ms(1500), refocus(true), release(false), sdr_drop("9") {}

BtConfig readBtConfig() {
    BtConfig c;
    c.src = (uint32_t)envHex("NBT_BT_SRC", 0x3a);
    c.sink = (uint32_t)envHex("NBT_BT_SINK", 0x1);
    c.conn = (uint32_t)envHex("NBT_BT_CONN", 0xc7);
    // NBT_BT_ROUTE_DELAY_MS (default 1500, 0 = at once): wait this long after the session starts before running
    // audioconn. A phone that says ByeBye right after the ServiceDiscovery and reconnects every ~3 s (tester bootloop
    // log, 2026-10-29) then never starts an audioconn at all: onSessionEnded() cancels the request while it waits.
    // Parsed with atoi (not envRange), as it always was: garbage reads as 0, a leading 0 is not octal.
    {
        const char* e = getenv("NBT_BT_ROUTE_DELAY_MS");
        if (!e || !*e) {
            c.route_delay_ms = 1500;
        } else {
            int v = atoi(e);
            c.route_delay_ms = v < 0 ? 0 : (v > 10000 ? 10000 : v);
        }
    }
    // Default ON since v1.7.7 (car-verified nbt-aad-22-2-2: with it the Assistant's reply is heard).
    // NBT_BT_REFOCUS=0 / off / no / false disables.
    c.refocus = envFlag("NBT_BT_REFOCUS") != 0;   // unset stays on
    c.release = getenv("NBT_BT_RELEASE") && strcmp(getenv("NBT_BT_RELEASE"), "1") == 0;
    c.sdr_drop = getenv("NBT_SDR_DROP") ? getenv("NBT_SDR_DROP") : "9";
    c.speech = readSpeechConfig();
    return c;
}

} // namespace nbt
