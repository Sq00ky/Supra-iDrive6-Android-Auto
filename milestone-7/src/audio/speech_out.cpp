// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "audio/speech_out.hpp"
#include "audio/focus_volume_quiet.hpp"
#include "audio/speech_format.hpp"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

namespace nbt {

namespace {

FocusVolumeQuiet g_focusQuiet;
const uint32_t kStopWaitMs = 2000;   // stop(): longest wait for the final focus remove

uint64_t nowMs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

} // namespace

FocusVolumeQuiet& focusVolumeQuiet() { return g_focusQuiet; }

SpeechOut::SpeechOut(AudioRoute& route)
    : route_(route), endpoint_(NULL), speech_(NULL), ready_(false), src_(0), sink_(0x1), frames_(0), bytes_(0),
      peak_(0), focus_(*this), prompts_(0), tick_(), tick_valid_(false), tick_stop_(false) {
    pthread_mutex_init(&lock_, NULL);
    const SpeechInput in = speechInputFormat(0, 0);
    prompt_rate_ = in.rate;
    prompt_ch_ = in.channels;
}

SpeechOut::~SpeechOut() {
    stop();
    pthread_mutex_destroy(&lock_);
}

void* SpeechOut::openThreadThunk(void* self) {
    static_cast<SpeechOut*>(self)->openThread();
    return NULL;
}

void* SpeechOut::tickThreadThunk(void* self) {
    static_cast<SpeechOut*>(self)->tickThread();
    return NULL;
}

void SpeechOut::grantedThunk(void* self) { static_cast<SpeechOut*>(self)->granted(); }
void SpeechOut::removeDoneThunk(void* self, bool ran) { static_cast<SpeechOut*>(self)->removeDone(ran); }

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
    const SpeechInput in = speechInputFormat(0, 0);
    rs_.configure(in.rate, in.channels, s->sampleRate(), s->channels());
    if (!s->start(src_ == 0)) {   // with a focus, audio waits in the ring until the grant
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
    NavPromptPolicy::Config pc;
    pc.hold_ms = cfg.hold_ms > 0 ? (uint32_t)cfg.hold_ms : 0;
    if (src_) {
        tick_valid_ = pthread_create(&tick_, NULL, &tickThreadThunk, this) == 0;
        if (!tick_valid_) {
            printf("[Speech] could not start the focus timer: playing without the nav prompt focus\n");
            src_ = 0;   // a focus nobody releases would keep the radio ducked
        }
    }
    pthread_mutex_lock(&lock_);   // the tick thread may already run
    focus_.configure(pc, src_ && route_.available());
    pthread_mutex_unlock(&lock_);
    printf("[Speech] NBT_SPEECH=1: opening the speech endpoint in the background (focus hold %u ms)\n", pc.hold_ms);
    pthread_t t;
    if (pthread_create(&t, NULL, &openThreadThunk, this) == 0) pthread_detach(t);
    else printf("[Speech] could not start the open thread\n");
}

bool SpeechOut::takesPrompt(uint64_t now) const {
    if (!ready_) return false;
    pthread_mutex_lock(&lock_);
    const bool ok = focus_.takesPrompt(now);
    pthread_mutex_unlock(&lock_);
    return ok;
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
    if (buf_.empty()) return;
    // Under the lock, so nothing lands in the ring after a grant timeout flushed it.
    pthread_mutex_lock(&lock_);
    if (focus_.playsFrames()) speech_->write(&buf_[0], buf_.size());
    pthread_mutex_unlock(&lock_);
}

void SpeechOut::state(bool started, uint32_t rate, uint32_t channels) {
    if (!ready_ || !speech_) return;
    const uint64_t now = nowMs();
    if (!started) {
        pthread_mutex_lock(&lock_);
        focus_.promptStopped();
        const uint32_t n = prompts_;
        const uint64_t bytes_per_s = (uint64_t)prompt_rate_ * prompt_ch_ * 2u;
        pthread_mutex_unlock(&lock_);
        printf("[Speech] prompt #%u ended: %u frames, %u ms of audio, peak %d, %u bytes queued, endpoint writes %u "
               "underruns %u\n", n, frames_, (unsigned)(bytes_ * 1000ull / bytes_per_s), peak_,
               (unsigned)speech_->queuedBytes(), speech_->writes(), speech_->underruns());
        return;
    }

    const SpeechInput in = speechInputFormat(rate, channels);
    rs_.configure(in.rate, in.channels, speech_->sampleRate(), speech_->channels());
    rs_.reset();
    frames_ = 0;
    bytes_ = 0;
    peak_ = 0;

    pthread_mutex_lock(&lock_);
    const uint32_t n = ++prompts_;
    prompt_rate_ = in.rate;
    prompt_ch_ = in.channels;
    const char* how = focus_.promptStarted(now);
    pthread_mutex_unlock(&lock_);
    if (!how) how = src_ ? "no focus (audioconn not found)" : "no focus (NBT_SPEECH_SRC=0)";
    printf("[Speech] prompt #%u started: %u Hz x%u, %s\n", n, in.rate, in.channels, how);
}

bool SpeechOut::active() const {
    if (!ready_ || !speech_) return false;
    pthread_mutex_lock(&lock_);
    const bool a = focus_.active();
    pthread_mutex_unlock(&lock_);
    return a;
}

void SpeechOut::granted() {   // audioconn worker thread
    const uint64_t now = nowMs();
    pthread_mutex_lock(&lock_);
    const NavPromptPolicy::State st = focus_.granted(now);
    const uint32_t n = prompts_;
    const unsigned long long after = focus_.grantAfterMs();
    const unsigned queued = (unsigned)speech_->queuedBytes();
    pthread_mutex_unlock(&lock_);
    if (st == NavPromptPolicy::BUILDING)
        printf("[Speech] prompt #%u: focus granted after %llu ms, %u bytes queued\n", n, after, queued);
    else if (st != NavPromptPolicy::ROUTED)
        printf("[Speech] focus granted after it was given up: releasing it again\n");
}

void SpeechOut::removeDone(bool ran) {   // audioconn worker thread, whatever the remove's outcome
    pthread_mutex_lock(&lock_);
    focus_.removeDone(nowMs(), ran);
    pthread_mutex_unlock(&lock_);
}

bool SpeechOut::queueBuild() {
    // AudioRoute only spawns a worker; the grant callback never runs inside this call. No retries: they would outlast
    // the 2 s grant timeout.
    return route_.buildFocus(src_, sink_, &grantedThunk, this, 0, false);
}

bool SpeechOut::queueRemove() { return route_.removeFocus(src_, sink_, &removeDoneThunk, this); }
void SpeechOut::cancelPending() { route_.cancelPending(); }
void SpeechOut::setFeed(bool on) { speech_->setFeedEnabled(on); }
void SpeechOut::flush() { speech_->flush(); }
size_t SpeechOut::queuedBytes() const { return speech_->queuedBytes(); }
void SpeechOut::setFocusBusy(bool busy, uint64_t now) { focusVolumeQuiet().setBusy(busy, now); }

void SpeechOut::tickThread() {
    const NavPromptPolicy::Config defaults;
    const uint32_t backoff_s = defaults.backoff_ms / 1000u;
    while (!tick_stop_) {
        usleep(100000);
        if (!ready_) continue;
        pthread_mutex_lock(&lock_);
        const uint64_t now = nowMs();   // under the lock: no other thread's timestamp is newer
        const SpeechFocus::Tick t = focus_.tick(now);
        const uint32_t n = prompts_;
        const uint32_t rate = prompt_rate_;
        const uint32_t ch = prompt_ch_;
        const unsigned long long after = focus_.grantAfterMs();
        pthread_mutex_unlock(&lock_);
        if (t.action == NavPromptPolicy::GRANT_TIMEOUT)
            printf("[Speech] prompt #%u: focus not granted after %llu ms: dropped, no focus requests for %u s\n", n,
                   (unsigned long long)t.waited_ms, backoff_s);
        else if (t.action == NavPromptPolicy::RELEASE)
            printf("[Speech] prompt #%u: %u Hz x%u, focus granted after %llu ms, released %llu ms after the last "
                   "audio\n", n, rate, ch, after, (unsigned long long)t.idle_ms);
        else if (t.action == NavPromptPolicy::RELEASE_TIMEOUT)
            printf("[Speech] focus release not done after %u ms: going on\n", defaults.release_timeout_ms);
        if (t.rebuild) printf("[Speech] prompt #%u: focus released, %s\n", n, t.rebuild);
    }
}

void SpeechOut::onSessionEnded() {
    if (!ready_ || !speech_) return;
    pthread_mutex_lock(&lock_);
    const bool releasing = focus_.sessionEnded(nowMs());
    pthread_mutex_unlock(&lock_);
    if (releasing) printf("[Speech] session ended: releasing the nav prompt focus\n");
}

bool SpeechOut::stop() {
    if (tick_valid_) {
        tick_stop_ = true;
        pthread_join(tick_, NULL);
        tick_valid_ = false;
    }
    if (!ready_ || !speech_) return false;
    const uint64_t deadline = nowMs() + kStopWaitMs;
    pthread_mutex_lock(&lock_);
    const bool cancelled = focus_.beginStop(nowMs());
    // The process exits right after: wait (bounded) for the focus to be given back.
    while (focus_.stopStep() && nowMs() < deadline) {
        pthread_mutex_unlock(&lock_);
        usleep(20000);
        pthread_mutex_lock(&lock_);
    }
    const SpeechFocus::Stop s = focus_.endStop(nowMs());
    pthread_mutex_unlock(&lock_);
    if (s.sent) printf("[Speech] shutdown: releasing the nav prompt focus\n");
    if (s.held) printf("[Speech] shutdown: the nav prompt focus may still be held (no remove ran within %u ms)\n",
                       kStopWaitMs);
    speech_->stop();
    return cancelled;
}

} // namespace nbt
