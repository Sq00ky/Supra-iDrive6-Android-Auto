// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "audio/asound_device.hpp"
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

int envInt(const char* name, int def) {
    const char* e = getenv(name);
    if (!e || !*e) return def;
    char* end = NULL;
    long v = strtol(e, &end, 0);
    return (end && end != e) ? (int)v : def;
}

} // namespace

const uint32_t AsoundDevice::kChannels;

AsoundDevice::AsoundDevice()
    : shim_path_(NULL), want_rate_(48000), force_rate_(0), lib_(NULL), pcm_(NULL), rate_(0), frag_bytes_(0),
      write_errors_(0), prepares_(0), writes_(0), writes_blocked_(0), lead_min_(0), lead_max_(0), lead_seen_(false) {
    memset(fn_, 0, sizeof(fn_));
}

AsoundDevice::~AsoundDevice() {}

void AsoundDevice::configure(const char* shim_path, uint32_t want_rate, uint32_t force_rate) {
    shim_path_ = shim_path;
    want_rate_ = want_rate;
    force_rate_ = force_rate;
}

bool AsoundDevice::load() {
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

void AsoundDevice::unload() {
    if (lib_) { dlclose(lib_); lib_ = NULL; }
}

// The shim hands our parameters to A2I_Client_Open (liba2i_client check_parameter), which only takes a package
// (= fragment) size that is a power of two of at least 32 bytes, 1..256 packages, and a whole buffer of at least twice
// the device period. Car log 2026-11-01: "check_parameter: Invalid uSizeOfPackage: 3528" (20 ms at 44.1 kHz), which
// snd_pcm_plugin_params reports as -22. A refused open can leave the handle half set up, so every try reopens it.
bool AsoundDevice::open() {
    if (pcm_) return true;
    if (!lib_ && !load()) return false;
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
        if (openWith(tries[t][0], tries[t][1])) return true;
    }
    return false;
}

bool AsoundDevice::openWith(int frag, int frags) {
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
    p.format.voices = (int32_t)kChannels;
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
        if (su.format.voices > 0 && su.format.voices != (int)kChannels)
            printf("[Browser] WARNING: device gave %d voices, the mixer writes %u\n", su.format.voices, kChannels);
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
    return true;
}

void AsoundDevice::close() {
    if (!pcm_) return;
    snd_pcm_t* h = (snd_pcm_t*)pcm_;
    if (fn_[F_FLUSH]) ((FnFlush)fn_[F_FLUSH])(h, SND_PCM_CHANNEL_PLAYBACK);
    ((FnClose)fn_[F_CLOSE])(h);
    pcm_ = NULL;
    printf("[Browser] PCM closed\n");
}

bool AsoundDevice::write(const int16_t* buf, size_t frames) {
    snd_pcm_t* h = (snd_pcm_t*)pcm_;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(buf);
    size_t left = frames * kChannels * 2;
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

} // namespace nbt
