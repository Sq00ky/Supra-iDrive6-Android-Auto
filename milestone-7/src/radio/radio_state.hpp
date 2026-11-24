// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#pragma once
// Radio state per tuner (DAB, FM) from tuner and connection master messages. Pure, no I/O.
#include "radio/most_tuner.hpp"

#include <string.h>
#include <string>
#include <utility>
#include <vector>

namespace nbt {

enum RadioTunerKind { kTunerNone = 0, kTunerDab = 1, kTunerFm = 2 };
enum { kRadioMaxDabNames = 32 };

struct TunerContent {
    std::string name, title, artist, text;
    std::vector<uint8_t> slide, logo;
    int slideId, pendingSlide;    // -1 none
    bool tuning;
    uint32_t tuneStartMs;
    bool spoke;                   // any message since start
    uint32_t id;                  // DAB: SId; FM: PI, or the frequency without PI; 0 unknown
    bool idIsFreq;                // FM: id is the frequency
    uint32_t freqKHz;             // FM
    TunerContent()
        : slideId(-1), pendingSlide(-1), tuning(false), tuneStartMs(0), spoke(false), id(0), idIsFreq(false),
          freqKHz(0) {}
};

struct RadioState {
    int audible;                  // -1 no C03 yet, 0, 1
    int tuner;                    // RadioTunerKind of the audible tuner, kTunerNone before the first one
    bool slideAnnounceSeen;       // any C44 or DAB station change since start
    TunerContent dab, fm;
    std::vector<std::pair<uint32_t, std::string> > dabNames;   // SId -> station name, newest last
    uint32_t generation;          // bumped on every visible change
    RadioState() : audible(-1), tuner(kTunerNone), slideAnnounceSeen(false), generation(0) {}
};

// NULL when unknown; valid until the next applyMost.
inline const std::string* dabNameFor(const RadioState& s, uint32_t sid) {
    for (size_t i = 0; i < s.dabNames.size(); ++i)
        if (s.dabNames[i].first == sid) return &s.dabNames[i].second;
    return NULL;
}

// The tuner the tile shows: the audible one, before the first one the one that spoke, DAB first.
inline int shownTuner(const RadioState& s) {
    if (s.tuner != kTunerNone) return s.tuner;
    return s.dab.spoke ? kTunerDab : (s.fm.spoke ? kTunerFm : kTunerNone);
}

namespace radio_detail {

inline bool setStr(std::string& dst, const std::string& v) {
    if (dst == v) return false;
    dst = v;
    return true;
}

inline bool setFlag(bool& dst, bool v) {
    if (dst == v) return false;
    dst = v;
    return true;
}

inline bool setImg(std::vector<uint8_t>& dst, const uint8_t* img, size_t len) {
    if (dst.size() == len && (len == 0 || memcmp(&dst[0], img, len) == 0)) return false;
    dst.assign(img, img + len);
    return true;
}

inline bool clearImg(std::vector<uint8_t>& dst) {
    if (dst.empty()) return false;
    dst.clear();
    return true;
}

// Drops a station's content except name and tuning, which the caller sets; true when any of it was visible.
inline bool dropContent(TunerContent& t) {
    bool ch = !t.title.empty() || !t.artist.empty() || !t.text.empty();
    t.title.clear();
    t.artist.clear();
    t.text.clear();
    ch |= clearImg(t.slide);
    ch |= clearImg(t.logo);
    t.slideId = -1;
    t.pendingSlide = -1;
    return ch;
}

inline void learnDabName(RadioState& s, uint32_t sid, const std::string& name) {
    for (size_t i = 0; i < s.dabNames.size(); ++i) {
        if (s.dabNames[i].first == sid) {
            s.dabNames.erase(s.dabNames.begin() + i);
            break;
        }
    }
    if (s.dabNames.size() >= kRadioMaxDabNames) s.dabNames.erase(s.dabNames.begin());
    s.dabNames.push_back(std::make_pair(sid, name));
}

inline bool applyDabStation(RadioState& s, const DabStation& st, uint32_t nowMs) {
    TunerContent& d = s.dab;
    bool ch = false;
    if (st.tuning) {
        ch |= dropContent(d);
        ch |= setStr(d.name, std::string());
        ch |= setFlag(d.tuning, true);
        d.id = 0;
        d.tuneStartMs = nowMs;
        s.slideAnnounceSeen = true;
        return ch;
    }
    if (d.id != 0 && d.id != st.sid) {
        ch |= dropContent(d);
        s.slideAnnounceSeen = true;
    }
    const std::string& name = st.longName.empty() ? st.shortName : st.longName;
    d.id = st.sid;
    ch |= setStr(d.name, name);
    ch |= setFlag(d.tuning, false);
    if (st.sid != 0) learnDabName(s, st.sid, name);
    return ch;
}

inline bool acceptSlide(const RadioState& s, int id) {
    return id == s.dab.pendingSlide || !s.slideAnnounceSeen || id == s.dab.slideId;
}

// False when the message is not one the state uses or does not decode; ch is set on a visible change.
inline bool applyDab(RadioState& s, uint16_t fkt, const uint8_t* p, size_t n, uint32_t nowMs, bool& ch) {
    TunerContent& d = s.dab;
    switch (fkt) {
    case kMostFktDabStation: {
        DabStation st;
        if (!decodeDabStation(p, n, st)) return false;
        ch |= applyDabStation(s, st, nowMs);
        return true;
    }
    case kMostFktDynamicLabel: {
        std::string text;
        if (!decodeDynamicLabel(p, n, text)) return false;
        ch |= setStr(d.text, text);
        return true;
    }
    case kMostFktDlPlus: {
        std::string title, artist;
        if (!decodeDlPlus(p, n, title, artist)) return false;
        ch |= setStr(d.title, title);
        ch |= setStr(d.artist, artist);
        return true;
    }
    case kMostFktLogo: {
        const uint8_t* img = NULL;
        size_t len = 0;
        if (decodeLogo(p, n, img, len)) ch |= setImg(d.logo, img, len);
        else if (mostNoLogo(p, n)) ch |= clearImg(d.logo);
        else return false;
        return true;
    }
    case kMostFktSlideAnnounce: {
        if (mostNoSlideshow(p, n)) {
            ch |= clearImg(d.slide);
            d.slideId = -1;
            d.pendingSlide = -1;
        } else {
            const int id = decodeSlideAnnounce(p, n);
            if (id < 0) return false;
            d.pendingSlide = id;
        }
        s.slideAnnounceSeen = true;
        return true;
    }
    case kMostFktSlide: {
        int id = -1;
        const uint8_t* img = NULL;
        size_t len = 0;
        if (!decodeSlide(p, n, id, img, len)) return false;
        if (acceptSlide(s, id)) {
            ch |= setImg(d.slide, img, len);
            d.slideId = id;
            if (id == d.pendingSlide) d.pendingSlide = -1;
        }
        return true;
    }
    default:
        return false;
    }
}

inline bool applyFmStation(RadioState& s, const FmStation& st, uint32_t nowMs) {
    TunerContent& f = s.fm;
    const bool hasPi = st.pi >= 0;
    const std::string* dabName = hasPi ? dabNameFor(s, (uint32_t)st.pi) : NULL;
    const uint32_t oldFreq = f.freqKHz;
    const bool sameFreq = st.freqKHz == f.freqKHz;
    // Same PI on any frequency, or a PI coming or going on the tuner's frequency. A PI station that loses its PI on
    // another frequency cannot be told from a new station, so that is a station change.
    const bool same = f.id == 0 || (hasPi ? f.id == (uint32_t)st.pi || (f.idIsFreq && sameFreq) : sameFreq);
    bool ch = false;
    if (!same) {
        const std::string& name = dabName ? *dabName : st.ps;
        ch |= dropContent(f);
        ch |= setStr(f.name, name);
        ch |= setFlag(f.tuning, name.empty());
        if (name.empty()) f.tuneStartMs = nowMs;
    } else {
        // Same station, or the first since start: the PS may change, the name stays unless the PI has a DAB name.
        if (dabName) ch |= setStr(f.name, *dabName);
        else if (f.name.empty()) ch |= setStr(f.name, st.ps);
        if (!f.name.empty()) ch |= setFlag(f.tuning, false);
    }
    if (hasPi) {
        f.id = (uint32_t)st.pi;
        f.idIsFreq = false;
    } else if (!same || f.id == 0) {
        f.id = st.freqKHz;
        f.idIsFreq = true;
    }
    f.freqKHz = st.freqKHz;
    if (f.name.empty() && f.freqKHz != oldFreq) ch = true;   // the fallback name shows the frequency
    return ch;
}

inline bool applyFm(RadioState& s, uint16_t fkt, const uint8_t* p, size_t n, uint32_t nowMs, bool& ch) {
    TunerContent& f = s.fm;
    switch (fkt) {
    case kMostFktFmStation: {
        FmStation st;
        if (!decodeFmStation(p, n, st)) return false;
        ch |= applyFmStation(s, st, nowMs);
        return true;
    }
    case kMostFktRadioText: {
        std::string text;
        if (!decodeRadioText(p, n, text)) return false;
        ch |= setStr(f.text, text);
        return true;
    }
    case kMostFktDlPlus: {
        std::string title, artist;
        if (!decodeDlPlus(p, n, title, artist)) return false;
        ch |= setStr(f.title, title);
        ch |= setStr(f.artist, artist);
        return true;
    }
    default:
        return false;
    }
}

inline bool applyConnectionTable(RadioState& s, const uint8_t* p, size_t n, bool& ch) {
    if (!connectionTableValid(p, n)) return false;
    const uint8_t src = audibleTuner(p, n);
    const int audible = src != 0 ? 1 : 0;
    if (s.audible != audible) {
        s.audible = audible;
        ch = true;
    }
    if (src != 0) {
        const int kind = mostIsDabSource(src) ? kTunerDab : kTunerFm;
        if (s.tuner != kind) {
            s.tuner = kind;
            ch = true;
        }
    }
    return true;
}

}  // namespace radio_detail

// DAB messages change only s.dab, FM messages only s.fm; C03 picks the audible tuner. True on a visible change.
inline bool applyMost(RadioState& s, uint8_t fblock, uint16_t fkt, const uint8_t* p, size_t n, uint32_t nowMs) {
    bool ch = false;
    if (fblock == kMostFBlockDabTuner) {
        if (!radio_detail::applyDab(s, fkt, p, n, nowMs, ch)) return false;
        ch |= radio_detail::setFlag(s.dab.spoke, true);
    } else if (fblock == kMostFBlockFmTuner) {
        if (!radio_detail::applyFm(s, fkt, p, n, nowMs, ch)) return false;
        ch |= radio_detail::setFlag(s.fm.spoke, true);
    } else if (fblock == kMostFBlockConnectionMaster && fkt == kMostFktConnectionTable) {
        if (!radio_detail::applyConnectionTable(s, p, n, ch)) return false;
    } else {
        return false;
    }
    if (ch) ++s.generation;
    return ch;
}

}  // namespace nbt
