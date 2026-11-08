// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef NBT_MEDIA_STATE_HPP
#define NBT_MEDIA_STATE_HPP

// The merged "now playing" the phone reports on the media playback channel (12), and the small text record the daemon
// publishes for the iDrive app. Host-tested (tests/test_media_state.cpp).
//
// The phone sends two messages: 0x8001 MediaPlaybackStatus about once a second (state, app name, position, shuffle /
// repeat) and 0x8003 MediaPlaybackMetadata when the track changes (song, artist, album, playlist, duration, album art).
// Either can arrive alone, a truncated one must not wipe the last known values, and a status without a state field
// (or a session that has not sent anything yet) is "unknown" rather than "stopped".
//
// Record (/dev/shmem/nbt-aad-media), one "key=value" per line, written whole and in place, at most kRecordMax bytes:
//   state=playing        stopped | playing | paused | unknown
//   source=Spotify       the app name the phone reports
//   song=..., artist=..., album=..., playlist=...
//   pos=57  dur=214      seconds
//   shuffle=0 repeat=0 repeatone=1
//   art=12345            bytes of album art in the metadata message (the image itself is not copied)
//   have=3               bit 1 status seen, bit 2 metadata seen
// Values never contain control characters (they would break the line format) and are cut at kValueMax bytes, which is
// more than any iDrive list row shows.

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <string>

#include "audio/aa_media_meta.hpp"

namespace nbt {

enum { kMediaValueMax = 96, kMediaRecordMax = 1024 };

struct MediaState {
    unsigned state;             // 0 unknown, 1 stopped, 2 playing, 3 paused (the phone's enum)
    std::string source, song, artist, album, playlist;
    unsigned duration, position;
    bool shuffle, repeat, repeatOne;
    unsigned long artBytes;
    bool haveStatus, haveMetadata;
    unsigned long updatedMs;    // when the last message arrived (monotonic, owner's clock)

    MediaState() : state(0), duration(0), position(0), shuffle(false), repeat(false), repeatOne(false),
                   artBytes(0), haveStatus(false), haveMetadata(false), updatedMs(0) {}

    void clear() { *this = MediaState(); }
};

// A value as it can go into the record: no control characters (a phone app name with a newline would forge a line),
// runs of whitespace kept as they are, the ends trimmed, cut at kValueMax bytes without splitting a UTF-8 sequence.
inline std::string mediaClean(const std::string& s) {
    std::string out;
    out.reserve(s.size() < (size_t)kMediaValueMax ? s.size() : (size_t)kMediaValueMax);
    for (size_t i = 0; i < s.size() && out.size() < (size_t)kMediaValueMax; ++i) {
        unsigned char c = (unsigned char)s[i];
        out += (c < 0x20 || c == 0x7F) ? ' ' : (char)c;
    }
    size_t b = out.find_first_not_of(' ');
    size_t e = out.find_last_not_of(' ');
    if (b == std::string::npos) return std::string();
    out = out.substr(b, e - b + 1);
    // Never leave half a UTF-8 sequence at the cut: find where the last sequence starts and drop it if it is incomplete.
    if (!out.empty()) {
        size_t i = out.size();
        while (i > 0 && ((unsigned char)out[i - 1] & 0xC0) == 0x80) --i;   // back to the lead byte of the last sequence
        if (i > 0) {
            const unsigned char c = (unsigned char)out[i - 1];
            const size_t need = (c & 0x80) == 0 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 1;
            if (out.size() - (i - 1) < need) out.erase(i - 1);
        }
    }
    return out;
}

// The phone's messages have to be complete: aaParseMedia* fill what they got, so a body cut inside a string would
// replace the first fields and leave the rest stale. Nothing is merged unless the whole body parses.
inline bool mediaBodyComplete(const uint8_t* p, size_t n) {
    if (!p) return false;
    proto::PbReader r(p, n);
    proto::PbField f;
    while (r.next(f)) {}
    return r.ok();
}

// Merge one channel-12 message into `st`. Returns true when anything changed (the caller only rewrites the record then).
inline bool mediaMerge(MediaState& st, uint16_t msgId, const uint8_t* body, size_t len) {
    if (msgId != 0x8001 && msgId != 0x8003) return false;
    if (!mediaBodyComplete(body, len)) return false;
    if (msgId == 0x8001) {
        AaMediaStatus m;
        if (!aaParseMediaStatus(body, len, m)) return false;
        bool changed = false;
        if (m.state && m.state != st.state) { st.state = m.state; changed = true; }
        if (!m.source.empty() && m.source != st.source) { st.source = mediaClean(m.source); changed = true; }
        if (m.position != st.position) { st.position = m.position; changed = true; }
        if (m.shuffle != st.shuffle) { st.shuffle = m.shuffle; changed = true; }
        if (m.repeat != st.repeat) { st.repeat = m.repeat; changed = true; }
        if (m.repeatOne != st.repeatOne) { st.repeatOne = m.repeatOne; changed = true; }
        if (!st.haveStatus) { st.haveStatus = true; changed = true; }
        return changed;
    }
    if (msgId == 0x8003) {
        AaMediaMetadata m;
        if (!aaParseMediaMetadata(body, len, m)) return false;
        bool changed = false;
        // An empty field is "the phone did not send it", not "it was cleared": keep the last known value.
        std::string song = mediaClean(m.song), artist = mediaClean(m.artist), album = mediaClean(m.album),
                    playlist = mediaClean(m.playlist);
        if (!song.empty() && song != st.song) { st.song = song; changed = true; }
        if (!artist.empty() && artist != st.artist) { st.artist = artist; changed = true; }
        if (!album.empty() && album != st.album) { st.album = album; changed = true; }
        if (!playlist.empty() && playlist != st.playlist) { st.playlist = playlist; changed = true; }
        if (m.duration && m.duration != st.duration) { st.duration = m.duration; changed = true; }
        if (m.albumArtBytes != (size_t)st.artBytes) { st.artBytes = (unsigned long)m.albumArtBytes; changed = true; }
        if (!st.haveMetadata) { st.haveMetadata = true; changed = true; }
        return changed;
    }
    return false;
}

inline const char* mediaStateName(unsigned s) {
    switch (s) {
        case 1: return "stopped";
        case 2: return "playing";
        case 3: return "paused";
        default: return "unknown";
    }
}

// The record the iDrive app reads. Empty strings are written as empty values, so a reader never has to tell "no line"
// from "no value".
inline std::string mediaRecord(const MediaState& st) {
    std::string r;
    char line[64];
    snprintf(line, sizeof(line), "state=%s\n", mediaStateName(st.state));
    r += line;
    r += "source=" + st.source + "\n";
    r += "song=" + st.song + "\n";
    r += "artist=" + st.artist + "\n";
    r += "album=" + st.album + "\n";
    r += "playlist=" + st.playlist + "\n";
    snprintf(line, sizeof(line), "pos=%u\n", st.position);
    r += line;
    snprintf(line, sizeof(line), "dur=%u\n", st.duration);
    r += line;
    snprintf(line, sizeof(line), "shuffle=%d\nrepeat=%d\nrepeatone=%d\n", st.shuffle ? 1 : 0, st.repeat ? 1 : 0,
             st.repeatOne ? 1 : 0);
    r += line;
    snprintf(line, sizeof(line), "art=%lu\n", st.artBytes);
    r += line;
    snprintf(line, sizeof(line), "have=%u\n", (st.haveStatus ? 1u : 0u) | (st.haveMetadata ? 2u : 0u));
    r += line;
    if (r.size() > (size_t)kMediaRecordMax) r.erase(kMediaRecordMax);
    return r;
}

// One log line: what the phone is playing and where in the track it is.
inline std::string mediaLine(const MediaState& st) {
    std::string s = st.song.empty() ? std::string("(no metadata)") : st.song;
    if (!st.song.empty() && !st.artist.empty()) { s += " - "; s += st.artist; }
    char tail[192];
    snprintf(tail, sizeof(tail), " [%s%s%s%s%s, %us/%us%s]", mediaStateName(st.state),
             st.source.empty() ? "" : ", ", st.source.c_str(),
             st.album.empty() ? "" : ", album ", st.album.c_str(), st.position, st.duration,
             st.shuffle ? ", shuffle" : "");
    s += tail;
    return s;
}

} // namespace nbt

#endif // NBT_MEDIA_STATE_HPP
