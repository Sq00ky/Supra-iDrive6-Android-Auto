// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "browser_audio.hpp"
#include "nbt_env.hpp"
#include "sdr_builder.hpp"
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/asoundlib.h>   // types and constants only: every function comes from the dlopen'ed shim

namespace nbt {

namespace {

// Shim entry points, in fn_[] order.
enum {
    F_OPEN = 0, F_OPEN_PREFERRED, F_CLOSE, F_INFO, F_PARAMS, F_SETUP, F_STATUS, F_PREPARE, F_WRITE, F_FLUSH,
    F_STRERROR, F_COUNT
};
const char* const kFnNames[F_COUNT] = {
    "snd_pcm_open", "snd_pcm_open_preferred", "snd_pcm_close", "snd_pcm_plugin_info", "snd_pcm_plugin_params",
    "snd_pcm_plugin_setup", "snd_pcm_plugin_status", "snd_pcm_plugin_prepare", "snd_pcm_plugin_write",
    "snd_pcm_plugin_flush", "snd_strerror"
};
typedef int (*FnOpen)(snd_pcm_t**, int, int, int);
typedef int (*FnOpenPreferred)(snd_pcm_t**, int*, int*, int);
typedef int (*FnClose)(snd_pcm_t*);
typedef int (*FnInfo)(snd_pcm_t*, snd_pcm_channel_info_t*);
typedef int (*FnParams)(snd_pcm_t*, snd_pcm_channel_params_t*);
typedef int (*FnSetup)(snd_pcm_t*, snd_pcm_channel_setup_t*);
typedef int (*FnStatus)(snd_pcm_t*, snd_pcm_channel_status_t*);
typedef int (*FnPrepare)(snd_pcm_t*, int);
typedef ssize_t (*FnWrite)(snd_pcm_t*, const void*, size_t);
typedef int (*FnFlush)(snd_pcm_t*, int);
typedef const char* (*FnStrerror)(int);

// AA formats (service discovery, include/dhu_sdp.h + addSystemAudioSink). Media offers 48 kHz stereo, system audio
// 16 kHz mono, guidance 16 kHz and 48 kHz mono. The phone picks one without saying which, so these are only the
// starting rates: PtsRateMeter measures each stream from its timestamps (car 2026-11-01: guidance came at 48 kHz).
const uint32_t kSrcRate[BrowserAudio::NSTREAMS] = { 48000, 48000, 16000 };
const uint32_t kSrcCh[BrowserAudio::NSTREAMS]   = { 2, 1, 1 };
// Raw ring sizes: media 2 s, guidance 8 s at 48 kHz (the phone sends a prompt faster than real time), system 4 s.
const size_t kRingBytes[BrowserAudio::NSTREAMS] = { 48000u * 4u * 2u, 48000u * 2u * 8u, 16000u * 2u * 4u };

const uint32_t kOutCh = 2;
const int kAudibleLevel = 64;        // |sample| above this (about -54 dBFS) counts as sound for ducking / idle
const uint32_t kLeadMs = 150;        // never run more than this ahead of real time
const uint32_t kGrantTimeoutMs = 75000;
const uint32_t kReleaseTimeoutMs = 20000;   // a remove that never reports back

uint64_t nowMs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

int envInt(const char* name, int def) {
    const char* e = getenv(name);
    if (!e || !*e) return def;
    char* end = NULL;
    long v = strtol(e, &end, 0);
    return (end && end != e) ? (int)v : def;
}

void routeGrantedThunk(void* self) { static_cast<BrowserAudio*>(self)->routeGranted(); }
void releaseDoneThunk(void* self) { static_cast<BrowserAudio*>(self)->releaseDone(); }

void* mixerThunk(void* self) {
    static_cast<BrowserAudio*>(self)->mixerLoop();
    return NULL;
}

} // namespace

BrowserAudio::BrowserAudio()
    : conn_(0xb8), want_rate_(48000), idle_s_(10), prompt_idle_ms_(1000), refocus_(true), shim_path_(NULL), lib_(NULL), pcm_(NULL),
      rate_(0), frag_bytes_(0), thread_(), thread_valid_(false), stop_(false), pcm_failed_(false),
      route_state_(ROUTE_IDLE), route_since_ms_(0), route_backoff_until_ms_(0), last_sound_ms_(0), last_media_ms_(0), blocks_(0), write_errors_(0), prepares_(0),
      last_stats_ms_(0), was_ducking_(false) {
    memset(fn_, 0, sizeof(fn_));
    for (int s = 0; s < NSTREAMS; ++s) {
        ring_[s] = NULL;   // allocated by init(): the object exists in every audio mode
        started_[s] = false;
        flush_[s] = false;
        frames_in_[s] = 0;
        src_rate_[s] = kSrcRate[s];
        rs_rate_[s] = 0;
    }
    pthread_mutex_init(&lock_, NULL);
}

BrowserAudio::~BrowserAudio() {
    stop();
    for (int s = 0; s < NSTREAMS; ++s) delete ring_[s];
    pthread_mutex_destroy(&lock_);
}

int BrowserAudio::streamForChannel(uint8_t channel) {
    if (channel == AA_CH_MEDIA_AUDIO) return MEDIA;
    if (channel == AA_CH_GUIDANCE_AUDIO) return GUIDANCE;
    if (channel == AA_CH_SYSTEM_AUDIO) return SYSTEM;
    return -1;
}

const char* BrowserAudio::streamName(int s) {
    return s == MEDIA ? "media" : (s == GUIDANCE ? "guidance" : (s == SYSTEM ? "system" : "?"));
}

bool BrowserAudio::loadShim() {
    if (lib_) return true;
    lib_ = dlopen(shim_path_, RTLD_NOW | RTLD_LOCAL);
    if (!lib_) {
        printf("[Browser] could not load %s: %s\n", shim_path_, dlerror());
        return false;
    }
    for (int i = 0; i < F_COUNT; ++i) {
        fn_[i] = dlsym(lib_, kFnNames[i]);
        if (!fn_[i] && i != F_STRERROR && i != F_FLUSH) {
            printf("[Browser] %s has no %s\n", shim_path_, kFnNames[i]);
            dlclose(lib_);
            lib_ = NULL;
            return false;
        }
    }
    printf("[Browser] libasound shim loaded: %s\n", shim_path_);
    return true;
}

bool BrowserAudio::init() {
    for (int s = 0; s < NSTREAMS; ++s) if (!ring_[s]) ring_[s] = new PcmRing(kRingBytes[s]);
    conn_ = (uint32_t)envHex("NBT_BROWSER_CONN", 0xb8);
    want_rate_ = (uint32_t)envInt("NBT_BROWSER_RATE", 48000);
    idle_s_ = envInt("NBT_BROWSER_IDLE_S", 10);
    prompt_idle_ms_ = envInt("NBT_BROWSER_PROMPT_IDLE_MS", 1000);
    refocus_ = envFlag("NBT_BROWSER_REFOCUS") != 0;
    shim_path_ = getenv("NBT_BROWSER_ASOUND");
    if (!shim_path_ || !*shim_path_) shim_path_ = "/opt/mm/lib/libasound/libasound.so";

    duck_cfg_ = DuckConfig();
    duck_cfg_.duck_gain = AudioMixer::dbToGain(envInt("NBT_DUCK_DB", -12));
    duck_cfg_.attack_ms = (uint32_t)envInt("NBT_DUCK_ATTACK_MS", 80);
    duck_cfg_.release_ms = (uint32_t)envInt("NBT_DUCK_RELEASE_MS", 500);
    duck_cfg_.hold_ms = (uint32_t)envInt("NBT_DUCK_HOLD_MS", 600);
    duck_cfg_.media_gain = AudioMixer::dbToGain(envInt("NBT_MEDIA_GAIN_DB", 0));
    duck_cfg_.guidance_gain = AudioMixer::dbToGain(envInt("NBT_GUIDANCE_GAIN_DB", 0));
    duck_cfg_.system_gain = AudioMixer::dbToGain(envInt("NBT_SYSTEM_GAIN_DB", 0));

    printf("[Browser] NBT_AUDIO_MODE=browser (experiment): AA media ch %d + guidance ch %d + system audio ch %d, "
           "mixed and played through the browser audio source\n",
           AA_CH_MEDIA_AUDIO, AA_CH_GUIDANCE_AUDIO, AA_CH_SYSTEM_AUDIO);
    printf("[Browser] connection 0x%x%s, release after %d s without media / %d ms after a prompt or sound alone, "
           "ducking %d dB (attack %u / hold %u / release %u ms)\n",
           conn_, conn_ ? "" : " (NBT_BROWSER_CONN=0: not routed)", idle_s_, prompt_idle_ms_, envInt("NBT_DUCK_DB", -12),
           duck_cfg_.attack_ms, duck_cfg_.hold_ms, duck_cfg_.release_ms);
    bool ok = loadShim();
    if (conn_ && !route_.configure()) printf("[Browser] audioconn not found: connection 0x%x cannot be built\n", conn_);
    return ok;
}

// ---------------------------------------------------------------------------------------------------------------
// Device (mixer thread)
// ---------------------------------------------------------------------------------------------------------------

// The shim hands our parameters to A2I_Client_Open (liba2i_client check_parameter), which only takes a package
// (= fragment) size that is a power of two of at least 32 bytes, 1..256 packages, and a whole buffer of at least twice
// the device period. Car log 2026-11-01: "check_parameter: Invalid uSizeOfPackage: 3528" (20 ms at 44.1 kHz), which
// snd_pcm_plugin_params reports as -22. A refused open can leave the handle half set up, so every try reopens it.
bool BrowserAudio::openPcm() {
    if (pcm_) return true;
    if (!lib_ && !loadShim()) return false;
    int frag = envInt("NBT_BROWSER_FRAG", 4096);
    int frags = envInt("NBT_BROWSER_FRAGS", 5);
    if (frag < 32) frag = 32;
    if (frags < 1) frags = 1;
    if (frags > 256) frags = 256;
    while (frag & (frag - 1)) frag &= frag - 1;   // round down to a power of two
    // First the configured size, then a deeper and a larger buffer in case the device period is long.
    const int tries[3][2] = { { frag, frags }, { frag, frags * 2 > 256 ? 256 : frags * 2 }, { frag * 2, 8 } };
    for (int t = 0; t < 3; ++t) {
        if (t > 0 && tries[t][0] == tries[t - 1][0] && tries[t][1] == tries[t - 1][1]) continue;
        if (t > 0) printf("[Browser] trying again with %d-byte fragments x%d\n", tries[t][0], tries[t][1]);
        if (openPcmWith(tries[t][0], tries[t][1])) return true;
    }
    return false;
}

bool BrowserAudio::openPcmWith(int frag, int frags) {
    snd_pcm_t* h = NULL;
    int card = -1, dev = -1, rc;
    const char* sel = getenv("NBT_BROWSER_PCM");
    if (sel && *sel && sscanf(sel, "%d:%d", &card, &dev) == 2) {
        rc = ((FnOpen)fn_[F_OPEN])(&h, card, dev, SND_PCM_OPEN_PLAYBACK);
    } else {
        rc = ((FnOpenPreferred)fn_[F_OPEN_PREFERRED])(&h, &card, &dev, SND_PCM_OPEN_PLAYBACK);
    }
    const char* (*err)(int) = (FnStrerror)fn_[F_STRERROR];
    if (rc < 0 || !h) {
        printf("[Browser] PCM open failed (card %d device %d): %d %s\n", card, dev, rc, err ? err(rc) : "");
        return false;
    }
    printf("[Browser] PCM open: card %d device %d\n", card, dev);

    snd_pcm_channel_info_t info;
    memset(&info, 0, sizeof(info));
    info.channel = SND_PCM_CHANNEL_PLAYBACK;
    uint32_t rate = want_rate_;
    int frag_min = 0, frag_max = 0;
    if (((FnInfo)fn_[F_INFO])(h, &info) == 0) {
        printf("[Browser] device info: rates %d..%d Hz, voices %d..%d, fragment %d..%d bytes, max buffer %d\n",
               info.min_rate, info.max_rate, info.min_voices, info.max_voices, info.min_fragment_size,
               info.max_fragment_size, info.max_buffer_size);
        if (info.max_rate > 0 && ((int)rate > info.max_rate || (int)rate < info.min_rate)) rate = (uint32_t)info.max_rate;
        frag_min = info.min_fragment_size;
        frag_max = info.max_fragment_size;
    } else {
        printf("[Browser] snd_pcm_plugin_info failed: asking for %u Hz anyway\n", rate);
    }

    // Keep the fragment a power of two inside the device's range (4096 bytes = 1024 frames = 23 ms at 44.1 kHz).
    while (frag_max > 0 && frag > frag_max && frag > 32) frag >>= 1;
    while (frag_min > 0 && frag < frag_min) frag <<= 1;

    snd_pcm_channel_params_t p;
    memset(&p, 0, sizeof(p));
    p.channel = SND_PCM_CHANNEL_PLAYBACK;
    p.mode = SND_PCM_MODE_BLOCK;
    p.start_mode = SND_PCM_START_FULL;
    p.stop_mode = SND_PCM_STOP_STOP;
    p.buf.block.frag_size = frag;
    p.buf.block.frags_min = 1;
    p.buf.block.frags_max = frags;
    p.format.interleave = 1;
    p.format.format = SND_PCM_SFMT_S16_LE;
    p.format.rate = (int32_t)rate;
    p.format.voices = (int32_t)kOutCh;
    rc = ((FnParams)fn_[F_PARAMS])(h, &p);
    if (rc < 0) {
        printf("[Browser] snd_pcm_plugin_params failed: %d %s (%u Hz, %d-byte fragments x%d; the reason is in "
               "sloginfo: dev-libasound / check_parameter)\n", rc, err ? err(rc) : "", rate, frag, frags);
        ((FnClose)fn_[F_CLOSE])(h);
        return false;
    }
    snd_pcm_channel_setup_t su;
    memset(&su, 0, sizeof(su));
    su.channel = SND_PCM_CHANNEL_PLAYBACK;
    if (((FnSetup)fn_[F_SETUP])(h, &su) == 0) {
        if (su.format.rate > 0) rate = (uint32_t)su.format.rate;
        if (su.buf.block.frag_size > 0) frag = su.buf.block.frag_size;
        printf("[Browser] device setup: %d Hz x%d, format %d, fragment %d bytes x%d\n", su.format.rate,
               su.format.voices, su.format.format, su.buf.block.frag_size, su.buf.block.frags);
        if (su.format.voices > 0 && su.format.voices != (int)kOutCh)
            printf("[Browser] WARNING: device gave %d voices, the mixer writes %u\n", su.format.voices, kOutCh);
    }
    rc = ((FnPrepare)fn_[F_PREPARE])(h, SND_PCM_CHANNEL_PLAYBACK);
    if (rc < 0) {
        printf("[Browser] snd_pcm_plugin_prepare failed: %d %s\n", rc, err ? err(rc) : "");
        ((FnClose)fn_[F_CLOSE])(h);
        return false;
    }
    pcm_ = h;
    rate_ = rate;
    frag_bytes_ = (uint32_t)frag;
    for (int s = 0; s < NSTREAMS; ++s) {
        rs_rate_[s] = src_rate_[s];
        rs_[s].configure(rs_rate_[s], kSrcCh[s], rate_, kOutCh);
        pending_[s].clear();
    }
    mixer_.configure(rate_, kOutCh, duck_cfg_);
    return true;
}

void BrowserAudio::closePcm() {
    if (!pcm_) return;
    snd_pcm_t* h = (snd_pcm_t*)pcm_;
    if (fn_[F_FLUSH]) ((FnFlush)fn_[F_FLUSH])(h, SND_PCM_CHANNEL_PLAYBACK);
    ((FnClose)fn_[F_CLOSE])(h);
    pcm_ = NULL;
    printf("[Browser] PCM closed\n");
}

bool BrowserAudio::writePcm(const int16_t* buf, size_t frames) {
    snd_pcm_t* h = (snd_pcm_t*)pcm_;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(buf);
    size_t left = frames * kOutCh * 2;
    for (int attempt = 0; left > 0 && attempt < 3; ++attempt) {
        ssize_t n = ((FnWrite)fn_[F_WRITE])(h, p, left);
        if (n > 0) { p += n; left -= (size_t)n; if (left == 0) break; }
        // Short write: underrun or the channel stopped. Re-prepare and carry on.
        snd_pcm_channel_status_t st;
        memset(&st, 0, sizeof(st));
        st.channel = SND_PCM_CHANNEL_PLAYBACK;
        if (((FnStatus)fn_[F_STATUS])(h, &st) == 0 &&
            (st.status == SND_PCM_STATUS_UNDERRUN || st.status == SND_PCM_STATUS_READY)) {
            ((FnPrepare)fn_[F_PREPARE])(h, SND_PCM_CHANNEL_PLAYBACK);
            ++prepares_;
        } else if (n < 0) {
            ++write_errors_;
            return false;
        }
    }
    return left == 0;
}

// ---------------------------------------------------------------------------------------------------------------
// Streams
// ---------------------------------------------------------------------------------------------------------------

void BrowserAudio::onAaAudio(uint8_t channel, const uint8_t* pcm, size_t len, uint64_t pts) {
    int s = streamForChannel(channel);
    if (s < 0 || !ring_[s] || !pcm || len == 0) return;
    ++frames_in_[s];
    ring_[s]->write(pcm, len);
    if (pts) {
        const uint32_t r = meter_[s].add(pts, len, kSrcCh[s]);
        if (r && r != src_rate_[s]) {
            printf("[Browser] AA %s stream is %u Hz (from its timestamps), not %u Hz: converting from %u Hz\n",
                   streamName(s), r, (unsigned)src_rate_[s], r);
            src_rate_[s] = r;   // the mixer reconfigures its resampler on the next block
        }
    }
    ensureMixer();
}

void BrowserAudio::onAaStreamState(uint8_t channel, bool started) {
    int s = streamForChannel(channel);
    if (s < 0 || !ring_[s]) return;
    printf("[Browser] AA %s stream %s\n", streamName(s), started ? "started" : "stopped");
    started_[s] = started;
    if (started) meter_[s].reset();   // each stream start is measured again
    // Media paused: drop what is queued so the music stops now. Prompts and sounds play their tail out.
    if (!started && s == MEDIA) { ring_[s]->clear(); flush_[s] = true; }
    if (started) ensureMixer();
}

void BrowserAudio::ensureMixer() {
    if (thread_valid_ || pcm_failed_) return;
    stop_ = false;
    if (pthread_create(&thread_, NULL, &mixerThunk, this) != 0) {
        printf("[Browser] could not start the mixer thread\n");
        pcm_failed_ = true;
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
bool BrowserAudio::pull(int s, size_t frames, int16_t* out, bool* audible) {
    std::vector<int16_t>& q = pending_[s];
    if (flush_[s]) { flush_[s] = false; q.clear(); rs_[s].reset(); }
    const uint32_t src = src_rate_[s];
    if (src != rs_rate_[s]) {   // the session thread measured a different rate for this stream
        rs_rate_[s] = src;
        rs_[s].configure(src, kSrcCh[s], rate_, kOutCh);
        q.clear();
    }
    const size_t need = frames * kOutCh;
    const size_t raw_block = (size_t)(src / 100u) * kSrcCh[s] * 2u;   // 10 ms of AA PCM (at most 48 kHz stereo)
    uint8_t raw[48000 / 100 * 2 * 2];
    while (q.size() < need) {
        size_t got = ring_[s]->read(raw, raw_block);
        if (got == 0) break;
        std::vector<uint8_t> conv;
        rs_[s].processBytes(raw, got, conv);
        size_t old = q.size();
        q.resize(old + conv.size() / 2);
        if (!conv.empty()) memcpy(&q[old], &conv[0], conv.size());
    }
    size_t take = q.size() < need ? q.size() : need;
    if (take == 0) return false;
    memcpy(out, &q[0], take * 2);
    if (take < need) memset(out + take, 0, (need - take) * 2);
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
    bool go = (route_state_ == ROUTE_IDLE) && now >= route_backoff_until_ms_;
    if (go) { route_state_ = ROUTE_BUILDING; route_since_ms_ = now; }
    pthread_mutex_unlock(&lock_);
    if (!go) return;
    printf("[Browser] requesting the browser audio source: audioconn -b 0x%x (%s)\n", conn_, why);
    if (!route_.build(conn_, &routeGrantedThunk, this, 0)) {
        pthread_mutex_lock(&lock_);
        route_state_ = ROUTE_IDLE;
        pthread_mutex_unlock(&lock_);
        printf("[Browser] could not queue audioconn\n");
    }
}

void BrowserAudio::routeGranted() {   // audioconn worker thread
    pthread_mutex_lock(&lock_);
    const int st = route_state_;
    if (st == ROUTE_BUILDING) { route_state_ = ROUTE_ROUTED; route_since_ms_ = nowMs(); }
    pthread_mutex_unlock(&lock_);
    if (st == ROUTE_BUILDING)
        printf("[Browser] connection 0x%x granted: AA audio is playing through the browser source\n", conn_);
    else if (st == ROUTE_ROUTED)
        printf("[Browser] connection 0x%x granted again\n", conn_);
    else   // released meanwhile: the queued remove runs after this build and takes it down again
        printf("[Browser] connection 0x%x granted after it was released: ignored (the remove follows)\n", conn_);
}

void BrowserAudio::releaseRoute(const char* why) {
    if (!conn_) return;
    pthread_mutex_lock(&lock_);
    const bool built = (route_state_ == ROUTE_BUILDING || route_state_ == ROUTE_ROUTED);
    if (built) { route_state_ = ROUTE_RELEASING; route_since_ms_ = nowMs(); }
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
    if (route_state_ == ROUTE_RELEASING) route_state_ = ROUTE_IDLE;
    pthread_mutex_unlock(&lock_);
}

bool BrowserAudio::routedForPlayback() {
    if (!conn_ || !route_.available()) return true;   // nothing to wait for
    pthread_mutex_lock(&lock_);
    bool r = (route_state_ == ROUTE_ROUTED);
    pthread_mutex_unlock(&lock_);
    return r;
}

void BrowserAudio::refocus(const char* why) {
    if (!refocus_ || !conn_ || !route_.available()) return;
    pthread_mutex_lock(&lock_);
    bool built = (route_state_ == ROUTE_ROUTED);
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
    last_stats_ms_ = now;
    pthread_mutex_lock(&lock_);
    int rs = route_state_;
    pthread_mutex_unlock(&lock_);
    printf("[Browser] blocks %llu, AA frames in media/guidance/system %u/%u/%u, queued bytes %u/%u/%u, "
           "dropped %llu/%llu/%llu, duck %d%%, route %s, write errors %u, re-prepares %u\n",
           (unsigned long long)blocks_, frames_in_[MEDIA], frames_in_[GUIDANCE], frames_in_[SYSTEM],
           (unsigned)ring_[MEDIA]->size(), (unsigned)ring_[GUIDANCE]->size(), (unsigned)ring_[SYSTEM]->size(),
           (unsigned long long)ring_[MEDIA]->droppedBytes(), (unsigned long long)ring_[GUIDANCE]->droppedBytes(),
           (unsigned long long)ring_[SYSTEM]->droppedBytes(), (int)(mixer_.duckLevel() * 100.0f + 0.5f),
           rs == ROUTE_ROUTED ? "granted" : (rs == ROUTE_BUILDING ? "requested" : (rs == ROUTE_RELEASING ? "releasing" : "idle")), write_errors_, prepares_);
}

void BrowserAudio::mixerLoop() {
    if (!openPcm()) {
        printf("[Browser] no output device: AA audio is dropped for this session\n");
        pcm_failed_ = true;
        return;
    }
    // One mixer block = one device fragment: the shim puts every write into a buffer slot of its own (one fragment
    // at most), so a short write would hand A2I a part-filled package.
    size_t block = 0;
    std::vector<int16_t> in[NSTREAMS];
    std::vector<int16_t> out;
    uint64_t t0 = nowMs(), written_frames = 0;
    last_sound_ms_ = t0;
    last_stats_ms_ = t0;
    uint32_t fail_run = 0;

    while (!stop_) {
        const size_t want_block = frag_bytes_ / (kOutCh * 2u);
        if (want_block != block) {
            block = want_block;
            for (int s = 0; s < NSTREAMS; ++s) in[s].assign(block * kOutCh, 0);
            out.assign(block * kOutCh, 0);
            printf("[Browser] mixing %u frames (%u ms) per write at %u Hz (AA media %u Hz stereo, guidance %u Hz "
                   "mono, system %u Hz mono until measured)\n", (unsigned)block, (unsigned)(block * 1000u / rate_),
                   rate_, (unsigned)src_rate_[MEDIA], (unsigned)src_rate_[GUIDANCE], (unsigned)src_rate_[SYSTEM]);
        }
        const uint64_t now = nowMs();
        bool queued = false, any_started = false;
        for (int s = 0; s < NSTREAMS; ++s) {
            if (started_[s]) any_started = true;
            if (ring_[s]->size() > 0 || !pending_[s].empty()) queued = true;
        }
        if (any_started || queued) requestRoute(any_started ? "AA stream started" : "AA audio queued");

        // Route housekeeping: give up on a grant that never came; drop the source after a quiet spell.
        pthread_mutex_lock(&lock_);
        int rstate = route_state_;
        uint64_t since = route_since_ms_;
        pthread_mutex_unlock(&lock_);
        if (rstate == ROUTE_BUILDING && now - since > kGrantTimeoutMs) {
            printf("[Browser] connection 0x%x was not granted within %u s: dropping queued audio\n", conn_,
                   kGrantTimeoutMs / 1000u);
            for (int s = 0; s < NSTREAMS; ++s) { ring_[s]->clear(); flush_[s] = true; }
            releaseRoute("not granted");
            pthread_mutex_lock(&lock_);
            route_backoff_until_ms_ = nowMs() + 60000u;   // don't hammer audioconn: next try in a minute
            pthread_mutex_unlock(&lock_);
        } else if (rstate == ROUTE_RELEASING && now - since > kReleaseTimeoutMs) {
            printf("[Browser] remove of connection 0x%x did not report back: carrying on\n", conn_);
            releaseDone();
        } else if (rstate == ROUTE_ROUTED && !any_started && !queued) {
            // Release as soon as it's quiet. If AA media played recently (paused / between tracks) wait
            // NBT_BROWSER_IDLE_S, so resuming doesn't rebuild. If only prompts or system sounds played (the user is
            // on the radio), wait just NBT_BROWSER_PROMPT_IDLE_MS: the radio is then cut only for the prompt.
            const uint64_t media_hold = idle_s_ > 0 ? (uint64_t)idle_s_ * 1000u : 0;
            const bool media_recent = last_media_ms_ && (media_hold == 0 || now - last_media_ms_ < media_hold);
            const uint64_t quiet = now - last_sound_ms_;
            if (media_recent) {
                if (media_hold > 0 && quiet > media_hold) releaseRoute("no Android Auto sound for a while");
            } else if (prompt_idle_ms_ > 0) {
                if (quiet > (uint64_t)prompt_idle_ms_) releaseRoute("prompt / sound over, no AA media playing");
            } else if (media_hold > 0 && quiet > media_hold) {
                releaseRoute("no Android Auto sound for a while");
            }
        }

        // Until the car grants the source, keep the device fed with silence and leave the AA audio queued.
        const bool play = routedForPlayback();
        bool have[NSTREAMS] = { false, false, false };
        bool loud[NSTREAMS] = { false, false, false };
        if (play) for (int s = 0; s < NSTREAMS; ++s) have[s] = pull(s, block, &in[s][0], &loud[s]);
        const bool duck = loud[GUIDANCE] || loud[SYSTEM];
        mixer_.mix(have[MEDIA] ? &in[MEDIA][0] : NULL, have[GUIDANCE] ? &in[GUIDANCE][0] : NULL,
                   have[SYSTEM] ? &in[SYSTEM][0] : NULL, block, duck, &out[0]);
        if (loud[MEDIA] || duck) last_sound_ms_ = now;
        if (loud[MEDIA] || started_[MEDIA]) last_media_ms_ = now;
        const bool ducking = mixer_.ducking();
        if (ducking != was_ducking_) {
            printf("[Browser] media %s\n", ducking ? "ducked under a prompt / system sound" : "back to full level");
            was_ducking_ = ducking;
        }

        if (writePcm(&out[0], block)) {
            fail_run = 0;
            ++blocks_;
            written_frames += block;
        } else if (++fail_run == 50) {
            printf("[Browser] 50 failed writes in a row: reopening the device\n");
            closePcm();
            usleep(500000);
            if (!openPcm()) { printf("[Browser] reopen failed: AA audio is dropped for this session\n"); pcm_failed_ = true; break; }
            fail_run = 0;
            t0 = nowMs();
            written_frames = 0;
        }

        // A blocking write paces us already; this only guards a shim that returns at once.
        uint64_t played_ms = written_frames * 1000u / rate_;
        uint64_t elapsed = nowMs() - t0;
        if (played_ms > elapsed + kLeadMs) usleep((useconds_t)((played_ms - elapsed - kLeadMs) * 1000u));
        else if (elapsed > played_ms + 1000) { t0 = nowMs(); written_frames = 0; }   // fell behind: re-anchor

        logStats(false);
    }
    logStats(true);
    closePcm();
}

void BrowserAudio::onSessionEnded() {
    stopMixer();
    releaseRoute("session ended");
    for (int s = 0; s < NSTREAMS; ++s) {
        started_[s] = false;
        if (ring_[s]) ring_[s]->clear();
        pending_[s].clear();
        rs_[s].reset();
        flush_[s] = false;
        frames_in_[s] = 0;
    }
    pcm_failed_ = false;   // try the device again next session
    pthread_mutex_lock(&lock_);
    route_backoff_until_ms_ = 0;
    pthread_mutex_unlock(&lock_);
    last_media_ms_ = 0;
}

void BrowserAudio::stop() {
    stopMixer();
    releaseRoute("shutdown");
    if (lib_) { dlclose(lib_); lib_ = NULL; }
}

} // namespace nbt
