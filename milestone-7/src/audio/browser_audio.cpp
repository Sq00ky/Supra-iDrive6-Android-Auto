// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "audio/browser_audio.hpp"
#include "sdr_builder.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <sched.h>

namespace nbt {

namespace {

// AA formats (service discovery, include/dhu_sdp.h + addSystemAudioSink). Media offers 48 kHz stereo, system audio
// 16 kHz mono, guidance 16 kHz and 48 kHz mono. The phone picks one without saying which, so these are only the
// starting rates: PtsRateMeter measures each stream from its timestamps (car 2026-11-01: guidance came at 48 kHz).
const uint32_t kSrcRate[BrowserAudio::NSTREAMS] = { 48000, 48000, 16000 };   // channel counts: audioSourceChannels()
// The rates each sink offers the phone: the timestamp meter only picks among these (car 2026-11-02: an uneven start
// made it read the 48 kHz media stream as 44100 Hz, so music played 8 % slow and overflowed its ring = crackling).
const uint32_t kOfferedMedia[] = { 48000 };
const uint32_t kOfferedGuidance[] = { 16000, 48000 };
const uint32_t kOfferedSystem[] = { 16000 };
// Raw ring sizes: media 2 s, guidance 8 s at 48 kHz (the phone sends a prompt faster than real time), system 4 s.
const size_t kRingBytes[BrowserAudio::NSTREAMS] = { 48000u * 4u * 2u, 48000u * 2u * 8u, 16000u * 2u * 4u };

const uint32_t kOutCh = AsoundDevice::kChannels;
const int kAudibleLevel = 64;        // |sample| above this (about -54 dBFS) counts as sound for ducking / idle
const uint32_t kLeadMs = 150;        // never run more than this ahead of real time
const uint32_t kWaitStepMs = 2;             // waiting for late input is checked this often
const size_t kFadeFrames = 32;              // ~0.7 ms fade at a real gap (padding, end of a prompt)

uint64_t nowMs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

uint64_t nowUs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

void routeGrantedThunk(void* self) { static_cast<BrowserAudio*>(self)->routeGranted(); }
void releaseDoneThunk(void* self) { static_cast<BrowserAudio*>(self)->releaseDone(); }

void* mixerThunk(void* self) {
    static_cast<BrowserAudio*>(self)->mixerLoop();
    return NULL;
}

} // namespace

BrowserAudio::BrowserAudio()
    : conn_(0xb8), refocus_(true), thread_(), thread_valid_(false), stop_(false), pcm_failed_(false),
      last_sound_ms_(0), last_media_ms_(0), blocks_(0), last_stats_ms_(0), was_ducking_(false) {
    consumed_fn_ = NULL;
    consumed_user_ = NULL;
    ack_window_ = 0;
    media_frame_us_ = 0;
    underflow_fn_ = NULL;
    underflow_user_ = NULL;
    rs_engine_ = PcmResampler::SPEEX;
    rs_engine_cfg_ = PcmResampler::SPEEX;
    rs_quality_ = 5;
    rs_switch_path_ = "";
    rs_switch_check_ms_ = 0;
    rs_us_ = 0;
    rs_failed_ = false;
    prio_want_ = 15;
    prio_now_ = -1;
    last_write_ms_ = 0;
    gap_max_ms_ = gap_late_ = gap_late_total_ = 0;
    wait_ms_ = 30;
    waited_blocks_ = waited_ms_ = 0;
    waited_blocks_total_ = 0;
    preroll_ms_ = 60;
    drift_on_ = true;
    drift_pi_on_ = true;
    drift_reset_ = false;
    drift_applied_ = 0;
    // StreamInput starts every field at 0 / false / NULL; the ring is allocated by init() (the object exists in every
    // audio mode).
    for (int s = 0; s < NSTREAMS; ++s) stream_[s].in.src_rate = kSrcRate[s];
    stream_[MEDIA].sess.rate.configure(kSrcRate[MEDIA], kOfferedMedia, sizeof(kOfferedMedia) / sizeof(kOfferedMedia[0]),
                            audioSourceChannels(MEDIA));
    stream_[GUIDANCE].sess.rate.configure(kSrcRate[GUIDANCE], kOfferedGuidance,
                               sizeof(kOfferedGuidance) / sizeof(kOfferedGuidance[0]), audioSourceChannels(GUIDANCE));
    stream_[SYSTEM].sess.rate.configure(kSrcRate[SYSTEM], kOfferedSystem, sizeof(kOfferedSystem) / sizeof(kOfferedSystem[0]),
                             audioSourceChannels(SYSTEM));
    pthread_mutex_init(&lock_, NULL);
}

BrowserAudio::~BrowserAudio() {
    stop();
    for (int s = 0; s < NSTREAMS; ++s) delete stream_[s].in.ring;
    pthread_mutex_destroy(&lock_);
}

void BrowserAudio::onMicClosed() {
    refocus("mic closed");
}

void BrowserAudio::consumed(int s, size_t bytes) {
    if (bytes && consumed_fn_ && s >= 0 && s < NSTREAMS) consumed_fn_(audioChannelForStream(s), bytes, consumed_user_);
}

// After the mixer pulled a block of stream s: got / need = converted samples obtained / wanted.
void BrowserAudio::checkUnderflow(int s, bool playing, size_t got, size_t need, uint64_t now) {
    if (!playing || got >= need) { stream_[s].mix.dry = false; return; }
    if (stream_[s].mix.dry || stream_[s].in.frames_since_start == 0) return;   // already reported, or no audio yet: not an underflow
    stream_[s].mix.dry = true;
    const uint32_t k = ++stream_[s].mix.reports;
    if (underflow_fn_) underflow_fn_(audioChannelForStream(s), underflow_user_);
    stream_[s].mix.report_ms = now;
    stream_[s].mix.report_written = stream_[s].in.ring->writtenBytes();
    if (k <= 5 || (k % 50) == 0)
        printf("[Browser] %s ran dry: a block got %u of %u frames; underflow #%u reported to the phone\n",
               audioStreamName(s), (unsigned)(got / kOutCh), (unsigned)(need / kOutCh), k);
}

// 5 s after a report: did the phone react? It sent X ms of audio in those 5 s (more than 5000 = it sent extra to
// rebuild its lead) and the queue holds Y ms now.
void BrowserAudio::logUnderflowFollowUp(uint64_t now) {
    for (int s = 0; s < NSTREAMS; ++s) {
        if (!stream_[s].mix.report_ms || now - stream_[s].mix.report_ms < 5000) continue;
        const uint32_t k = stream_[s].mix.reports;
        const uint64_t bps = (uint64_t)stream_[s].in.src_rate * audioSourceChannels(s) * 2u;
        const uint64_t sent = stream_[s].in.ring->writtenBytes() - stream_[s].mix.report_written;
        if (bps && (k <= 5 || (k % 50) == 0))
            printf("[Browser] 5 s after %s underflow #%u: the phone sent %u ms of audio in %u ms, queue now %u ms\n",
                   audioStreamName(s), k, (unsigned)(sent * 1000u / bps), (unsigned)(now - stream_[s].mix.report_ms),
                   (unsigned)(stream_[s].in.ring->size() * 1000u / bps));
        stream_[s].mix.report_ms = 0;
    }
}

void BrowserAudio::discard(int s) {
    if (stream_[s].in.ring) consumed(s, stream_[s].in.ring->clear());
}

bool BrowserAudio::init(const BrowserConfig& cfg) {
    for (int s = 0; s < NSTREAMS; ++s) if (!stream_[s].in.ring) stream_[s].in.ring = new PcmRing(kRingBytes[s]);
    conn_ = cfg.conn;
    if (cfg.force_rate)
        printf("[Browser] NBT_BROWSER_FORCE_RATE=%u (experiment): asking the device for %u Hz whatever it reports\n",
               cfg.force_rate, cfg.force_rate);
    RoutePolicy::Config route_cfg;
    route_cfg.idle_s = cfg.idle_s;
    route_cfg.prompt_idle_ms = cfg.prompt_idle_ms;
    pthread_mutex_lock(&lock_);
    route_policy_.configure(route_cfg);
    pthread_mutex_unlock(&lock_);
    wait_ms_ = cfg.wait_ms;
    for (int s = 0; s < NSTREAMS; ++s) stream_[s].mix.guard.setBudget(wait_ms_);
    preroll_ms_ = cfg.preroll_ms;
    printf("[Browser] pre-roll %u ms per stream%s, late-input wait up to %u ms (NBT_BROWSER_PREROLL_MS / "
           "NBT_BROWSER_WAIT_MS)\n", preroll_ms_, preroll_ms_ ? "" : " (off)", wait_ms_);
    drift_on_ = cfg.drift;
    prio_want_ = cfg.prio;
    {
        rs_engine_ = cfg.resampler_linear ? PcmResampler::LINEAR : PcmResampler::SPEEX;
        rs_engine_cfg_ = rs_engine_;
        rs_quality_ = cfg.speex_q;
        rs_switch_path_ = cfg.resampler_file;
        for (int s = 0; s < NSTREAMS; ++s) stream_[s].mix.rs.setEngine(rs_engine_, rs_quality_);
        if (rs_engine_ == PcmResampler::SPEEX)
            printf("[Browser] resampler: speexdsp, quality %d (NBT_BROWSER_RESAMPLER / NBT_BROWSER_SPEEX_Q)\n",
                   rs_quality_);
        else
            printf("[Browser] resampler: linear interpolation (NBT_BROWSER_RESAMPLER=linear)\n");
        if (*rs_switch_path_)
            printf("[Browser] switch it while playing: echo linear > %s (or speex); rm %s = back to the setting\n",
                   rs_switch_path_, rs_switch_path_);
    }
    drift_.configure(DriftConfig());
    drift_pi_.configure(DriftPiConfig());
    drift_pi_on_ = cfg.drift_pi;
    if (drift_on_)
        printf("[Browser] media drift loop: %s (NBT_BROWSER_DRIFT_LOOP)\n",
               drift_pi_on_ ? "proportional-integral" : "proportional");
    refocus_ = cfg.refocus;
    dev_.configure(cfg.shim_path, cfg.want_rate, cfg.force_rate, cfg.pcm, cfg.frag, cfg.frags);

    duck_cfg_ = DuckConfig();
    duck_cfg_.duck_gain = AudioMixer::dbToGain(cfg.duck_db);
    duck_cfg_.attack_ms = cfg.duck_attack_ms;
    duck_cfg_.release_ms = cfg.duck_release_ms;
    duck_cfg_.hold_ms = cfg.duck_hold_ms;
    duck_cfg_.media_gain = AudioMixer::dbToGain(cfg.media_gain_db);
    duck_cfg_.guidance_gain = AudioMixer::dbToGain(cfg.guidance_gain_db);
    duck_cfg_.system_gain = AudioMixer::dbToGain(cfg.system_gain_db);

    printf("[Browser] audio mode browser (NBT_AUDIO_MODE=browser; Bluetooth is the default): AA media ch %d + guidance ch %d + system audio ch %d, "
           "mixed and played through the browser audio source\n",
           AA_CH_MEDIA_AUDIO, AA_CH_GUIDANCE_AUDIO, AA_CH_SYSTEM_AUDIO);
    printf("[Browser] connection 0x%x%s, release after %d s without media / %d ms after a prompt or sound alone, "
           "ducking %d dB (attack %u / hold %u / release %u ms)\n",
           conn_, conn_ ? "" : " (NBT_BROWSER_CONN=0: not routed)", route_cfg.idle_s, route_cfg.prompt_idle_ms,
           cfg.duck_db, duck_cfg_.attack_ms, duck_cfg_.hold_ms, duck_cfg_.release_ms);
    bool ok = dev_.load();
    if (conn_ && !route_.configure()) printf("[Browser] audioconn not found: connection 0x%x cannot be built\n", conn_);
    return ok;
}

// ---------------------------------------------------------------------------------------------------------------
// Device (mixer thread)
// ---------------------------------------------------------------------------------------------------------------

// The device itself is AsoundDevice (asound_device.cpp). Once it is open, every stream converts to its rate.
bool BrowserAudio::openPcm() {
    if (dev_.isOpen()) return true;
    if (!dev_.open()) return false;
    for (int s = 0; s < NSTREAMS; ++s) {
        stream_[s].mix.rs_rate = stream_[s].in.src_rate;
        stream_[s].mix.rs.configure(stream_[s].mix.rs_rate, audioSourceChannels(s), dev_.rate(), kOutCh);
        stream_[s].mix.rs.resetCounters();
        stream_[s].mix.pending.clear();
    }
    mixer_.configure(dev_.rate(), kOutCh, duck_cfg_);
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// Streams
// ---------------------------------------------------------------------------------------------------------------

void BrowserAudio::onFrame(uint8_t channel, const uint8_t* pcm, size_t len, uint64_t pts) {
    int s = audioStreamForChannel(channel);
    if (s < 0 || !stream_[s].in.ring || !pcm || len == 0) return;
    ++stream_[s].in.frames_in;
    ++stream_[s].in.frames_since_start;
    if (s == MEDIA) {   // frame length, for the drift loop's target cap under deferred acks
        const uint64_t bps = (uint64_t)stream_[MEDIA].in.src_rate * audioSourceChannels(MEDIA) * 2u;
        if (bps) media_frame_us_ = (uint32_t)((uint64_t)len * 1000000u / bps);
    }
    if (pcm_failed_) {   // no output device this session: nothing will play it, so it is taken at once
        consumed(s, len);
        return;
    }
    consumed(s, stream_[s].in.ring->write(pcm, len));   // a full ring drops its oldest bytes
    uint32_t mismatch = 0;
    if (stream_[s].sess.rate.frame(pts, len, &mismatch)) {
        printf("[Browser] AA %s stream is %u Hz (from its timestamps), not %u Hz: converting from %u Hz\n",
               audioStreamName(s), stream_[s].sess.rate.rate(), (unsigned)stream_[s].in.src_rate, stream_[s].sess.rate.rate());
        stream_[s].in.src_rate = stream_[s].sess.rate.rate();   // the mixer reconfigures its resampler on the next block
    } else if (mismatch) {
        printf("[Browser] WARNING: AA %s stream timestamps suggest %u Hz, the phone declared %u Hz: keeping the "
               "declared rate\n", audioStreamName(s), mismatch, stream_[s].sess.rate.rate());
    }
    ensureMixer();
}

void BrowserAudio::onStreamState(uint8_t channel, bool started, uint32_t rate, uint32_t channels) {
    int s = audioStreamForChannel(channel);
    if (s < 0 || !stream_[s].in.ring) return;
    if (started && rate) {
        // The phone declared this start's format (START_INDICATION configuration_index): use it from the first frame.
        printf("[Browser] AA %s stream started: %u Hz %s (declared by the phone)\n", audioStreamName(s), rate,
               channels == 1 ? "mono" : "stereo");
        if (channels && channels != audioSourceChannels(s))
            printf("[Browser] WARNING: AA %s stream declared %u channels, the mixer reads %u\n", audioStreamName(s),
                   channels, audioSourceChannels(s));
    } else {
        printf("[Browser] AA %s stream %s%s\n", audioStreamName(s), started ? "started" : "stopped",
               started ? " without a declared format: measuring its rate from the timestamps" : "");
    }
    if (started) {
        stream_[s].sess.rate.start(rate);
        stream_[s].in.src_rate = stream_[s].sess.rate.rate();   // the mixer reconfigures its resampler before the next block
        stream_[s].in.frames_since_start = 0;        // no underflow before this stream's first frame
        ++stream_[s].in.start_seq;                   // the mixer holds it for pre-roll again, even after a quick STOP / START
    }
    stream_[s].in.started = started;
    if (s == MEDIA) drift_reset_ = true;   // a new stream (or none): the mixer learns its level afresh
    // Media paused: drop what is queued so the music stops now. Prompts and sounds play their tail out.
    if (!started && s == MEDIA) { discard(s); stream_[s].in.flush = true; }
    if (started) ensureMixer();
}

void BrowserAudio::ensureMixer() {
    if (thread_valid_ || pcm_failed_) return;
    stop_ = false;
    if (pthread_create(&thread_, NULL, &mixerThunk, this) != 0) {
        printf("[Browser] could not start the mixer thread\n");
        pcm_failed_ = true;
        for (int s = 0; s < NSTREAMS; ++s) discard(s);   // nothing will play what is queued
        return;
    }
    thread_valid_ = true;
}

void BrowserAudio::stopMixer() {
    if (!thread_valid_) return;
    stop_ = true;
    pthread_join(thread_, NULL);
    thread_valid_ = false;
}

// Takes `frames` converted frames of stream s into out (zero-padded). Mixer thread only.
bool BrowserAudio::pull(int s, size_t frames, int16_t* out, bool* audible, size_t* got_out) {
    if (got_out) *got_out = 0;
    std::vector<int16_t>& q = stream_[s].mix.pending;
    if (stream_[s].in.flush) { stream_[s].in.flush = false; q.clear(); stream_[s].mix.rs.reset(); }
    const uint32_t src = stream_[s].in.src_rate;
    if (src != stream_[s].mix.rs_rate) {   // the session thread measured a different rate for this stream
        stream_[s].mix.rs_rate = src;
        stream_[s].mix.rs.configure(src, audioSourceChannels(s), dev_.rate(), kOutCh);
        stream_[s].mix.rs.resetCounters();   // the ratio check must not mix two rates
        q.clear();
    }
    const size_t need = frames * kOutCh;
    // 10 ms of AA PCM per read, but never more than this buffer (sized for 48 kHz stereo): a stream that declares a
    // higher rate is read in more, smaller steps instead of overflowing it.
    uint8_t raw[48000 / 100 * 2 * 2];
    const size_t raw_block = pcmChunkBytes(src, audioSourceChannels(s), 10u, sizeof(raw));
    while (q.size() < need) {
        size_t got = stream_[s].in.ring->read(raw, raw_block);
        if (got == 0) break;
        consumed(s, got);
        std::vector<uint8_t> conv;
        const uint64_t t0 = nowUs();
        stream_[s].mix.rs.processBytes(raw, got, conv);
        rs_us_ += nowUs() - t0;
        if (rs_engine_ == PcmResampler::SPEEX && stream_[s].mix.rs.engine() != PcmResampler::SPEEX && !rs_failed_) {
            // PcmResampler couldn't set speexdsp up and carried on with linear interpolation: make that visible and
            // switch every stream, so the logs, the health check and an A/B by ear name the engine really in use.
            rs_failed_ = true;
            printf("[Browser] WARNING: speexdsp resampler could not start (%s stream): falling back to linear for "
                   "this session\n", audioStreamName(s));
            setResampler(PcmResampler::LINEAR, "speexdsp could not start");
        }
        size_t old = q.size();
        q.resize(old + conv.size() / 2);
        if (!conv.empty()) memcpy(&q[old], &conv[0], conv.size());
    }
    size_t take = q.size() < need ? q.size() : need;
    if (got_out) *got_out = take;
    if (take == 0) { stream_[s].mix.gap = true; return false; }
    memcpy(out, &q[0], take * 2);
    const size_t take_frames = take / kOutCh;
    // A real gap (padding that waiting could not avoid, or a prompt's end) fades instead of clicking: the audio
    // before the padding ramps down, the first block after it ramps up.
    if (stream_[s].mix.gap) { fadeInHead(out, take_frames, kOutCh, kFadeFrames); stream_[s].mix.gap = false; }
    if (take < need) {
        fadeOutTail(out, take_frames, kOutCh, kFadeFrames);
        memset(out + take, 0, (need - take) * 2);
        stream_[s].mix.gap = true;
    }
    q.erase(q.begin(), q.begin() + take);
    bool loud = false;
    for (size_t i = 0; i < take && !loud; ++i) loud = (out[i] > kAudibleLevel || out[i] < -kAudibleLevel);
    if (audible) *audible = loud;
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// Route (connection 0xb8)
// ---------------------------------------------------------------------------------------------------------------

void BrowserAudio::requestRoute(const char* why) {
    if (!conn_ || !route_.available()) return;
    const uint64_t now = nowMs();
    pthread_mutex_lock(&lock_);
    bool go = route_policy_.tryBuild(now);
    pthread_mutex_unlock(&lock_);
    if (!go) return;
    printf("[Browser] requesting the browser audio source: audioconn -b 0x%x (%s)\n", conn_, why);
    if (!route_.build(conn_, &routeGrantedThunk, this, 0)) {
        pthread_mutex_lock(&lock_);
        route_policy_.buildFailed();
        pthread_mutex_unlock(&lock_);
        printf("[Browser] could not queue audioconn\n");
    }
}

void BrowserAudio::routeGranted() {   // audioconn worker thread
    pthread_mutex_lock(&lock_);
    const RoutePolicy::State st = route_policy_.granted(nowMs());
    pthread_mutex_unlock(&lock_);
    if (st == RoutePolicy::BUILDING)
        printf("[Browser] connection 0x%x granted: AA audio is playing through the browser source\n", conn_);
    else if (st == RoutePolicy::ROUTED)
        printf("[Browser] connection 0x%x granted again\n", conn_);
    else   // released meanwhile: the queued remove runs after this build and takes it down again
        printf("[Browser] connection 0x%x granted after it was released: ignored (the remove follows)\n", conn_);
}

void BrowserAudio::releaseRoute(const char* why) {
    if (!conn_) return;
    pthread_mutex_lock(&lock_);
    const bool built = route_policy_.startRelease(nowMs());
    pthread_mutex_unlock(&lock_);
    if (!built) return;   // idle, or a remove is already on its way
    route_.cancelPending();
    printf("[Browser] releasing the browser audio source: audioconn -r 0x%x (%s)\n", conn_, why);
    // The next build must not overtake this remove (both wait for the one audioconn slot, in no fixed order):
    // stay RELEASING, which blocks requestRoute(), until the remove has run.
    if (!route_.available() || !route_.remove(conn_, &releaseDoneThunk, this)) releaseDone();
}

void BrowserAudio::releaseDone() {   // audioconn worker thread (or the caller when no remove was queued)
    pthread_mutex_lock(&lock_);
    route_policy_.released();
    pthread_mutex_unlock(&lock_);
}

bool BrowserAudio::routedForPlayback() {
    if (!conn_ || !route_.available()) return true;   // nothing to wait for
    pthread_mutex_lock(&lock_);
    bool r = route_policy_.routed();
    pthread_mutex_unlock(&lock_);
    return r;
}

void BrowserAudio::setMediaGain(float g) {
    // Ramped inside the mixer (per frame, ~50 ms), so this is safe from the UI thread while the mixer thread runs.
    mixer_.setMediaGain(g);
}

void BrowserAudio::refocus(const char* why) {
    if (!refocus_ || !conn_ || !route_.available()) return;
    pthread_mutex_lock(&lock_);
    bool built = route_policy_.routed();
    pthread_mutex_unlock(&lock_);
    if (!built) return;   // idle: the next sound builds it anyway; building: already on its way
    printf("[Browser] %s: requesting connection 0x%x again\n", why, conn_);
    route_.build(conn_, &routeGrantedThunk, this, 0);
}

// ---------------------------------------------------------------------------------------------------------------
// Mixer thread
// ---------------------------------------------------------------------------------------------------------------

void BrowserAudio::logStats(bool force) {
    uint64_t now = nowMs();
    if (!force && now - last_stats_ms_ < 10000) return;
    const uint64_t interval_ms = now - last_stats_ms_;
    last_stats_ms_ = now;
    checkResamplerHealth(interval_ms);
    pthread_mutex_lock(&lock_);
    const RoutePolicy::State rs = route_policy_.state();
    pthread_mutex_unlock(&lock_);
    printf("[Browser] blocks %llu, AA frames in media/guidance/system %u/%u/%u, queued bytes %u/%u/%u, "
           "dropped %llu/%llu/%llu, duck %d%%, route %s, write errors %u, re-prepares %u\n",
           (unsigned long long)blocks_, stream_[MEDIA].in.frames_in, stream_[GUIDANCE].in.frames_in, stream_[SYSTEM].in.frames_in,
           (unsigned)stream_[MEDIA].in.ring->size(), (unsigned)stream_[GUIDANCE].in.ring->size(), (unsigned)stream_[SYSTEM].in.ring->size(),
           (unsigned long long)stream_[MEDIA].in.ring->droppedBytes(), (unsigned long long)stream_[GUIDANCE].in.ring->droppedBytes(),
           (unsigned long long)stream_[SYSTEM].in.ring->droppedBytes(), (int)(mixer_.duckLevel() * 100.0f + 0.5f),
           RoutePolicy::stateName(rs), dev_.writeErrors(), dev_.prepares());
    // Gaps since the last line, per stream while it was started: blocks that got only part of their audio (short) or
    // none (empty) and were padded with silence, the lowest ring fill before a block, and the session's total gaps.
    char fill[NSTREAMS][16];
    for (int s = 0; s < NSTREAMS; ++s) {
        if (!stream_[s].mix.watch.hasMin()) { snprintf(fill[s], sizeof(fill[s]), "-"); continue; }
        const uint64_t bps = (uint64_t)stream_[s].in.src_rate * audioSourceChannels(s) * 2u;
        snprintf(fill[s], sizeof(fill[s]), "%u ms", bps ? (unsigned)(stream_[s].mix.watch.minQueued() * 1000u / bps) : 0u);
    }
    printf("[Browser] gaps media/guidance/system: short %u/%u/%u, empty %u/%u/%u of %u/%u/%u blocks, lowest fill %s/%s/%s; "
           "session gaps %llu/%llu/%llu; waited for late input %u ms over %u blocks, waits timed out (session) %u/%u/%u\n",
           stream_[MEDIA].mix.watch.shortPulls(), stream_[GUIDANCE].mix.watch.shortPulls(), stream_[SYSTEM].mix.watch.shortPulls(),
           stream_[MEDIA].mix.watch.emptyPulls(), stream_[GUIDANCE].mix.watch.emptyPulls(), stream_[SYSTEM].mix.watch.emptyPulls(),
           stream_[MEDIA].mix.watch.pulls(), stream_[GUIDANCE].mix.watch.pulls(), stream_[SYSTEM].mix.watch.pulls(), fill[MEDIA], fill[GUIDANCE],
           fill[SYSTEM], (unsigned long long)stream_[MEDIA].mix.watch.totalGaps(), (unsigned long long)stream_[GUIDANCE].mix.watch.totalGaps(),
           (unsigned long long)stream_[SYSTEM].mix.watch.totalGaps(), waited_ms_, waited_blocks_, stream_[MEDIA].mix.guard.timeouts(),
           stream_[GUIDANCE].mix.guard.timeouts(), stream_[SYSTEM].mix.guard.timeouts());
    // Pre-roll: starts (and resumes after running empty) that were held until the stream had a cushion.
    if (preroll_ms_)
        printf("[Browser] pre-roll holds media/guidance/system: %u/%u/%u (%u ms each at most)\n", stream_[MEDIA].mix.gate.holds(),
               stream_[GUIDANCE].mix.gate.holds(), stream_[SYSTEM].mix.gate.holds(), preroll_ms_);
    for (int s = 0; s < NSTREAMS; ++s) { stream_[s].mix.watch.resetInterval(); stream_[s].mix.gate.resetInterval(); }
    waited_ms_ = waited_blocks_ = 0;
    // Mixer timing: the longest time between two device writes in the interval against the block period. Late gaps
    // (over twice the period) eat the device's ~4 fragments of headroom; if they show up, raise NBT_BROWSER_PRIO.
    if (dev_.rate() && dev_.fragBytes()) {
        const uint32_t period = (uint32_t)(dev_.fragBytes() / (kOutCh * 2u) * 1000u / dev_.rate());
        printf("[Browser] mixer timing: block %u ms, longest gap between writes %u ms, late gaps (> %u ms) %u, session "
               "%u, thread priority %d\n", period, gap_max_ms_, 2u * period, gap_late_, gap_late_total_, prio_now_);
        // What decides whether a late-input wait is allowed (waitIsSafe): writes that blocked (device full), and how
        // far ahead of real time the mixer was before a block (a wait needs NBT_BROWSER_WAIT_MS + a block + 20 ms).
        if (dev_.leadSeen())
            printf("[Browser] wait room: writes that blocked %u of %u, lead over real time %lld..%lld ms (a wait needs "
                   "%u ms)\n", dev_.writesBlocked(), dev_.writes(), (long long)dev_.leadMin(), (long long)dev_.leadMax(),
                   wait_ms_ + period + 20u);
    }
    gap_max_ms_ = gap_late_ = 0;
    dev_.resetWaitRoom();
    if (force)
        printf("[Feature] preroll=%u ms holds media/guidance/system=%llu/%llu/%llu; late_input_wait=%u ms "
               "waited_blocks=%llu timeouts=%u/%u/%u\n", preroll_ms_,
               (unsigned long long)stream_[MEDIA].mix.gate.totalHolds(), (unsigned long long)stream_[GUIDANCE].mix.gate.totalHolds(),
               (unsigned long long)stream_[SYSTEM].mix.gate.totalHolds(), wait_ms_, (unsigned long long)waited_blocks_total_,
               stream_[MEDIA].mix.guard.timeouts(), stream_[GUIDANCE].mix.guard.timeouts(), stream_[SYSTEM].mix.guard.timeouts());
    // Media drift correction: the trim now applied, the smoothed media queue and the level it holds.
    if (!drift_on_)
        printf("[Browser] media drift correction off (NBT_BROWSER_DRIFT=0)\n");
    else if (stream_[MEDIA].in.started && drift_.learning())
        printf("[Browser] media drift: learning the queue level (%.0f ms so far)\n", drift_.smoothed());
    else if (stream_[MEDIA].in.started && drift_pi_on_)
        // The integral settles on the phone's clock error: how far its media clock is from the car's.
        printf("[Browser] media drift (PI): trim %+d ppm, queue %.0f ms, target %.0f ms, off by %+.1f ms, phone clock "
               "%+.0f ppm\n", drift_applied_, drift_.smoothed(), drift_.target(), drift_.smoothed() - drift_.target(),
               drift_pi_.integral());
    else if (stream_[MEDIA].in.started)
        printf("[Browser] media drift (P): trim %+d ppm, queue %.0f ms, target %.0f ms, off by %+.1f ms\n",
               drift_applied_, drift_.smoothed(), drift_.target(), drift_.smoothed() - drift_.target());
    if (force && drift_on_ && !drift_.learning()) {
        if (drift_pi_on_)
            printf("[Feature] drift_loop=pi trim=%+d ppm off_target=%+.1f ms phone_clock=%+.0f ppm\n", drift_applied_,
                   drift_.smoothed() - drift_.target(), drift_pi_.integral());
        else
            printf("[Feature] drift_loop=p trim=%+d ppm off_target=%+.1f ms\n", drift_applied_,
                   drift_.smoothed() - drift_.target());
    }
}

// Mixer thread: it must write a device fragment every ~23 ms with ~92 ms of headroom, and with deferred acks its
// consumption also paces the phone, so it should not wait behind HMI or video work for the CPU. QNX mutexes inherit
// priority, so the ring mutex shared with the session thread can't invert it.
void BrowserAudio::raiseMixerPriority() {
    struct sched_param sp;
    int policy = 0;
    if (pthread_getschedparam(pthread_self(), &policy, &sp) != 0) return;
    const int before = sp.sched_priority;
    prio_now_ = before;
    if (prio_want_ <= 0 || before >= prio_want_) {
        printf("[Browser] mixer thread priority %d (NBT_BROWSER_PRIO=%d: %s)\n", before, prio_want_,
               prio_want_ <= 0 ? "left alone" : "already at least that");
        return;
    }
    sp.sched_priority = prio_want_;
    const int r = pthread_setschedparam(pthread_self(), policy, &sp);
    if (r == 0) prio_now_ = prio_want_;
    printf("[Browser] mixer thread priority %d -> %d%s%s (NBT_BROWSER_PRIO)\n", before, prio_now_,
           r == 0 ? "" : ": not set, ", r == 0 ? "" : strerror(r));
}

static const char* engineName(PcmResampler::Engine e) { return e == PcmResampler::SPEEX ? "speexdsp" : "linear"; }

void BrowserAudio::setResampler(PcmResampler::Engine e, const char* why) {
    if (e == rs_engine_) return;
    rs_engine_ = e;
    for (int s = 0; s < NSTREAMS; ++s) {
        stream_[s].mix.rs.setEngine(e, rs_quality_);   // restarts the conversion: a few ms discontinuity
        stream_[s].mix.rs.resetCounters();
    }
    printf("[Browser] resampler -> %s (%s)\n", engineName(e), why);
}

// The switch file: "linear" or "speex" in it selects that engine; no file = the NBT_BROWSER_RESAMPLER setting. After
// a fallback (checkResamplerHealth) speex stays off for the session.
void BrowserAudio::checkResamplerSwitch(uint64_t now) {
    if (!*rs_switch_path_ || now - rs_switch_check_ms_ < 1000) return;
    rs_switch_check_ms_ = now;
    PcmResampler::Engine want = rs_engine_cfg_;
    FILE* f = fopen(rs_switch_path_, "r");
    if (f) {
        char word[16] = { 0 };
        if (fscanf(f, "%15s", word) == 1) {
            if (strcmp(word, "linear") == 0) want = PcmResampler::LINEAR;
            else if (strcmp(word, "speex") == 0) want = PcmResampler::SPEEX;
        }
        fclose(f);
    }
    if (want == PcmResampler::SPEEX && rs_failed_) return;
    setResampler(want, f ? "switch file" : "switch file removed: back to NBT_BROWSER_RESAMPLER");
}

// Each stats interval: CPU spent converting, and whether each stream's output / input frame count matches its rates
// (and trim). Speex that costs over a quarter of the time or gets the ratio wrong by over 2 % falls back to linear.
void BrowserAudio::checkResamplerHealth(uint64_t interval_ms) {
    if (!interval_ms || !dev_.rate()) return;
    const double cpu_pct = (double)rs_us_ / 10.0 / (double)interval_ms;   // us / (ms * 1000) * 100
    char ratios[NSTREAMS][24];
    bool bad = false;
    for (int s = 0; s < NSTREAMS; ++s) {
        const uint64_t in = stream_[s].mix.rs.inFrames(), out = stream_[s].mix.rs.outFrames();
        ratios[s][0] = 0;
        snprintf(ratios[s], sizeof(ratios[s]), "-");
        if (in >= stream_[s].mix.rs_rate && stream_[s].mix.rs_rate) {   // at least a second of input
            const double want = (double)dev_.rate() / (double)stream_[s].mix.rs_rate / (1.0 + stream_[s].mix.rs.trimPpm() * 1e-6);
            const double got = (double)out / (double)in;
            const double err = got / want - 1.0;
            snprintf(ratios[s], sizeof(ratios[s]), "%+.3f %%", err * 100.0);
            if (err > 0.02 || err < -0.02) bad = true;
        }
        stream_[s].mix.rs.resetCounters();
    }
    printf("[Browser] resampler %s: %.1f ms CPU per s (%.2f %%), output/input vs rates media/guidance/system %s/%s/%s\n",
           engineName(rs_engine_), cpu_pct * 10.0, cpu_pct, ratios[MEDIA], ratios[GUIDANCE], ratios[SYSTEM]);
    rs_us_ = 0;
    if (rs_engine_ == PcmResampler::SPEEX && (cpu_pct > 25.0 || bad)) {
        rs_failed_ = true;
        printf("[Browser] WARNING: speexdsp resampler %s: falling back to linear for this session\n",
               bad ? "produced the wrong number of frames" : "used too much CPU");
        setResampler(PcmResampler::LINEAR, "fallback");
    }
}

void BrowserAudio::mixerLoop() {
    raiseMixerPriority();
    if (!openPcm()) {
        printf("[Browser] no output device: AA audio is dropped for this session\n");
        pcm_failed_ = true;
        for (int s = 0; s < NSTREAMS; ++s) discard(s);   // nothing will play what is queued
        return;
    }
    for (int s = 0; s < NSTREAMS; ++s) {
        stream_[s].mix.gate.configure((size_t)preroll_ms_ * dev_.rate() / 1000u, preroll_ms_);
        stream_[s].mix.seen_seq = stream_[s].in.start_seq;
    }
    // One mixer block = one device fragment: the shim puts every write into a buffer slot of its own (one fragment
    // at most), so a short write would hand A2I a part-filled package.
    size_t block = 0;
    std::vector<int16_t> in[NSTREAMS];
    std::vector<int16_t> out;
    uint64_t t0 = nowMs(), written_frames = 0;
    bool last_write_blocked = false;   // the previous write waited for room: the device is full (~4 fragments queued)
    last_sound_ms_ = t0;
    last_stats_ms_ = t0;
    uint32_t fail_run = 0;

    while (!stop_) {
        const size_t want_block = dev_.fragBytes() / (kOutCh * 2u);
        if (want_block != block) {
            block = want_block;
            for (int s = 0; s < NSTREAMS; ++s) in[s].assign(block * kOutCh, 0);
            out.assign(block * kOutCh, 0);
            printf("[Browser] mixing %u frames (%u ms) per write at %u Hz (AA media %u Hz stereo, guidance %u Hz "
                   "mono, system %u Hz mono until measured)\n", (unsigned)block, (unsigned)(block * 1000u / dev_.rate()),
                   dev_.rate(), (unsigned)stream_[MEDIA].in.src_rate, (unsigned)stream_[GUIDANCE].in.src_rate, (unsigned)stream_[SYSTEM].in.src_rate);
        }
        const uint64_t now = nowMs();
        bool queued = false, any_started = false;
        for (int s = 0; s < NSTREAMS; ++s) {
            if (stream_[s].in.started) any_started = true;
            if (stream_[s].in.ring->size() > 0 || !stream_[s].mix.pending.empty()) queued = true;
        }
        if (any_started || queued) requestRoute(any_started ? "AA stream started" : "AA audio queued");

        // Route housekeeping: give up on a grant that never came; drop the source after a quiet spell (RoutePolicy).
        pthread_mutex_lock(&lock_);
        const RoutePolicy::Action ra = route_policy_.decide(now, any_started || queued, last_sound_ms_, last_media_ms_);
        const uint32_t grant_timeout_ms = route_policy_.config().grant_timeout_ms;
        pthread_mutex_unlock(&lock_);
        if (ra == RoutePolicy::GRANT_TIMEOUT) {
            printf("[Browser] connection 0x%x was not granted within %u s: dropping queued audio\n", conn_,
                   grant_timeout_ms / 1000u);
            for (int s = 0; s < NSTREAMS; ++s) { discard(s); stream_[s].in.flush = true; }
            releaseRoute("not granted");
            pthread_mutex_lock(&lock_);
            route_policy_.backoff(nowMs());   // don't hammer audioconn: next try in a minute
            pthread_mutex_unlock(&lock_);
        } else if (ra == RoutePolicy::RELEASE_TIMEOUT) {
            printf("[Browser] remove of connection 0x%x did not report back: carrying on\n", conn_);
            releaseDone();
        } else if (ra == RoutePolicy::RELEASE_IDLE) {
            releaseRoute("no Android Auto sound for a while");
        } else if (ra == RoutePolicy::RELEASE_PROMPT) {
            releaseRoute("prompt / sound over, no AA media playing");
        }

        // Until the car grants the source, keep the device fed with silence and leave the AA audio queued.
        const bool play = routedForPlayback();
        bool have[NSTREAMS] = { false, false, false };
        bool loud[NSTREAMS] = { false, false, false };
        if (play) {
            // Pre-roll: a stream that just started (or ran empty) is held until it has a cushion, so the phone's
            // real-time frames don't each arrive just in time. A held stream is not pulled, counted or reported.
            bool mix[NSTREAMS], live[NSTREAMS];
            for (int s = 0; s < NSTREAMS; ++s) {
                const uint32_t seq = stream_[s].in.start_seq;
                if (seq != stream_[s].mix.seen_seq) { stream_[s].mix.seen_seq = seq; stream_[s].mix.gate.restart(); }
                const bool started = stream_[s].in.started;
                const size_t avail = estimateOutFrames(stream_[s].in.ring->size(), stream_[s].mix.pending.size(), stream_[s].in.src_rate, audioSourceChannels(s),
                                                       dev_.rate(), kOutCh);
                mix[s] = stream_[s].mix.gate.ready(started, avail, now);
                live[s] = started && mix[s];
            }
            // A playing stream that is a few ms short of a whole block: wait for it rather than padding a hole into
            // the audio. Only while the device holds enough to cover the wait (waitIsSafe: the last write blocked,
            // or the mixer is far enough ahead of real time), at most NBT_BROWSER_WAIT_MS per block; a stream that
            // really stopped sending is padded at once after the first timeout.
            // The time waited is measured, not counted in steps: a usleep(2000) rounds up to the next clock tick and
            // may wake late, so counted steps would let a 60 ms budget really take ~90 ms, about all the device holds.
            const uint32_t block_ms = (uint32_t)(block * 1000u / dev_.rate());
            const int64_t lead = (int64_t)(written_frames * 1000u / dev_.rate()) - (int64_t)(nowMs() - t0);
            dev_.noteLead(lead);
            const bool can_wait = waitIsSafe(last_write_blocked, lead, wait_ms_, block_ms);
            uint32_t waited = 0;
            const uint64_t wait_start = nowUs();
            for (;;) {
                bool wait = false;
                for (int s = 0; s < NSTREAMS; ++s) {
                    const size_t avail = estimateOutFrames(stream_[s].in.ring->size(), stream_[s].mix.pending.size(), stream_[s].in.src_rate,
                                                           audioSourceChannels(s), dev_.rate(), kOutCh);
                    if (stream_[s].mix.guard.shouldWait(live[s], avail, block, waited, can_wait)) wait = true;
                }
                if (!wait || stop_) break;
                usleep(kWaitStepMs * 1000u);
                waited = (uint32_t)((nowUs() - wait_start) / 1000u);
            }
            if (waited) { ++waited_blocks_; ++waited_blocks_total_; waited_ms_ += waited; }
            for (int s = 0; s < NSTREAMS; ++s) {
                if (!mix[s]) continue;   // held for pre-roll: its audio stays queued (and unacked)
                const size_t queued_before = stream_[s].in.ring->size();
                size_t got = 0;
                have[s] = pull(s, block, &in[s][0], &loud[s], &got);
                stream_[s].mix.watch.onPull(live[s], queued_before, got, block * kOutCh);
                stream_[s].mix.guard.onPulled(live[s], got / kOutCh, block);
                checkUnderflow(s, live[s], got, block * kOutCh, now);
                stream_[s].mix.gate.onPulled(live[s], got / kOutCh);
            }
        }

        // Media clock drift: once per block while media plays, hold its queue at the level the phone keeps by
        // trimming the media resampler (DriftControl). Reset at every media start / stop.
        if (drift_reset_) {
            drift_reset_ = false;
            drift_.reset();
            drift_pi_.reset();
            if (drift_applied_) { stream_[MEDIA].mix.rs.setTrimPpm(0); drift_applied_ = 0; }
        }
        if (drift_on_ && play && stream_[MEDIA].in.started) {
            const uint64_t in_bps = (uint64_t)stream_[MEDIA].in.src_rate * audioSourceChannels(MEDIA) * 2u;
            const double fill_ms = (in_bps ? (double)stream_[MEDIA].in.ring->size() * 1000.0 / (double)in_bps : 0.0) +
                                   (double)(stream_[MEDIA].mix.pending.size() / kOutCh) * 1000.0 / (double)dev_.rate();
            // Deferred acks: a phone held to the window keeps ~window x frame length queued, and no trim lowers that.
            // The target cap must cover it (plus a block and some jitter), or the loop pins the trim at +500 ppm.
            const uint32_t frame_us = media_frame_us_;
            if (ack_window_ && frame_us) {
                const uint32_t cap = (uint32_t)((uint64_t)ack_window_ * frame_us / 1000u) + 60u;
                if (cap > drift_.maxTarget()) {
                    drift_.raiseMaxTarget(cap);
                    printf("[Browser] media drift: target cap raised to %u ms (deferred acks: window %u x %.1f ms "
                           "frames)\n", cap, ack_window_, frame_us / 1000.0);
                }
            }
            int32_t ppm = drift_.update(fill_ms, now);   // learns the target and smooths the queue for both loops
            if (drift_pi_on_) ppm = drift_.learning() ? 0 : drift_pi_.update(drift_.smoothed() - drift_.target(), now);
            if (ppm != drift_applied_) { stream_[MEDIA].mix.rs.setTrimPpm(ppm); drift_applied_ = ppm; }
        }
        const bool duck = loud[GUIDANCE] || loud[SYSTEM];
        mixer_.mix(have[MEDIA] ? &in[MEDIA][0] : NULL, have[GUIDANCE] ? &in[GUIDANCE][0] : NULL,
                   have[SYSTEM] ? &in[SYSTEM][0] : NULL, block, duck, &out[0]);
        if (loud[MEDIA] || duck) last_sound_ms_ = now;
        if (loud[MEDIA] || stream_[MEDIA].in.started) last_media_ms_ = now;
        const bool ducking = mixer_.ducking();
        if (ducking != was_ducking_) {
            printf("[Browser] media %s\n", ducking ? "ducked under a prompt / system sound" : "back to full level");
            was_ducking_ = ducking;
        }

        // A write that took at least half a block period blocked: the device was full (WAIT above relies on this).
        const uint64_t write_start = nowMs();
        // Lateness: time since the previous write started. A blocking write paces it at one block period; much more
        // means the mixer thread was late (waiting for the CPU, or a slow log write), eating the device's headroom.
        if (last_write_ms_) {
            const uint32_t gap = (uint32_t)(write_start - last_write_ms_);
            if (gap > gap_max_ms_) gap_max_ms_ = gap;
            if (gap > 2u * (uint32_t)(block * 1000u / dev_.rate())) { ++gap_late_; ++gap_late_total_; }
        }
        last_write_ms_ = write_start;
        const bool wrote = dev_.write(&out[0], block);
        last_write_blocked = wrote && nowMs() - write_start >= (uint64_t)(block * 1000u / dev_.rate()) / 2u;
        if (wrote) dev_.noteWrite(last_write_blocked);
        if (wrote) {
            fail_run = 0;
            ++blocks_;
            written_frames += block;
        } else if (++fail_run == 50) {
            printf("[Browser] 50 failed writes in a row: reopening the device\n");
            dev_.close();
            usleep(500000);
            if (!openPcm()) {
                printf("[Browser] reopen failed: AA audio is dropped for this session\n");
                pcm_failed_ = true;
                for (int s = 0; s < NSTREAMS; ++s) discard(s);   // nothing will play what is queued
                break;
            }
            fail_run = 0;
            t0 = nowMs();
            written_frames = 0;
            last_write_ms_ = 0;   // the reopen pause is not lateness
        }

        // A blocking write paces us already; this only guards a shim that returns at once.
        uint64_t played_ms = written_frames * 1000u / dev_.rate();
        uint64_t elapsed = nowMs() - t0;
        if (played_ms > elapsed + kLeadMs) usleep((useconds_t)((played_ms - elapsed - kLeadMs) * 1000u));
        else if (elapsed > played_ms + 1000) { t0 = nowMs(); written_frames = 0; }   // fell behind: re-anchor

        logUnderflowFollowUp(nowMs());
        checkResamplerSwitch(nowMs());
        logStats(false);
    }
    logStats(true);
    dev_.close();
}

void BrowserAudio::onSessionEnded() {
    stopMixer();
    releaseRoute("session ended");
    for (int s = 0; s < NSTREAMS; ++s) {
        stream_[s].in.started = false;
        if (stream_[s].in.ring) stream_[s].in.ring->clear();
        stream_[s].mix.pending.clear();
        stream_[s].mix.rs.reset();
        stream_[s].in.flush = false;
        stream_[s].in.frames_in = 0;
        stream_[s].mix.watch.resetAll();
        stream_[s].mix.guard.reset();
        stream_[s].mix.gate.reset();
        stream_[s].mix.seen_seq = stream_[s].in.start_seq;
        stream_[s].mix.gap = false;
        stream_[s].sess.rate.reset();            // the next phone declares (or is measured) afresh
        stream_[s].in.src_rate = stream_[s].sess.rate.rate();
        stream_[s].in.frames_since_start = 0;
        stream_[s].mix.dry = false;
        stream_[s].mix.report_ms = 0;
        stream_[s].mix.reports = 0;
    }
    last_write_ms_ = 0;
    gap_max_ms_ = gap_late_ = gap_late_total_ = 0;
    rs_us_ = 0;
    rs_failed_ = false;    // the next session tries the configured resampler again
    setResampler(rs_engine_cfg_, "new session");
    pcm_failed_ = false;   // try the device again next session
    pthread_mutex_lock(&lock_);
    route_policy_.clearBackoff();
    pthread_mutex_unlock(&lock_);
    last_media_ms_ = 0;
    waited_ms_ = waited_blocks_ = 0;
    waited_blocks_total_ = 0;
    dev_.resetWaitRoom();
    drift_.reset();   // the mixer thread is stopped: safe to touch its state
    drift_pi_.reset();
    drift_reset_ = false;
    stream_[MEDIA].mix.rs.setTrimPpm(0);
    drift_applied_ = 0;
}

void BrowserAudio::stop() {
    stopMixer();
    releaseRoute("shutdown");
    dev_.unload();
}

} // namespace nbt
