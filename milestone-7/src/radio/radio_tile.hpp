// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#pragma once
// What the Android Auto media tile shows for the audible radio, and when to send it. Pure, no I/O, no clock.
#include "radio/radio_state.hpp"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

namespace nbt {

enum { kSettleMs = 300, kMaxDelayMs = 1000, kTuneHoldMs = 3000 };

struct RadioTileView {
    bool show;                  // false: send nothing now
    bool hold;                  // the shown tuner tunes while playing: keep the last tile until holdUntilMs
    uint32_t holdUntilMs;
    int state;                  // 1 stopped, 2 playing
    std::string source, song, artist, album;
    std::vector<uint8_t> art;
    RadioTileView() : show(false), hold(false), holdUntilMs(0), state(1) {}
};

struct RadioTileSent {
    bool valid;
    int state;
    std::string source, song, artist, album;
    std::vector<uint8_t> art;
    RadioTileSent() : valid(false), state(0) {}
};

namespace radio_detail {

inline std::string fallbackName(int kind, const TunerContent& t) {
    if (kind == kTunerDab) return "DAB";
    if (t.freqKHz == 0) return "FM";
    char buf[24];
    if (t.freqKHz < 30000) {
        snprintf(buf, sizeof(buf), "AM %u", (unsigned)t.freqKHz);
    } else {
        const unsigned tenths = (unsigned)((t.freqKHz + 50) / 100);
        snprintf(buf, sizeof(buf), "FM %u.%u", tenths / 10, tenths % 10);
    }
    return buf;
}

inline std::string secondLine(const TunerContent& t) {
    if (!t.artist.empty() && !t.title.empty()) return t.artist + " \xe2\x80\x93 " + t.title;
    if (!t.artist.empty()) return t.artist;
    if (!t.title.empty()) return t.title;
    return t.text;
}

}  // namespace radio_detail

// The one place the art passes before it is sent.
inline std::vector<uint8_t> radioTileArt(const TunerContent& t) {
    return !t.slide.empty() ? t.slide : t.logo;
}

inline RadioTileView radioTileView(const RadioState& s, bool phonePlaying, uint32_t nowMs) {
    RadioTileView v;
    int kind = s.tuner;
    if (kind == kTunerNone) kind = s.dab.spoke ? kTunerDab : (s.fm.spoke ? kTunerFm : kTunerNone);
    if (kind == kTunerNone) return v;
    const TunerContent& t = kind == kTunerDab ? s.dab : s.fm;

    v.state = s.audible == 1 ? 2 : (s.audible == 0 ? 1 : (phonePlaying ? 1 : 2));
    if (v.state == 2 && t.tuning && nowMs - t.tuneStartMs < (uint32_t)kTuneHoldMs) {   // STOPPED goes out at once
        v.hold = true;
        v.holdUntilMs = t.tuneStartMs + kTuneHoldMs;
        return v;
    }
    v.source = kind == kTunerDab ? "DAB" : "FM";
    v.song = t.name.empty() ? radio_detail::fallbackName(kind, t) : t.name;
    v.album = t.name;
    v.artist = radio_detail::secondLine(t);
    v.art = radioTileArt(t);
    v.show = !v.song.empty();
    return v;
}

inline bool radioTileChanged(const RadioTileSent& last, const RadioTileView& v) {
    return !last.valid || last.state != v.state || last.source != v.source || last.song != v.song ||
           last.artist != v.artist || last.album != v.album || last.art != v.art;
}

inline void radioTileRemember(RadioTileSent& last, const RadioTileView& v) {
    last.valid = true;
    last.state = v.state;
    last.source = v.source;
    last.song = v.song;
    last.artist = v.artist;
    last.album = v.album;
    last.art = v.art;
}

// Collects changes: due kSettleMs after the last one or kMaxDelayMs after the first; holdUntil arms an extra due time.
class RadioTilePacer {
public:
    RadioTilePacer() { reset(); }

    void reset() {
        pending_ = false;
        hold_ = false;
        firstMs_ = lastMs_ = holdMs_ = 0;
    }

    void changed(uint32_t nowMs) {
        if (!pending_) {
            pending_ = true;
            firstMs_ = nowMs;
        }
        lastMs_ = nowMs;
    }

    void holdUntil(uint32_t whenMs) {
        hold_ = true;
        holdMs_ = whenMs;
    }

    bool due(uint32_t nowMs) const { return msUntilDue(nowMs) == 0; }

    uint32_t msUntilDue(uint32_t nowMs) const {
        uint32_t best = 0xFFFFFFFFu;
        if (pending_) {
            best = remaining(nowMs, lastMs_, kSettleMs);
            const uint32_t m = remaining(nowMs, firstMs_, kMaxDelayMs);
            if (m < best) best = m;
        }
        if (hold_) {
            const uint32_t h = (int32_t)(nowMs - holdMs_) >= 0 ? 0 : holdMs_ - nowMs;
            if (h < best) best = h;
        }
        return best;
    }

    void sent() {
        pending_ = false;
        hold_ = false;
    }

private:
    static uint32_t remaining(uint32_t nowMs, uint32_t sinceMs, uint32_t spanMs) {
        const uint32_t elapsed = nowMs - sinceMs;
        return elapsed >= spanMs ? 0 : spanMs - elapsed;
    }

    bool pending_, hold_;
    uint32_t firstMs_, lastMs_, holdMs_;
};

}  // namespace nbt
