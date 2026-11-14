// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test: the audio settings (audio/config.hpp): defaults, ranges and parsing of each, and the audio mode.
#include "audio/config.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace nbt;

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

static const char* const kVars[] = {
    "NBT_AUDIO_MODE", "NBT_AUDIO_BT",
    "NBT_BROWSER_CONN", "NBT_BROWSER_RATE", "NBT_BROWSER_FORCE_RATE", "NBT_BROWSER_IDLE_S",
    "NBT_BROWSER_PROMPT_IDLE_MS", "NBT_BROWSER_WAIT_MS", "NBT_BROWSER_PREROLL_MS", "NBT_BROWSER_DRIFT",
    "NBT_BROWSER_DRIFT_LOOP", "NBT_BROWSER_PRIO", "NBT_BROWSER_RESAMPLER", "NBT_BROWSER_SPEEX_Q",
    "NBT_BROWSER_RESAMPLER_FILE", "NBT_BROWSER_REFOCUS", "NBT_BROWSER_ASOUND", "NBT_BROWSER_PCM", "NBT_BROWSER_FRAG",
    "NBT_BROWSER_FRAGS", "NBT_DUCK_DB", "NBT_DUCK_ATTACK_MS", "NBT_DUCK_RELEASE_MS", "NBT_DUCK_HOLD_MS",
    "NBT_MEDIA_GAIN_DB", "NBT_GUIDANCE_GAIN_DB", "NBT_SYSTEM_GAIN_DB", "NBT_AA_DEFER_ACKS", "NBT_AA_UNDERFLOW_REPORT",
    "NBT_BT_SRC", "NBT_BT_SINK", "NBT_BT_CONN", "NBT_BT_ROUTE_DELAY_MS", "NBT_BT_REFOCUS", "NBT_BT_RELEASE",
    "NBT_SDR_DROP", "NBT_SPEECH", "NBT_SPEECH_EP", "NBT_SPEECH_SRC", "NBT_SPEECH_SINK",
};

static void clearAll() {
    for (size_t i = 0; i < sizeof(kVars) / sizeof(kVars[0]); ++i) unsetenv(kVars[i]);
}

static bool same(const char* a, const char* b) { return a && b && strcmp(a, b) == 0; }

int main() {
    clearAll();

    // 1. Browser defaults: read from an empty environment, and the struct's own defaults, agree.
    {
        const BrowserConfig c = readBrowserConfig();
        const BrowserConfig d;
        CHECK(c.conn == 0xb8 && d.conn == 0xb8);
        CHECK(c.want_rate == 48000 && d.want_rate == 48000);
        CHECK(c.force_rate == 0 && d.force_rate == 0);
        CHECK(c.idle_s == 10 && d.idle_s == 10);
        CHECK(c.prompt_idle_ms == 1000 && d.prompt_idle_ms == 1000);
        CHECK(c.wait_ms == 30 && d.wait_ms == 30);
        CHECK(c.preroll_ms == 60 && d.preroll_ms == 60);
        CHECK(c.drift && d.drift);
        CHECK(c.drift_pi && d.drift_pi);
        CHECK(c.prio == 15 && d.prio == 15);
        CHECK(!c.resampler_linear && !d.resampler_linear);
        CHECK(c.speex_q == 5 && d.speex_q == 5);
        CHECK(same(c.resampler_file, "/tmp/nbt_resampler") && same(d.resampler_file, "/tmp/nbt_resampler"));
        CHECK(c.refocus && d.refocus);
        CHECK(same(c.shim_path, "/opt/mm/lib/libasound/libasound.so") && same(d.shim_path, c.shim_path));
        CHECK(c.pcm == NULL && d.pcm == NULL);
        CHECK(c.frag == 4096 && d.frag == 4096);
        CHECK(c.frags == 5 && d.frags == 5);
        CHECK(c.duck_db == -12 && d.duck_db == -12);
        CHECK(c.duck_attack_ms == 80 && c.duck_release_ms == 500 && c.duck_hold_ms == 600);
        CHECK(d.duck_attack_ms == 80 && d.duck_release_ms == 500 && d.duck_hold_ms == 600);
        CHECK(c.media_gain_db == 0 && c.guidance_gain_db == 0 && c.system_gain_db == 0);
        CHECK(c.defer_acks && d.defer_acks);
        CHECK(c.underflow_reports && d.underflow_reports);
    }

    // 2. Browser values, ranges and parsing.
    {
        setenv("NBT_BROWSER_CONN", "0", 1);
        setenv("NBT_BROWSER_RATE", "44100", 1);
        setenv("NBT_BROWSER_FORCE_RATE", "48000", 1);
        setenv("NBT_BROWSER_IDLE_S", "0", 1);
        setenv("NBT_BROWSER_PROMPT_IDLE_MS", "250", 1);
        setenv("NBT_BROWSER_WAIT_MS", "45", 1);
        setenv("NBT_BROWSER_PREROLL_MS", "0", 1);
        setenv("NBT_BROWSER_DRIFT", "0", 1);
        setenv("NBT_BROWSER_DRIFT_LOOP", "p", 1);
        setenv("NBT_BROWSER_PRIO", "0", 1);
        setenv("NBT_BROWSER_RESAMPLER", "linear", 1);
        setenv("NBT_BROWSER_SPEEX_Q", "8", 1);
        setenv("NBT_BROWSER_RESAMPLER_FILE", "", 1);
        setenv("NBT_BROWSER_REFOCUS", "off", 1);
        setenv("NBT_BROWSER_ASOUND", "/tmp/shim.so", 1);
        setenv("NBT_BROWSER_PCM", "0:3", 1);
        setenv("NBT_BROWSER_FRAG", "2048", 1);
        setenv("NBT_BROWSER_FRAGS", "8", 1);
        setenv("NBT_DUCK_DB", "-20", 1);
        setenv("NBT_DUCK_ATTACK_MS", "10", 1);
        setenv("NBT_DUCK_RELEASE_MS", "20", 1);
        setenv("NBT_DUCK_HOLD_MS", "30", 1);
        setenv("NBT_MEDIA_GAIN_DB", "-3", 1);
        setenv("NBT_GUIDANCE_GAIN_DB", "4", 1);
        setenv("NBT_SYSTEM_GAIN_DB", "-5", 1);
        setenv("NBT_AA_DEFER_ACKS", "0", 1);
        setenv("NBT_AA_UNDERFLOW_REPORT", "no", 1);
        const BrowserConfig c = readBrowserConfig();
        CHECK(c.conn == 0);
        CHECK(c.want_rate == 44100);
        CHECK(c.force_rate == 48000);
        CHECK(c.idle_s == 0);
        CHECK(c.prompt_idle_ms == 250);
        CHECK(c.wait_ms == 45);
        CHECK(c.preroll_ms == 0);
        CHECK(!c.drift);
        CHECK(!c.drift_pi);
        CHECK(c.prio == 0);
        CHECK(c.resampler_linear);
        CHECK(c.speex_q == 8);
        CHECK(same(c.resampler_file, ""));                        // set but empty: the switch file is off
        CHECK(!c.refocus);
        CHECK(same(c.shim_path, "/tmp/shim.so"));
        CHECK(same(c.pcm, "0:3"));
        CHECK(c.frag == 2048 && c.frags == 8);
        CHECK(c.duck_db == -20 && c.duck_attack_ms == 10 && c.duck_release_ms == 20 && c.duck_hold_ms == 30);
        CHECK(c.media_gain_db == -3 && c.guidance_gain_db == 4 && c.system_gain_db == -5);
        CHECK(!c.defer_acks && !c.underflow_reports);
        clearAll();

        // Clamps: wait 0..60, pre-roll 0..200, priority 0..63, speex quality 0..10.
        setenv("NBT_BROWSER_WAIT_MS", "500", 1);
        setenv("NBT_BROWSER_PREROLL_MS", "999", 1);
        setenv("NBT_BROWSER_PRIO", "99", 1);
        setenv("NBT_BROWSER_SPEEX_Q", "11", 1);
        BrowserConfig h = readBrowserConfig();
        CHECK(h.wait_ms == 60 && h.preroll_ms == 200 && h.prio == 63 && h.speex_q == 10);
        setenv("NBT_BROWSER_WAIT_MS", "-5", 1);
        setenv("NBT_BROWSER_PREROLL_MS", "-1", 1);
        setenv("NBT_BROWSER_PRIO", "-1", 1);
        setenv("NBT_BROWSER_SPEEX_Q", "-1", 1);
        h = readBrowserConfig();
        CHECK(h.wait_ms == 0 && h.preroll_ms == 0 && h.prio == 0 && h.speex_q == 0);
        clearAll();

        // Parsing: hex accepted (strtol base 0), garbage and empty keep the default, a negative force rate is off,
        // only "linear" / "p" switch the resampler / drift loop, an empty shim path is the default.
        setenv("NBT_BROWSER_CONN", "0x31d", 1);
        setenv("NBT_BROWSER_RATE", "fast", 1);
        setenv("NBT_BROWSER_IDLE_S", "", 1);
        setenv("NBT_BROWSER_FORCE_RATE", "-48000", 1);
        setenv("NBT_BROWSER_RESAMPLER", "speex", 1);
        setenv("NBT_BROWSER_DRIFT_LOOP", "pi", 1);
        setenv("NBT_BROWSER_ASOUND", "", 1);
        setenv("NBT_DUCK_DB", "0x10", 1);
        const BrowserConfig g = readBrowserConfig();
        CHECK(g.conn == 0x31d);
        CHECK(g.want_rate == 48000);
        CHECK(g.idle_s == 10);
        CHECK(g.force_rate == 0);
        CHECK(!g.resampler_linear);
        CHECK(g.drift_pi);
        CHECK(same(g.shim_path, "/opt/mm/lib/libasound/libasound.so"));
        CHECK(g.duck_db == 16);
        clearAll();
    }

    // 3. Bluetooth defaults and values.
    {
        const BtConfig c = readBtConfig();
        const BtConfig d;
        CHECK(c.src == 0x3a && c.sink == 0x1 && c.conn == 0xc7);
        CHECK(d.src == 0x3a && d.sink == 0x1 && d.conn == 0xc7);
        CHECK(c.route_delay_ms == 1500 && d.route_delay_ms == 1500);
        CHECK(c.refocus && d.refocus);
        CHECK(!c.release && !d.release);
        CHECK(same(c.sdr_drop, "9") && same(d.sdr_drop, "9"));
        CHECK(!c.speech.enabled && c.speech.endpoint == NULL && c.speech.src == 0 && c.speech.sink == 0x1);

        setenv("NBT_BT_SRC", "0x40", 1);
        setenv("NBT_BT_SINK", "2", 1);
        setenv("NBT_BT_CONN", "0", 1);
        setenv("NBT_BT_ROUTE_DELAY_MS", "250", 1);
        setenv("NBT_BT_REFOCUS", "0", 1);
        setenv("NBT_BT_RELEASE", "1", 1);
        setenv("NBT_SDR_DROP", "9,8", 1);
        setenv("NBT_SPEECH", "1", 1);
        setenv("NBT_SPEECH_EP", "/input_stereo_3", 1);
        setenv("NBT_SPEECH_SRC", "0xba", 1);
        setenv("NBT_SPEECH_SINK", "3", 1);
        const BtConfig v = readBtConfig();
        CHECK(v.src == 0x40 && v.sink == 2 && v.conn == 0);
        CHECK(v.route_delay_ms == 250);
        CHECK(!v.refocus);
        CHECK(v.release);
        CHECK(same(v.sdr_drop, "9,8"));
        CHECK(v.speech.enabled && same(v.speech.endpoint, "/input_stereo_3") && v.speech.src == 0xba &&
              v.speech.sink == 3);

        // NBT_BT_ROUTE_DELAY_MS keeps its atoi parsing, clamped to 0..10000; NBT_BT_RELEASE / NBT_SPEECH need "1".
        setenv("NBT_BT_ROUTE_DELAY_MS", "99999", 1);
        CHECK(readBtConfig().route_delay_ms == 10000);
        setenv("NBT_BT_ROUTE_DELAY_MS", "-3", 1);
        CHECK(readBtConfig().route_delay_ms == 0);
        setenv("NBT_BT_ROUTE_DELAY_MS", "soon", 1);
        CHECK(readBtConfig().route_delay_ms == 0);
        setenv("NBT_BT_ROUTE_DELAY_MS", "0100", 1);
        CHECK(readBtConfig().route_delay_ms == 100);               // decimal, not octal
        setenv("NBT_BT_ROUTE_DELAY_MS", "", 1);
        CHECK(readBtConfig().route_delay_ms == 1500);
        setenv("NBT_BT_RELEASE", "on", 1);
        setenv("NBT_SPEECH", "yes", 1);
        const BtConfig w = readBtConfig();
        CHECK(!w.release && !w.speech.enabled);
        clearAll();
    }

    // 4. The audio mode: NBT_AUDIO_MODE wins over NBT_AUDIO_BT; unknown words fall back to NBT_AUDIO_BT; bt by default.
    {
        CHECK(audioMode() == AUDIO_MODE_BT);                       // default again since v1.8.66
        // Use Native Audio instead of Bluetooth (rhmiapp-45): the flag file picks browser only when neither env is set
        CHECK(audioModeSetting(NULL, -1, true) == AUDIO_MODE_BROWSER);
        CHECK(audioModeSetting("", -1, true) == AUDIO_MODE_BROWSER);
        CHECK(audioModeSetting(NULL, -1, false) == AUDIO_MODE_BT);
        CHECK(audioModeSetting("bt", -1, true) == AUDIO_MODE_BT);
        CHECK(audioModeSetting(NULL, 1, true) == AUDIO_MODE_BT);
        CHECK(audioModeSetting(NULL, 0, true) == AUDIO_MODE_A2I);
        CHECK(audioModeSetting("junk", -1, true) == AUDIO_MODE_BROWSER);
        CHECK(audioModeSetting("a2i", -1, false) == AUDIO_MODE_A2I);
        CHECK(!nativeAudioFlagSet());                              // the host has no flag file
        setenv("NBT_AUDIO_MODE", "browser", 1);
        CHECK(audioMode() == AUDIO_MODE_BROWSER && !btAudioEnabled() && browserAudioEnabled());
        setenv("NBT_AUDIO_MODE", "bt", 1);
        setenv("NBT_AUDIO_BT", "0", 1);
        CHECK(audioMode() == AUDIO_MODE_BT && btAudioEnabled());
        setenv("NBT_AUDIO_MODE", "a2i", 1);
        CHECK(audioMode() == AUDIO_MODE_A2I);
        setenv("NBT_AUDIO_MODE", "nonsense", 1);
        CHECK(audioMode() == AUDIO_MODE_A2I);
        setenv("NBT_AUDIO_BT", "1", 1);
        CHECK(audioMode() == AUDIO_MODE_BT);                       // explicit NBT_AUDIO_BT=1 keeps Bluetooth
        unsetenv("NBT_AUDIO_MODE");
        setenv("NBT_AUDIO_BT", "off", 1);
        CHECK(audioMode() == AUDIO_MODE_A2I);
        clearAll();
        CHECK(strcmp(audioModeName(AUDIO_MODE_BT), "bt") == 0);
        CHECK(strcmp(audioModeName(AUDIO_MODE_BROWSER), "browser") == 0);
        CHECK(strcmp(audioModeName(AUDIO_MODE_A2I), "a2i") == 0);
    }

    // 5. Speech settings: the same in both audio modes; NBT_SPEECH_HOLD_MS clamped to 0..5000.
    {
        const BrowserConfig b = readBrowserConfig();
        CHECK(!b.speech.enabled && b.speech.src == 0 && b.speech.sink == 0x1 && b.speech.hold_ms == 500);
        setenv("NBT_SPEECH", "1", 1);
        setenv("NBT_SPEECH_EP", "/input_mono_0", 1);
        setenv("NBT_SPEECH_SRC", "0x1a", 1);
        const BrowserConfig b2 = readBrowserConfig();
        const BtConfig t2 = readBtConfig();
        CHECK(b2.speech.enabled && same(b2.speech.endpoint, "/input_mono_0") && b2.speech.src == 0x1a);
        CHECK(t2.speech.enabled && same(t2.speech.endpoint, "/input_mono_0") && t2.speech.src == 0x1a);
        setenv("NBT_SPEECH_HOLD_MS", "9000", 1);
        CHECK(readBrowserConfig().speech.hold_ms == 5000 && readBtConfig().speech.hold_ms == 5000);
        setenv("NBT_SPEECH_HOLD_MS", "-5", 1);
        CHECK(readBrowserConfig().speech.hold_ms == 0);
        setenv("NBT_SPEECH_HOLD_MS", "abc", 1);
        CHECK(readBrowserConfig().speech.hold_ms == 500);
        clearAll();
    }

    if (fails) { printf("test_audio_config: %d failure(s)\n", fails); return 1; }
    printf("test_audio_config OK\n");
    return 0;
}
