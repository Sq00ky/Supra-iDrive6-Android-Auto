// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef AUDIO_ASOUND_DEVICE_HPP
#define AUDIO_ASOUND_DEVICE_HPP

// The browser mode's output device: the car's libasound shim (/opt/mm/lib/libasound/libasound.so, dlopen'ed: a
// multi-client front end of dev-libasound feeding A2I input_stereo_3, see browser_audio.hpp), opened for 16-bit
// stereo playback. Its log lines keep the [Browser] prefix. Used by the mixer thread only (load() also at init).
// The asound types stay in asound_device.cpp, so this header builds on the host too.

#include <stdint.h>
#include <stddef.h>

namespace nbt {

// The output device rate to ask for. want = the configured rate, kept if the device's reported range allows it, else
// its maximum (this car's browser device reports 22050..44100, so 48000 becomes 44100). force (non-zero) is asked for
// as is, whatever the device reports (NBT_BROWSER_FORCE_RATE experiment). min/max <= 0 = range unknown.
inline uint32_t pickDeviceRate(uint32_t want, uint32_t force, int min_rate, int max_rate) {
    if (force) return force;
    if (max_rate > 0 && ((int64_t)want > max_rate || (min_rate > 0 && (int64_t)want < min_rate)))
        return (uint32_t)max_rate;
    return want;
}

class AsoundDevice {
public:
    static const uint32_t kChannels = 2;   // stereo, interleaved

    AsoundDevice();
    ~AsoundDevice();

    // shim_path: the library to dlopen; want_rate / force_rate: see pickDeviceRate.
    void configure(const char* shim_path, uint32_t want_rate, uint32_t force_rate);
    bool load();     // dlopen the shim and look up its entry points (already loaded: true)
    void unload();   // dlclose (shutdown)

    // Opens the device (NBT_BROWSER_PCM / NBT_BROWSER_FRAG / NBT_BROWSER_FRAGS): the configured fragment size first,
    // then a deeper and a larger buffer. Already open: true.
    bool open();
    void close();
    bool isOpen() const { return pcm_ != NULL; }
    uint32_t rate() const { return rate_; }             // the rate actually set up
    uint32_t fragBytes() const { return frag_bytes_; }  // one device fragment = one mixer block

    // Writes frames of interleaved stereo; a short write (underrun, channel stopped) re-prepares and carries on.
    bool write(const int16_t* buf, size_t frames);
    uint32_t writeErrors() const { return write_errors_; }
    uint32_t prepares() const { return prepares_; }

    // Wait room per stats interval (whether waitIsSafe lets the mixer wait for late input): device writes, the ones
    // that blocked (the device was full), and the lowest / highest lead over real time seen before a block.
    void noteWrite(bool blocked) { ++writes_; if (blocked) ++writes_blocked_; }
    void noteLead(int64_t lead_ms) {
        if (!lead_seen_ || lead_ms < lead_min_) lead_min_ = lead_ms;
        if (!lead_seen_ || lead_ms > lead_max_) lead_max_ = lead_ms;
        lead_seen_ = true;
    }
    uint32_t writes() const { return writes_; }
    uint32_t writesBlocked() const { return writes_blocked_; }
    bool leadSeen() const { return lead_seen_; }
    int64_t leadMin() const { return lead_min_; }
    int64_t leadMax() const { return lead_max_; }
    void resetWaitRoom() { writes_ = writes_blocked_ = 0; lead_seen_ = false; }

private:
    AsoundDevice(const AsoundDevice&);
    AsoundDevice& operator=(const AsoundDevice&);

    bool openWith(int frag, int frags);

    const char* shim_path_;
    uint32_t want_rate_;
    uint32_t force_rate_;    // NBT_BROWSER_FORCE_RATE: ask the device for this rate whatever it reports (0 = off)

    // libasound shim (dlopen'ed, function pointers held as void* to keep the asound types out of this header)
    void* lib_;
    void* fn_[11];
    void* pcm_;
    uint32_t rate_;          // device rate actually set up
    uint32_t frag_bytes_;

    uint32_t write_errors_;
    uint32_t prepares_;
    uint32_t writes_, writes_blocked_;
    int64_t lead_min_, lead_max_;
    bool lead_seen_;
};

} // namespace nbt

#endif // AUDIO_ASOUND_DEVICE_HPP
