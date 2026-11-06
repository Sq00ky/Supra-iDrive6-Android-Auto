// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "audio/speech_out.hpp"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace nbt {

SpeechOut::SpeechOut(AudioRoute& route)
    : route_(route), endpoint_(NULL), speech_(NULL), ready_(false), src_(0), sink_(0x1), routed_(false), frames_(0), bytes_(0),
      peak_(0), prompts_(0) {}

void* SpeechOut::openThreadThunk(void* self) {
    static_cast<SpeechOut*>(self)->openThread();
    return NULL;
}

void SpeechOut::openThread() {
    const char* ep = endpoint_;
    if (!ep || !*ep) ep = A2I_ENDPOINT_NAV_ALT;
    // Ring holds ~12 s at 44.1 kHz stereo: the phone sends a prompt faster than real time (only max_unacked
    // limits it) and the ring drops the OLDEST bytes when full, which would cut the start of a long prompt.
    A2IPlaybackStream* s = new A2IPlaybackStream("Speech", 44100u * 4u * 12u);
    uint32_t rates[2] = { A2I_SAMPLE_RATE_44K, A2I_SAMPLE_RATE_48K };
    bool ok = false;
    for (int i = 0; i < 2 && !ok; ++i) {
        A2IStreamConfig cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.endpoint          = ep;
        cfg.fallback_endpoint = NULL;
        cfg.sample_rate       = rates[i];
        cfg.channels          = A2I_CHANNELS_STEREO;
        cfg.buffer_size       = A2I_BUFFER_SIZE_STEREO;
        cfg.cookie_primary    = A2I_COOKIE_LEGACY_PLAYBACK;
        cfg.cookie_alternate  = A2I_COOKIE_FACTORY;
        cfg.timeout_ms        = 2000;
        ok = s->open(cfg);
    }
    if (!ok) {
        printf("[Speech] could not open %s: spoken prompts stay off\n", ep);
        delete s;
        return;
    }
    rs_.configure(16000, 1, s->sampleRate(), s->channels());
    if (!s->start(true)) {
        printf("[Speech] playback worker did not start on %s: spoken prompts stay off\n", ep);
        return;   // keep the handle (no close mid-run), just never feed it
    }
    speech_ = s;
    __sync_synchronize();   // resampler + pointer visible before the session thread sees ready
    ready_ = true;
    printf("[Speech] ready: %s %u Hz x%u, route %s 0x%x -> 0x%x\n", s->endpoint() ? s->endpoint() : ep,
           s->sampleRate(), s->channels(), src_ ? "audioconn" : "off", src_, sink_);
}

void SpeechOut::init(const SpeechConfig& cfg) {
    if (!cfg.enabled) {
        printf("[Speech] spoken nav prompts: off (NBT_SPEECH=1 enables)\n");
        return;
    }
    src_ = cfg.src;
    sink_ = cfg.sink;
    endpoint_ = cfg.endpoint;
    printf("[Speech] NBT_SPEECH=1: opening the speech endpoint in the background\n");
    pthread_t t;
    if (pthread_create(&t, NULL, &openThreadThunk, this) == 0) pthread_detach(t);
    else printf("[Speech] could not start the open thread\n");
}

void SpeechOut::frame(const uint8_t* pcm, size_t len) {
    if (!ready_ || !speech_) return;
    for (size_t i = 0; i + 1 < len; i += 2) {
        int v = (int16_t)(pcm[i] | (pcm[i + 1] << 8));
        if (v < 0) v = -v;
        if (v > peak_) peak_ = v;
    }
    ++frames_;
    bytes_ += (uint32_t)len;
    buf_.clear();
    rs_.processBytes(pcm, len, buf_);
    if (!buf_.empty()) speech_->write(&buf_[0], buf_.size());
}

void SpeechOut::state(bool started) {
    if (!ready_ || !speech_) return;
    if (started) {
        ++prompts_;
        frames_ = 0;
        bytes_ = 0;
        peak_ = 0;
        rs_.reset();
        printf("[Speech] prompt #%u started\n", prompts_);
        if (src_ && !routed_ && route_.available())
            routed_ = route_.buildFocus(src_, sink_);
    } else {
        // No flush: the queued tail still plays out (the STOP arrives right after the last frame).
        printf("[Speech] prompt #%u ended: %u frames, %u ms of audio, peak %d, endpoint writes %u underruns %u\n",
               prompts_, frames_, bytes_ / 32u, peak_, speech_->writes(),
               speech_->underruns());
        if (routed_) {
            route_.removeFocus(src_, sink_);
            routed_ = false;
        }
    }
}

void SpeechOut::onSessionEnded() {
    if (routed_) {   // session died mid-prompt: give the speech source back
        route_.removeFocus(src_, sink_);
        routed_ = false;
    }
    if (ready_ && speech_) speech_->flush();
}

void SpeechOut::stop() {
    if (ready_ && speech_) speech_->stop();
}

} // namespace nbt
