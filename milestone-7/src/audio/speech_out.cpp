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
const uint32_t kMaxOpenFails = 3;    // failed opens in a row before browser mode keeps prompts in its mix

uint64_t nowMs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

} // namespace

FocusVolumeQuiet& focusVolumeQuiet() { return g_focusQuiet; }

SpeechOut::SpeechOut(AudioRoute& route)
    : route_(route), endpoint_(NULL), speech_(NULL), ready_(false), src_(0), sink_(0x1), ep_rate_(0), ep_ch_(0),
      frames_(0), bytes_(0), peak_(0), focus_(*this), prompts_(0), ep_want_(false), ep_open_(false),
      ep_failed_(false), ep_fails_(0), ep_stop_(false), ep_thread_(), ep_valid_(false), tick_(), tick_valid_(false),
      tick_stop_(false) {
    pthread_mutex_init(&lock_, NULL);
    pthread_cond_init(&ep_cond_, NULL);
    memset(&ep_cfg_, 0, sizeof(ep_cfg_));
    const SpeechInput in = speechInputFormat(0, 0);
    prompt_rate_ = in.rate;
    prompt_ch_ = in.channels;
}

SpeechOut::~SpeechOut() {
    stop();
    delete speech_;
    pthread_cond_destroy(&ep_cond_);
    pthread_mutex_destroy(&lock_);
}

void* SpeechOut::endpointThreadThunk(void* self) {
    static_cast<SpeechOut*>(self)->endpointThread();
    return NULL;
}

void* SpeechOut::tickThreadThunk(void* self) {
    static_cast<SpeechOut*>(self)->tickThread();
    return NULL;
}

void SpeechOut::grantedThunk(void* self) { static_cast<SpeechOut*>(self)->granted(); }
void SpeechOut::removeDoneThunk(void* self, bool ran) { static_cast<SpeechOut*>(self)->removeDone(ran); }

bool SpeechOut::openFirst() {
    const char* ep = endpoint_;
    if (!ep || !*ep) ep = A2I_ENDPOINT_NAV_ALT;
    const uint32_t ch = speechEndpointChannels(ep);
    uint32_t rates[2] = { A2I_SAMPLE_RATE_44K, A2I_SAMPLE_RATE_48K };
    A2IStreamConfig cfg;
    bool ok = false;
    for (int i = 0; i < 2 && !ok; ++i) {
        memset(&cfg, 0, sizeof(cfg));
        cfg.endpoint          = ep;
        cfg.fallback_endpoint = NULL;
        cfg.sample_rate       = rates[i];
        cfg.channels          = ch;
        cfg.buffer_size       = ch == A2I_CHANNELS_MONO ? A2I_BUFFER_SIZE_MONO : A2I_BUFFER_SIZE_STEREO;
        cfg.cookie_primary    = A2I_COOKIE_LEGACY_PLAYBACK;
        cfg.cookie_alternate  = A2I_COOKIE_FACTORY;
        cfg.timeout_ms        = 2000;
        ok = speech_->open(cfg);
    }
    if (!ok) {
        printf("[Speech] could not open %s: spoken prompts stay off\n", ep);
        return false;
    }
    // Every later open asks for exactly what worked.
    if (speech_->endpoint()) cfg.endpoint = speech_->endpoint();
    cfg.cookie_primary = speech_->cookie();
    cfg.cookie_alternate = A2I_COOKIE_NONE;
    ep_cfg_ = cfg;
    ep_rate_ = speech_->sampleRate();
    ep_ch_ = speech_->channels();
    const SpeechInput in = speechInputFormat(0, 0);
    rs_.configure(in.rate, in.channels, ep_rate_, ep_ch_);
    speech_->stop();   // only the format was wanted: closed until the first prompt
    __sync_synchronize();   // format + resampler visible before the session thread sees ready
    ready_ = true;
    printf("[Speech] ready: %s %u Hz x%u, opened for each prompt, route %s 0x%x -> 0x%x\n", ep_cfg_.endpoint,
           ep_rate_, ep_ch_, src_ ? "audioconn" : "off", src_, sink_);
    return true;
}

// Opens and closes the endpoint as the focus asks (ep_want_). The open can block for seconds, so it runs here and
// never under lock_; the worker starts under lock_, so a feed change by the focus cannot slip in between.
void SpeechOut::endpointThread() {
    if (!openFirst()) return;
    pthread_mutex_lock(&lock_);
    while (!ep_stop_) {
        const bool want = ep_want_;
        if (want == ep_open_ || (want && ep_failed_)) {
            pthread_cond_wait(&ep_cond_, &lock_);
            continue;
        }
        pthread_mutex_unlock(&lock_);
        if (!want) {
            speech_->stop();
            printf("[Speech] endpoint closed\n");
            pthread_mutex_lock(&lock_);
            ep_open_ = false;
            continue;
        }
        const bool opened = speech_->open(ep_cfg_);
        pthread_mutex_lock(&lock_);
        // Prompt audio queued while it opened plays first; the feed is whatever the focus set meanwhile.
        if (opened && speech_->start(speech_->feedEnabled(), true)) {
            ep_open_ = true;
            ep_fails_ = 0;
            continue;
        }
        ep_failed_ = true;
        const uint32_t fails = ++ep_fails_;
        pthread_mutex_unlock(&lock_);
        if (opened) speech_->stop();
        printf("[Speech] could not %s %s (%u in a row)%s\n", opened ? "start the playback worker on" : "open",
               ep_cfg_.endpoint, fails,
               fails >= kMaxOpenFails ? ": browser mode keeps prompts in its mix until the session ends" : "");
        pthread_mutex_lock(&lock_);
    }
    pthread_mutex_unlock(&lock_);
}

void SpeechOut::init(const SpeechConfig& cfg) {
    if (!cfg.enabled) {
        printf("[Speech] spoken nav prompts: off (NBT_SPEECH: on by default in browser mode, =1 in Bluetooth mode)\n");
        return;
    }
    src_ = cfg.src;
    sink_ = cfg.sink;
    endpoint_ = cfg.endpoint;
    NavPromptPolicy::Config pc;
    pc.hold_ms = cfg.hold_ms > 0 ? (uint32_t)cfg.hold_ms : 0;
    // Ring holds ~12 s at 44.1 kHz stereo (~24 s mono): the phone sends a prompt faster than real time (only
    // max_unacked limits it) and the ring drops the OLDEST bytes when full, which would cut the start of a long prompt.
    speech_ = new A2IPlaybackStream("Speech", 44100u * 4u * 12u);
    // The timer closes the endpoint after each prompt and catches a stalled one: no prompts without it.
    tick_valid_ = pthread_create(&tick_, NULL, &tickThreadThunk, this) == 0;
    if (!tick_valid_) {
        printf("[Speech] could not start the focus timer: spoken prompts stay off\n");
        return;
    }
    pthread_mutex_lock(&lock_);   // the tick thread already runs
    focus_.configure(pc, src_ && route_.available());
    pthread_mutex_unlock(&lock_);
    printf("[Speech] NBT_SPEECH=1: checking the speech endpoint in the background (focus hold %u ms)\n", pc.hold_ms);
    ep_valid_ = pthread_create(&ep_thread_, NULL, &endpointThreadThunk, this) == 0;
    if (!ep_valid_) printf("[Speech] could not start the endpoint thread: spoken prompts stay off\n");
}

bool SpeechOut::takesPrompt(uint64_t now) const {
    if (!ready_) return false;
    pthread_mutex_lock(&lock_);
    const bool ok = focus_.takesPrompt(now) && ep_fails_ < kMaxOpenFails;
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
    rs_.configure(in.rate, in.channels, ep_rate_, ep_ch_);
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

void SpeechOut::openEndpoint() {
    ep_want_ = true;
    ep_failed_ = false;   // a new prompt tries again
    pthread_cond_signal(&ep_cond_);
}

void SpeechOut::closeEndpoint() {
    ep_want_ = false;
    pthread_cond_signal(&ep_cond_);
}

uint32_t SpeechOut::endpointWrites() const { return speech_->writes(); }

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
            printf("[Speech] prompt #%u: focus not granted after %llu ms: dropped, no focus requests for %u s%s\n", n,
                   (unsigned long long)t.waited_ms, backoff_s,
                   t.gave_up ? "; refused again: browser mode keeps prompts in its mix until the session ends" : "");
        else if (t.action == NavPromptPolicy::RELEASE)
            printf("[Speech] prompt #%u: %u Hz x%u, focus granted after %llu ms, released %llu ms after the last "
                   "audio\n", n, rate, ch, after, (unsigned long long)t.idle_ms);
        else if (t.action == NavPromptPolicy::RELEASE_TIMEOUT)
            printf("[Speech] focus release not done after %u ms: going on\n", defaults.release_timeout_ms);
        if (t.stalled)
            printf("[Speech] prompt #%u: the endpoint took no buffer for %llu ms (taken over by the car?): rest of the "
                   "prompt dropped (%u bytes queued), focus released, endpoint closed\n", n,
                   (unsigned long long)t.stall_ms, (unsigned)t.dropped);
        if (t.rebuild) printf("[Speech] prompt #%u: focus released, %s\n", n, t.rebuild);
    }
}

void SpeechOut::onSessionEnded() {
    if (!ready_ || !speech_) return;
    pthread_mutex_lock(&lock_);
    const bool releasing = focus_.sessionEnded(nowMs());
    ep_fails_ = 0;   // the next session tries the endpoint again
    pthread_mutex_unlock(&lock_);
    if (releasing) printf("[Speech] session ended: releasing the nav prompt focus\n");
}

bool SpeechOut::stop() {
    if (tick_valid_) {
        tick_stop_ = true;
        pthread_join(tick_, NULL);
        tick_valid_ = false;
    }
    bool cancelled = false;
    if (ready_) {
        const uint64_t deadline = nowMs() + kStopWaitMs;
        pthread_mutex_lock(&lock_);
        cancelled = focus_.beginStop(nowMs());
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
    }
    if (ep_valid_) {   // an open in progress finishes first
        pthread_mutex_lock(&lock_);
        ep_stop_ = true;
        pthread_cond_signal(&ep_cond_);
        pthread_mutex_unlock(&lock_);
        pthread_join(ep_thread_, NULL);
        ep_valid_ = false;
    }
    if (speech_) speech_->stop();
    return cancelled;
}

} // namespace nbt
