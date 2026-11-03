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
#include <pthread.h>
#include <sched.h>
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
// The rates each sink offers the phone: the timestamp meter only picks among these (car 2026-11-02: an uneven start
// made it read the 48 kHz media stream as 44100 Hz, so music played 8 % slow and overflowed its ring = crackling).
const uint32_t kOfferedMedia[] = { 48000 };
const uint32_t kOfferedGuidance[] = { 16000, 48000 };
const uint32_t kOfferedSystem[] = { 16000 };
// Raw ring sizes: media 2 s, guidance 8 s at 48 kHz (the phone sends a prompt faster than real time), system 4 s.
const size_t kRingBytes[BrowserAudio::NSTREAMS] = { 48000u * 4u * 2u, 48000u * 2u * 8u, 16000u * 2u * 4u };

const uint32_t kOutCh = 2;
const int kAudibleLevel = 64;        // |sample| above this (about -54 dBFS) counts as sound for ducking / idle
const uint32_t kLeadMs = 150;        // never run more than this ahead of real time
const uint32_t kGrantTimeoutMs = 75000;
const uint32_t kReleaseTimeoutMs = 20000;   // a remove that never reports back
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
    : conn_(0xb8), want_rate_(48000), force_rate_(0), idle_s_(10), prompt_idle_ms_(1000), refocus_(true), shim_path_(NULL), lib_(NULL), pcm_(NULL),
      rate_(0), frag_bytes_(0), thread_(), thread_valid_(false), stop_(false), pcm_failed_(false),
      route_state_(ROUTE_IDLE), route_since_ms_(0), route_backoff_until_ms_(0), last_sound_ms_(0), last_media_ms_(0), blocks_(0), write_errors_(0), prepares_(0),
      last_stats_ms_(0), was_ducking_(false) {
    memset(fn_, 0, sizeof(fn_));
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
    for (int s = 0; s < NSTREAMS; ++s) {
        frames_since_start_[s] = 0;
        dry_[s] = false;
        report_ms_[s] = 0;
        report_written_[s] = 0;
        reports_[s] = 0;
    }
    wait_ms_ = 30;
    waited_blocks_ = waited_ms_ = 0;
    drift_on_ = true;
    drift_pi_on_ = true;
    drift_reset_ = false;
    drift_applied_ = 0;
    for (int s = 0; s < NSTREAMS; ++s) {
        ring_[s] = NULL;   // allocated by init(): the object exists in every audio mode
        started_[s] = false;
        flush_[s] = false;
        gap_[s] = false;
        frames_in_[s] = 0;
        src_rate_[s] = kSrcRate[s];
        rs_rate_[s] = 0;
    }
    srate_[MEDIA].configure(kSrcRate[MEDIA], kOfferedMedia, sizeof(kOfferedMedia) / sizeof(kOfferedMedia[0]),
                            kSrcCh[MEDIA]);
    srate_[GUIDANCE].configure(kSrcRate[GUIDANCE], kOfferedGuidance,
                               sizeof(kOfferedGuidance) / sizeof(kOfferedGuidance[0]), kSrcCh[GUIDANCE]);
    srate_[SYSTEM].configure(kSrcRate[SYSTEM], kOfferedSystem, sizeof(kOfferedSystem) / sizeof(kOfferedSystem[0]),
                             kSrcCh[SYSTEM]);
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

static const uint8_t kStreamChannel[BrowserAudio::NSTREAMS] = { AA_CH_MEDIA_AUDIO, AA_CH_GUIDANCE_AUDIO,
                                                                AA_CH_SYSTEM_AUDIO };

void BrowserAudio::consumed(int s, size_t bytes) {
    if (bytes && consumed_fn_ && s >= 0 && s < NSTREAMS) consumed_fn_(kStreamChannel[s], bytes, consumed_user_);
}

// After the mixer pulled a block of stream s: got / need = converted samples obtained / wanted.
void BrowserAudio::checkUnderflow(int s, size_t got, size_t need, uint64_t now) {
    if (!started_[s] || got >= need) { dry_[s] = false; return; }
    if (dry_[s] || frames_since_start_[s] == 0) return;   // already reported, or no audio yet: not an underflow
    dry_[s] = true;
    const uint32_t k = ++reports_[s];
    if (underflow_fn_) underflow_fn_(kStreamChannel[s], underflow_user_);
    report_ms_[s] = now;
    report_written_[s] = ring_[s]->writtenBytes();
    if (k <= 5 || (k % 50) == 0)
        printf("[Browser] %s ran dry: a block got %u of %u frames; underflow #%u reported to the phone\n",
               streamName(s), (unsigned)(got / kOutCh), (unsigned)(need / kOutCh), k);
}

// 5 s after a report: did the phone react? It sent X ms of audio in those 5 s (more than 5000 = it sent extra to
// rebuild its lead) and the queue holds Y ms now.
void BrowserAudio::logUnderflowFollowUp(uint64_t now) {
    for (int s = 0; s < NSTREAMS; ++s) {
        if (!report_ms_[s] || now - report_ms_[s] < 5000) continue;
        const uint32_t k = reports_[s];
        const uint64_t bps = (uint64_t)src_rate_[s] * kSrcCh[s] * 2u;
        const uint64_t sent = ring_[s]->writtenBytes() - report_written_[s];
        if (bps && (k <= 5 || (k % 50) == 0))
            printf("[Browser] 5 s after %s underflow #%u: the phone sent %u ms of audio in %u ms, queue now %u ms\n",
                   streamName(s), k, (unsigned)(sent * 1000u / bps), (unsigned)(now - report_ms_[s]),
                   (unsigned)(ring_[s]->size() * 1000u / bps));
        report_ms_[s] = 0;
    }
}

void BrowserAudio::discard(int s) {
    if (ring_[s]) consumed(s, ring_[s]->clear());
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
    {
        const int fr = envInt("NBT_BROWSER_FORCE_RATE", 0);
        force_rate_ = fr > 0 ? (uint32_t)fr : 0u;
    }
    if (force_rate_)
        printf("[Browser] NBT_BROWSER_FORCE_RATE=%u (experiment): asking the device for %u Hz whatever it reports\n",
               force_rate_, force_rate_);
    idle_s_ = envInt("NBT_BROWSER_IDLE_S", 10);
    prompt_idle_ms_ = envInt("NBT_BROWSER_PROMPT_IDLE_MS", 1000);
    {
        // Above ~60 ms a wait could outlast what the device still holds when the mixer wakes (~92 ms).
        int w = envInt("NBT_BROWSER_WAIT_MS", 30);
        wait_ms_ = w < 0 ? 0u : (w > 60 ? 60u : (uint32_t)w);
    }
    for (int s = 0; s < NSTREAMS; ++s) guard_[s].setBudget(wait_ms_);
    drift_on_ = envFlag("NBT_BROWSER_DRIFT") != 0;   // unset (-1) or on (1): correct media clock drift
    // Mixer thread priority: 15 by default, above the default 10 that the HMI and video threads compete at, below
    // the 21 of the USB reader. 0 = leave it alone. The car's own audio threads should stay above it (check with pidin).
    prio_want_ = (int)envRange("NBT_BROWSER_PRIO", 15, 0, 63);
    // Resampler: speexdsp by default (flat to ~19.5 kHz, no aliasing, no treble wobble under the drift trim);
    // NBT_BROWSER_RESAMPLER=linear goes back to linear interpolation.
    {
        const char* e = getenv("NBT_BROWSER_RESAMPLER");
        rs_engine_ = (e && strcmp(e, "linear") == 0) ? PcmResampler::LINEAR : PcmResampler::SPEEX;
        rs_engine_cfg_ = rs_engine_;
        rs_quality_ = (int)envRange("NBT_BROWSER_SPEEX_Q", 5, 0, 10);
        rs_switch_path_ = getenv("NBT_BROWSER_RESAMPLER_FILE");
        if (!rs_switch_path_) rs_switch_path_ = "/tmp/nbt_resampler";
        for (int s = 0; s < NSTREAMS; ++s) rs_[s].setEngine(rs_engine_, rs_quality_);
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
    {
        // Which loop sets the trim: pi (default: holds the queue on target, its integral reads the phone's clock
        // error) or p (the proportional trim alone, ~0.1 ms off target per ppm of drift).
        const char* e = getenv("NBT_BROWSER_DRIFT_LOOP");
        drift_pi_on_ = !(e && strcmp(e, "p") == 0);
        if (drift_on_)
            printf("[Browser] media drift loop: %s (NBT_BROWSER_DRIFT_LOOP)\n",
                   drift_pi_on_ ? "proportional-integral" : "proportional");
    }
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

    printf("[Browser] audio mode browser (NBT_AUDIO_MODE=browser; Bluetooth is the default): AA media ch %d + guidance ch %d + system audio ch %d, "
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
    uint32_t rate = pickDeviceRate(want_rate_, force_rate_, 0, 0);
    int frag_min = 0, frag_max = 0;
    if (((FnInfo)fn_[F_INFO])(h, &info) == 0) {
        printf("[Browser] device info: rates %d..%d Hz, voices %d..%d, fragment %d..%d bytes, max buffer %d\n",
               info.min_rate, info.max_rate, info.min_voices, info.max_voices, info.min_fragment_size,
               info.max_fragment_size, info.max_buffer_size);
        rate = pickDeviceRate(want_rate_, force_rate_, info.min_rate, info.max_rate);
        if (force_rate_ && info.max_rate > 0 && ((int)force_rate_ > info.max_rate || (int)force_rate_ < info.min_rate))
            printf("[Browser] NBT_BROWSER_FORCE_RATE: asking for %u Hz, outside the reported %d..%d Hz\n", force_rate_,
                   info.min_rate, info.max_rate);
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
        if (force_rate_)
            printf("[Browser] NBT_BROWSER_FORCE_RATE: the device %s %u Hz (got %u Hz)\n",
                   rate == force_rate_ ? "accepted" : "did not take", force_rate_, rate);
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
        rs_[s].resetCounters();
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
    ++frames_since_start_[s];
    if (s == MEDIA) {   // frame length, for the drift loop's target cap under deferred acks
        const uint64_t bps = (uint64_t)src_rate_[MEDIA] * kSrcCh[MEDIA] * 2u;
        if (bps) media_frame_us_ = (uint32_t)((uint64_t)len * 1000000u / bps);
    }
    if (pcm_failed_) {   // no output device this session: nothing will play it, so it is taken at once
        consumed(s, len);
        return;
    }
    consumed(s, ring_[s]->write(pcm, len));   // a full ring drops its oldest bytes
    uint32_t mismatch = 0;
    if (srate_[s].frame(pts, len, &mismatch)) {
        printf("[Browser] AA %s stream is %u Hz (from its timestamps), not %u Hz: converting from %u Hz\n",
               streamName(s), srate_[s].rate(), (unsigned)src_rate_[s], srate_[s].rate());
        src_rate_[s] = srate_[s].rate();   // the mixer reconfigures its resampler on the next block
    } else if (mismatch) {
        printf("[Browser] WARNING: AA %s stream timestamps suggest %u Hz, the phone declared %u Hz: keeping the "
               "declared rate\n", streamName(s), mismatch, srate_[s].rate());
    }
    ensureMixer();
}

void BrowserAudio::onAaStreamState(uint8_t channel, bool started, uint32_t rate, uint32_t channels) {
    int s = streamForChannel(channel);
    if (s < 0 || !ring_[s]) return;
    if (started && rate) {
        // The phone declared this start's format (START_INDICATION configuration_index): use it from the first frame.
        printf("[Browser] AA %s stream started: %u Hz %s (declared by the phone)\n", streamName(s), rate,
               channels == 1 ? "mono" : "stereo");
        if (channels && channels != kSrcCh[s])
            printf("[Browser] WARNING: AA %s stream declared %u channels, the mixer reads %u\n", streamName(s),
                   channels, kSrcCh[s]);
    } else {
        printf("[Browser] AA %s stream %s%s\n", streamName(s), started ? "started" : "stopped",
               started ? " without a declared format: measuring its rate from the timestamps" : "");
    }
    if (started) {
        srate_[s].start(rate);
        src_rate_[s] = srate_[s].rate();   // the mixer reconfigures its resampler before the next block
        frames_since_start_[s] = 0;        // no underflow before this stream's first frame
    }
    started_[s] = started;
    if (s == MEDIA) drift_reset_ = true;   // a new stream (or none): the mixer learns its level afresh
    // Media paused: drop what is queued so the music stops now. Prompts and sounds play their tail out.
    if (!started && s == MEDIA) { discard(s); flush_[s] = true; }
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
    std::vector<int16_t>& q = pending_[s];
    if (flush_[s]) { flush_[s] = false; q.clear(); rs_[s].reset(); }
    const uint32_t src = src_rate_[s];
    if (src != rs_rate_[s]) {   // the session thread measured a different rate for this stream
        rs_rate_[s] = src;
        rs_[s].configure(src, kSrcCh[s], rate_, kOutCh);
        rs_[s].resetCounters();   // the ratio check must not mix two rates
        q.clear();
    }
    const size_t need = frames * kOutCh;
    // 10 ms of AA PCM per read, but never more than this buffer (sized for 48 kHz stereo): a stream that declares a
    // higher rate is read in more, smaller steps instead of overflowing it.
    uint8_t raw[48000 / 100 * 2 * 2];
    const size_t raw_block = pcmChunkBytes(src, kSrcCh[s], 10u, sizeof(raw));
    while (q.size() < need) {
        size_t got = ring_[s]->read(raw, raw_block);
        if (got == 0) break;
        consumed(s, got);
        std::vector<uint8_t> conv;
        const uint64_t t0 = nowUs();
        rs_[s].processBytes(raw, got, conv);
        rs_us_ += nowUs() - t0;
        size_t old = q.size();
        q.resize(old + conv.size() / 2);
        if (!conv.empty()) memcpy(&q[old], &conv[0], conv.size());
    }
    size_t take = q.size() < need ? q.size() : need;
    if (got_out) *got_out = take;
    if (take == 0) { gap_[s] = true; return false; }
    memcpy(out, &q[0], take * 2);
    const size_t take_frames = take / kOutCh;
    // A real gap (padding that waiting could not avoid, or a prompt's end) fades instead of clicking: the audio
    // before the padding ramps down, the first block after it ramps up.
    if (gap_[s]) { fadeInHead(out, take_frames, kOutCh, kFadeFrames); gap_[s] = false; }
    if (take < need) {
        fadeOutTail(out, take_frames, kOutCh, kFadeFrames);
        memset(out + take, 0, (need - take) * 2);
        gap_[s] = true;
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
    const uint64_t interval_ms = now - last_stats_ms_;
    last_stats_ms_ = now;
    checkResamplerHealth(interval_ms);
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
    // Gaps since the last line, per stream while it was started: blocks that got only part of their audio (short) or
    // none (empty) and were padded with silence, the lowest ring fill before a block, and the session's total gaps.
    char fill[NSTREAMS][16];
    for (int s = 0; s < NSTREAMS; ++s) {
        if (!watch_[s].hasMin()) { snprintf(fill[s], sizeof(fill[s]), "-"); continue; }
        const uint64_t bps = (uint64_t)src_rate_[s] * kSrcCh[s] * 2u;
        snprintf(fill[s], sizeof(fill[s]), "%u ms", bps ? (unsigned)(watch_[s].minQueued() * 1000u / bps) : 0u);
    }
    printf("[Browser] gaps media/guidance/system: short %u/%u/%u, empty %u/%u/%u of %u/%u/%u blocks, lowest fill %s/%s/%s; "
           "session gaps %llu/%llu/%llu; waited for late input %u ms over %u blocks, waits timed out (session) %u/%u/%u\n",
           watch_[MEDIA].shortPulls(), watch_[GUIDANCE].shortPulls(), watch_[SYSTEM].shortPulls(),
           watch_[MEDIA].emptyPulls(), watch_[GUIDANCE].emptyPulls(), watch_[SYSTEM].emptyPulls(),
           watch_[MEDIA].pulls(), watch_[GUIDANCE].pulls(), watch_[SYSTEM].pulls(), fill[MEDIA], fill[GUIDANCE],
           fill[SYSTEM], (unsigned long long)watch_[MEDIA].totalGaps(), (unsigned long long)watch_[GUIDANCE].totalGaps(),
           (unsigned long long)watch_[SYSTEM].totalGaps(), waited_ms_, waited_blocks_, guard_[MEDIA].timeouts(),
           guard_[GUIDANCE].timeouts(), guard_[SYSTEM].timeouts());
    for (int s = 0; s < NSTREAMS; ++s) watch_[s].resetInterval();
    waited_ms_ = waited_blocks_ = 0;
    // Mixer timing: the longest time between two device writes in the interval against the block period. Late gaps
    // (over twice the period) eat the device's ~4 fragments of headroom; if they show up, raise NBT_BROWSER_PRIO.
    if (rate_ && frag_bytes_) {
        const uint32_t period = (uint32_t)(frag_bytes_ / (kOutCh * 2u) * 1000u / rate_);
        printf("[Browser] mixer timing: block %u ms, longest gap between writes %u ms, late gaps (> %u ms) %u, session "
               "%u, thread priority %d\n", period, gap_max_ms_, 2u * period, gap_late_, gap_late_total_, prio_now_);
    }
    gap_max_ms_ = gap_late_ = 0;
    // Media drift correction: the trim now applied, the smoothed media queue and the level it holds.
    if (!drift_on_)
        printf("[Browser] media drift correction off (NBT_BROWSER_DRIFT=0)\n");
    else if (started_[MEDIA] && drift_.learning())
        printf("[Browser] media drift: learning the queue level (%.0f ms so far)\n", drift_.smoothed());
    else if (started_[MEDIA] && drift_pi_on_)
        // The integral settles on the phone's clock error: how far its media clock is from the car's.
        printf("[Browser] media drift (PI): trim %+d ppm, queue %.0f ms, target %.0f ms, off by %+.1f ms, phone clock "
               "%+.0f ppm\n", drift_applied_, drift_.smoothed(), drift_.target(), drift_.smoothed() - drift_.target(),
               drift_pi_.integral());
    else if (started_[MEDIA])
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
        rs_[s].setEngine(e, rs_quality_);   // restarts the conversion: a few ms discontinuity
        rs_[s].resetCounters();
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
    if (!interval_ms || !rate_) return;
    const double cpu_pct = (double)rs_us_ / 10.0 / (double)interval_ms;   // us / (ms * 1000) * 100
    char ratios[NSTREAMS][24];
    bool bad = false;
    for (int s = 0; s < NSTREAMS; ++s) {
        const uint64_t in = rs_[s].inFrames(), out = rs_[s].outFrames();
        ratios[s][0] = 0;
        snprintf(ratios[s], sizeof(ratios[s]), "-");
        if (in >= rs_rate_[s] && rs_rate_[s]) {   // at least a second of input
            const double want = (double)rate_ / (double)rs_rate_[s] / (1.0 + rs_[s].trimPpm() * 1e-6);
            const double got = (double)out / (double)in;
            const double err = got / want - 1.0;
            snprintf(ratios[s], sizeof(ratios[s]), "%+.3f %%", err * 100.0);
            if (err > 0.02 || err < -0.02) bad = true;
        }
        rs_[s].resetCounters();
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
            for (int s = 0; s < NSTREAMS; ++s) { discard(s); flush_[s] = true; }
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
        if (play) {
            // A playing stream that is a few ms short of a whole block: wait for it rather than padding a hole into
            // the music. Only right after a write that blocked (the device is full and still holds ~4 fragments,
            // ~92 ms), at most NBT_BROWSER_WAIT_MS per block; a stream that really stopped sending is padded at once
            // after the first timeout.
            uint32_t waited = 0;
            for (;;) {
                bool wait = false;
                for (int s = 0; s < NSTREAMS; ++s) {
                    const size_t avail = estimateOutFrames(ring_[s]->size(), pending_[s].size(), src_rate_[s],
                                                           kSrcCh[s], rate_, kOutCh);
                    if (guard_[s].shouldWait(started_[s], avail, block, waited, last_write_blocked)) wait = true;
                }
                if (!wait || stop_) break;
                usleep(kWaitStepMs * 1000u);
                waited += kWaitStepMs;
            }
            if (waited) { ++waited_blocks_; waited_ms_ += waited; }
            for (int s = 0; s < NSTREAMS; ++s) {
                const size_t queued_before = ring_[s]->size();
                size_t got = 0;
                have[s] = pull(s, block, &in[s][0], &loud[s], &got);
                watch_[s].onPull(started_[s], queued_before, got, block * kOutCh);
                guard_[s].onPulled(started_[s], got / kOutCh, block);
                checkUnderflow(s, got, block * kOutCh, now);
            }
        }

        // Media clock drift: once per block while media plays, hold its queue at the level the phone keeps by
        // trimming the media resampler (DriftControl). Reset at every media start / stop.
        if (drift_reset_) {
            drift_reset_ = false;
            drift_.reset();
            drift_pi_.reset();
            if (drift_applied_) { rs_[MEDIA].setTrimPpm(0); drift_applied_ = 0; }
        }
        if (drift_on_ && play && started_[MEDIA]) {
            const uint64_t in_bps = (uint64_t)src_rate_[MEDIA] * kSrcCh[MEDIA] * 2u;
            const double fill_ms = (in_bps ? (double)ring_[MEDIA]->size() * 1000.0 / (double)in_bps : 0.0) +
                                   (double)(pending_[MEDIA].size() / kOutCh) * 1000.0 / (double)rate_;
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
            if (ppm != drift_applied_) { rs_[MEDIA].setTrimPpm(ppm); drift_applied_ = ppm; }
        }
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

        // A write that took at least half a block period blocked: the device was full (WAIT above relies on this).
        const uint64_t write_start = nowMs();
        // Lateness: time since the previous write started. A blocking write paces it at one block period; much more
        // means the mixer thread was late (waiting for the CPU, or a slow log write), eating the device's headroom.
        if (last_write_ms_) {
            const uint32_t gap = (uint32_t)(write_start - last_write_ms_);
            if (gap > gap_max_ms_) gap_max_ms_ = gap;
            if (gap > 2u * (uint32_t)(block * 1000u / rate_)) { ++gap_late_; ++gap_late_total_; }
        }
        last_write_ms_ = write_start;
        const bool wrote = writePcm(&out[0], block);
        last_write_blocked = wrote && nowMs() - write_start >= (uint64_t)(block * 1000u / rate_) / 2u;
        if (wrote) {
            fail_run = 0;
            ++blocks_;
            written_frames += block;
        } else if (++fail_run == 50) {
            printf("[Browser] 50 failed writes in a row: reopening the device\n");
            closePcm();
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
        uint64_t played_ms = written_frames * 1000u / rate_;
        uint64_t elapsed = nowMs() - t0;
        if (played_ms > elapsed + kLeadMs) usleep((useconds_t)((played_ms - elapsed - kLeadMs) * 1000u));
        else if (elapsed > played_ms + 1000) { t0 = nowMs(); written_frames = 0; }   // fell behind: re-anchor

        logUnderflowFollowUp(nowMs());
        checkResamplerSwitch(nowMs());
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
        watch_[s].resetAll();
        guard_[s].reset();
        gap_[s] = false;
        srate_[s].reset();            // the next phone declares (or is measured) afresh
        src_rate_[s] = srate_[s].rate();
        frames_since_start_[s] = 0;
        dry_[s] = false;
        report_ms_[s] = 0;
        reports_[s] = 0;
    }
    last_write_ms_ = 0;
    gap_max_ms_ = gap_late_ = gap_late_total_ = 0;
    rs_us_ = 0;
    rs_failed_ = false;    // the next session tries the configured resampler again
    setResampler(rs_engine_cfg_, "new session");
    pcm_failed_ = false;   // try the device again next session
    pthread_mutex_lock(&lock_);
    route_backoff_until_ms_ = 0;
    pthread_mutex_unlock(&lock_);
    last_media_ms_ = 0;
    waited_ms_ = waited_blocks_ = 0;
    drift_.reset();   // the mixer thread is stopped: safe to touch its state
    drift_pi_.reset();
    drift_reset_ = false;
    rs_[MEDIA].setTrimPpm(0);
    drift_applied_ = 0;
}

void BrowserAudio::stop() {
    stopMixer();
    releaseRoute("shutdown");
    if (lib_) { dlclose(lib_); lib_ = NULL; }
}

} // namespace nbt
