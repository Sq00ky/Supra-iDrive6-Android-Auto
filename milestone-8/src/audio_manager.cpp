// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "audio_manager.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace nbt {

namespace {

const size_t   kRingBytes      = 96000;   // ~500 ms of 48 kHz stereo 16-bit
const uint32_t kAaMediaRate    = 48000;   // AA media: 48 kHz stereo (DHU channel 9)
const uint32_t kAaSpeechRate   = 16000;   // AA speech: 16 kHz mono  (DHU channel 8, config 0)
const uint32_t kMicRate        = 16000;   // mic: 16 kHz mono (DHU channel 14)
const uint32_t kEndpointRate   = 44100;   // A2I open rate car-verified by Fork C v1.4.x
// DHU service discovery (include/dhu_sdp.h): ch 8 = SPEECH (we answer config 0 = 16 kHz mono),
// ch 9 = MEDIA 48 kHz stereo, ch 14 = microphone source 16 kHz mono.
enum { AA_CH_AUDIO_SPEECH = 8, AA_CH_AUDIO_MEDIA = 9 };

uint32_t envU32(const char* name, uint32_t def) {
    const char* v = getenv(name);
    if (!v || !v[0]) return def;
    char* end = NULL;
    unsigned long parsed = strtoul(v, &end, 0);
    if (end == v) return def;
    return static_cast<uint32_t>(parsed);
}

} // namespace

AudioManager::AudioManager()
    : stage_(STAGE_OPEN_ONLY),
      media_rate_(kEndpointRate),
      route_media_(0xb9),
      route_nav_(0xba),
      route_sink_(0x1),
      route_mode_(0),
      media_("AudioMedia", kRingBytes),
      nav_("AudioNav", kRingBytes),
      mic_("AudioMic"),
      media_routed_(false),
      nav_routed_(false),
      started_(false),
      mic_callback_(NULL),
      mic_user_data_(NULL) {
}

AudioManager::~AudioManager() {
    stop();
}

bool AudioManager::openPlayback(A2IPlaybackStream& stream, const char* endpoint, const char* fallback) {
    // Preferred rate first, then the classic 44.1 kHz in case 48 kHz is rejected.
    uint32_t rates[2] = { media_rate_, A2I_SAMPLE_RATE_44K };
    for (int i = 0; i < 2; ++i) {
        if (i == 1 && rates[1] == rates[0]) break;

        A2IStreamConfig cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.endpoint          = endpoint;
        cfg.fallback_endpoint = fallback;
        cfg.sample_rate       = rates[i];
        cfg.channels          = A2I_CHANNELS_STEREO;
        cfg.buffer_size       = A2I_BUFFER_SIZE_STEREO;
        cfg.cookie_primary    = A2I_COOKIE_LEGACY_PLAYBACK;   // known to open on this car
        cfg.cookie_alternate  = A2I_COOKIE_FACTORY;           // what the stock CarPlay code passes
        cfg.timeout_ms        = 2000;
        if (stream.open(cfg)) return true;
    }
    return false;
}

bool AudioManager::init() {
    stage_ = static_cast<int>(envU32("NBT_AUDIO_STAGE", STAGE_OPEN_ONLY));
    if (stage_ < STAGE_OPEN_ONLY) stage_ = STAGE_OPEN_ONLY;
    if (stage_ > STAGE_ROUTE) stage_ = STAGE_ROUTE;
    media_rate_  = envU32("NBT_MEDIA_RATE", kEndpointRate);
    route_mode_  = static_cast<int>(envU32("NBT_ROUTE_MODE", 0));
    // Mode 0 (AM focus): eSRC codes. Mode 1 (CM build): ConnIds.
    route_media_ = envU32("NBT_ROUTE_MEDIA", route_mode_ ? 0x31d : 0xb9);  // A4A_CARPLAY_ENT_MEDIA
    route_nav_   = envU32("NBT_ROUTE_NAV",   route_mode_ ? 0x31e : 0xba);  // A4A_CARPLAY_INT_MIX
    route_sink_  = envU32("NBT_ROUTE_SINK", 0x1);                          // eSNK_SPEAKERS

    static const char* kStageNames[4] = {
        "0: open endpoints only",
        "1: factory prime/Start loop, silence only",
        "2: AA PCM playback + microphone",
        "3: stage 2 + audioconn route build/remove"
    };
    printf("[AudioManager] NBT_AUDIO_STAGE=%d (%s)\n", stage_, kStageNames[stage_]);
    printf("[AudioManager] Route mode %d (%s), media=0x%x nav=0x%x sink=0x%x\n", route_mode_, route_mode_ ? "CM connId" : "AM focus src/snk", route_media_, route_nav_, route_sink_);
    printf("[AudioManager] Opening A2I playback endpoints (preferred rate %u Hz)...\n", media_rate_);

    bool ok = true;
    if (!openPlayback(media_, A2I_ENDPOINT_MEDIA_MAIN, A2I_ENDPOINT_MEDIA_FALLBACK)) {
        printf("[AudioManager] Error: could not open the media endpoint.\n");
        ok = false;
    } else {
        media_rs_.configure(kAaMediaRate, 2, media_.sampleRate(), 2);
    }

    if (!openPlayback(nav_, A2I_ENDPOINT_NAV_ALT, NULL)) {
        printf("[AudioManager] Warning: could not open the navigation endpoint.\n");
    } else {
        // AA guidance/speech is 16 kHz mono; the AirPlayAlt endpoint is stereo.
        speech_rs_.configure(kAaSpeechRate, 1, nav_.sampleRate(), 2);
    }

    if (stage_ >= STAGE_ROUTE) route_.configure();
    return ok;
}

void AudioManager::start() {
    if (started_) return;
    started_ = true;

    if (stage_ < STAGE_SILENCE) {
        printf("[AudioManager] Stage 0: endpoints held open, workers not started.\n");
        return;
    }
    const bool feed = (stage_ >= STAGE_PLAYBACK);
    if (media_.isOpen()) media_.start(feed);
    if (nav_.isOpen())   nav_.start(feed);
    printf("[AudioManager] Playback workers started (%s).\n", feed ? "AA PCM" : "silence only");
}

void AudioManager::stop() {
    if (!started_ && !media_.isOpen() && !nav_.isOpen() && !mic_.isOpen()) return;
    started_ = false;

    onSessionEnded();
    stopMic();
    media_.stop();
    nav_.stop();
    printf("[AudioManager] Audio streams stopped.\n");
}

void AudioManager::onAaAudio(uint8_t aa_channel, const uint8_t* pcm, size_t len) {
    if (stage_ < STAGE_PLAYBACK || !pcm || len == 0) return;

    scratch_.clear();
    if (aa_channel == AA_CH_AUDIO_MEDIA) {
        if (!media_.isRunning()) return;
        media_rs_.processBytes(pcm, len, scratch_);
        if (!scratch_.empty()) media_.write(&scratch_[0], scratch_.size());
    } else if (aa_channel == AA_CH_AUDIO_SPEECH) {
        if (!nav_.isRunning()) return;
        speech_rs_.processBytes(pcm, len, scratch_);
        if (!scratch_.empty()) nav_.write(&scratch_[0], scratch_.size());
    }
}

void AudioManager::onAaStreamState(uint8_t aa_channel, bool started) {
    if (aa_channel != AA_CH_AUDIO_MEDIA && aa_channel != AA_CH_AUDIO_SPEECH) return;
    const bool media = (aa_channel == AA_CH_AUDIO_MEDIA);
    printf("[AudioManager] AA %s stream %s.\n", media ? "media" : "speech", started ? "started" : "stopped");

    if (media) media_rs_.reset(); else speech_rs_.reset();
    if (!started) {
        if (media) media_.flush(); else nav_.flush();
    }

    if (stage_ < STAGE_ROUTE || !route_.available()) return;

    const uint32_t conn = media ? route_media_ : route_nav_;
    bool& routed = media ? media_routed_ : nav_routed_;
    if (started && !routed) {
        routed = route_mode_ ? route_.build(conn) : route_.buildFocus(conn, route_sink_);
    } else if (!started && routed) {
        if (route_mode_) route_.remove(conn); else route_.removeFocus(conn, route_sink_);
        routed = false;
    }
}

void AudioManager::onSessionEnded() {
    media_.flush();
    nav_.flush();
    if (stage_ >= STAGE_ROUTE && route_.available()) {
        if (media_routed_) {
            if (route_mode_) route_.remove(route_media_); else route_.removeFocus(route_media_, route_sink_);
            media_routed_ = false;
        }
        if (nav_routed_) {
            if (route_mode_) route_.remove(route_nav_); else route_.removeFocus(route_nav_, route_sink_);
            nav_routed_ = false;
        }
    }
    stopMic();
}

void AudioManager::setMicCallback(MicDataCallback callback, void* user_data) {
    mic_callback_ = callback;
    mic_user_data_ = user_data;
}

void AudioManager::micThunk(const uint8_t* pcm, size_t size, void* self) {
    AudioManager* am = reinterpret_cast<AudioManager*>(self);
    if (am->mic_callback_) am->mic_callback_(pcm, size, am->mic_user_data_);
}

bool AudioManager::startMic() {
    if (stage_ < STAGE_PLAYBACK || !mic_callback_) return false;
    if (mic_.isRunning()) return true;

    if (!mic_.isOpen()) {
        A2IStreamConfig cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.endpoint          = A2I_ENDPOINT_MIC_UL;
        cfg.fallback_endpoint = NULL;
        cfg.sample_rate       = kMicRate;
        cfg.channels          = A2I_CHANNELS_MONO;
        cfg.buffer_size       = A2I_BUFFER_SIZE_MONO;
        cfg.cookie_primary    = A2I_COOKIE_LEGACY_CAPTURE;
        cfg.cookie_alternate  = A2I_COOKIE_FACTORY;
        cfg.timeout_ms        = 2000;
        if (!mic_.open(cfg)) {
            printf("[AudioManager] Error: could not open the microphone endpoint.\n");
            return false;
        }
    }
    return mic_.start(&AudioManager::micThunk, this);
}

void AudioManager::stopMic() {
    if (mic_.isOpen() || mic_.isRunning()) mic_.stop();
}

} // namespace nbt
