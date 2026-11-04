// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "audio/mic_uplink.hpp"
#include "nbt_env.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

namespace nbt {

bool micEnabled() {
    if (envFlag("NBT_MIC") == 0) return false;
    return access("/opt/aa/mic.disable", F_OK) != 0;
}

bool MicUplink::start() {
    if (mic_.isRunning()) return true;
    if (!mic_.isOpen()) {
        A2IStreamConfig cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.endpoint          = A2I_ENDPOINT_MIC_UL;
        cfg.fallback_endpoint = NULL;
        cfg.sample_rate       = 16000;
        cfg.channels          = A2I_CHANNELS_MONO;
        cfg.buffer_size       = A2I_BUFFER_SIZE_MONO;
        cfg.cookie_primary    = A2I_COOKIE_LEGACY_CAPTURE;
        cfg.cookie_alternate  = A2I_COOKIE_FACTORY;
        cfg.timeout_ms        = 2000;
        if (!mic_.open(cfg)) {
            printf("[Mic] could not open %s\n", A2I_ENDPOINT_MIC_UL);
            return false;
        }
    }
    return mic_.start(cb_, user_);
}

void micLevelMeter(const uint8_t* pcm_data, size_t size) {
    static uint32_t acc_n = 0, acc_peak = 0, zero_chunks = 0, chunks = 0;
    static double acc_sq = 0;
    static FILE* dump = NULL;
    static int dump_state = -1;              // -1 unknown, 0 off, 1 on, 2 full
    static uint32_t dumped = 0;
    const int16_t* s = reinterpret_cast<const int16_t*>(pcm_data);
    size_t n = size / 2;
    bool all_zero = true;
    for (size_t i = 0; i < n; ++i) {
        int v = s[i];
        if (v) all_zero = false;
        uint32_t a = (uint32_t)(v < 0 ? -v : v);
        if (a > acc_peak) acc_peak = a;
        acc_sq += (double)v * v;
    }
    acc_n += (uint32_t)n;
    ++chunks;
    if (all_zero) ++zero_chunks;
    if (acc_n >= 16000) {
        double rms = 0;
        { double m = acc_sq / acc_n; double r = m > 0 ? m : 0; /* sqrt without libm dependency issues */
          double x = r > 1 ? r : 1; for (int k = 0; k < 20; ++k) x = 0.5 * (x + r / x); rms = r > 0 ? x : 0; }
        printf("[Mic] level: peak %u rms %d (of 32767), %u/%u chunks all-zero, chunk %u bytes\n",
               acc_peak, (int)rms, zero_chunks, chunks, (unsigned)size);
        acc_n = acc_peak = zero_chunks = chunks = 0;
        acc_sq = 0;
    }
    if (dump_state < 0) dump_state = getenv("NBT_MIC_DUMP") ? 1 : 0;
    if (dump_state == 1) {
        if (!dump) dump = fopen("/dev/shmem/nbt-aad-mic.raw", "wb");
        if (dump) {
            fwrite(pcm_data, 1, size, dump);
            dumped += (uint32_t)size;
            if (dumped >= 30u * 32000u) { fclose(dump); dump = NULL; dump_state = 2; printf("[Mic] dump complete (/dev/shmem/nbt-aad-mic.raw)\n"); }
            else fflush(dump);
        }
    }
}

} // namespace nbt
