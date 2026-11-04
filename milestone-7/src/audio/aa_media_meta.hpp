// Android Auto media playback messages (channel 12) - plain-protobuf decoders.
//
// The phone sends two messages on the media playback channel, and nbt-aad used to read one varint out of the first
// one and throw the rest away. They carry everything an iDrive "now playing" needs:
//
//   0x8001 MediaPlaybackStatus   { state=1 enum, media_source=2 string ("Apple Music", "Spotify", ...),
//                                  playback_seconds=3 uint32, shuffle=4 bool, repeat=5 bool, repeat_one=6 bool }
//   0x8003 MediaPlaybackMetadata { song=1 string, artist=2 string, album=3 string, album_art=4 bytes (a JPEG/PNG,
//                                  not a URL), playlist=5 string, duration_seconds=6 uint32, rating=7 int32 }
//
// Message ids (aap_protobuf/service/mediaplayback): MEDIA_PLAYBACK_STATUS = 0x8001, MEDIA_PLAYBACK_INPUT = 0x8002,
// MEDIA_PLAYBACK_METADATA = 0x8003. State enum: STOPPED = 1, PLAYING = 2, PAUSED = 3.
//
// This header is deliberately decoder-only (no allocation of album art: only its size is reported), so it can be
// unit-tested on the host and used from the session thread without surprises.
#ifndef NBT_AA_MEDIA_META_HPP
#define NBT_AA_MEDIA_META_HPP

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <string>

namespace nbt {

struct AaMediaStatus {
    unsigned state;             // 1 stopped, 2 playing, 3 paused (0 = not seen)
    std::string source;         // media_source: the app name, e.g. "Apple Music"
    unsigned position;          // playback_seconds
    bool shuffle;
    bool repeat;
    bool repeatOne;
    AaMediaStatus() : state(0), position(0), shuffle(false), repeat(false), repeatOne(false) {}
};

struct AaMediaMetadata {
    std::string song;
    std::string artist;
    std::string album;
    std::string playlist;
    unsigned duration;          // duration_seconds
    size_t albumArtBytes;       // field 4 is the image itself; only its length is kept here
    AaMediaMetadata() : duration(0), albumArtBytes(0) {}
};

// One protobuf field of a message body.
struct PbField {
    unsigned number;
    unsigned wire;              // 0 varint, 1 fixed64, 2 length-delimited, 5 fixed32
    uint64_t varint;
    const uint8_t* data;        // length-delimited payload (may be NULL for an empty one)
    size_t len;
};

// Minimal varint reader: returns false on truncation or a >32-bit key.
inline bool pbReadVarint(const uint8_t* p, size_t n, size_t& pos, uint64_t& v) {
    v = 0;
    unsigned shift = 0;
    while (pos < n) {
        uint8_t b = p[pos++];
        v |= (uint64_t)(b & 0x7Fu) << shift;
        if (!(b & 0x80u)) return true;
        shift += 7;
        if (shift > 63) return false;
    }
    return false;
}

// Next field of the body; advances `pos`. False at the end or on a malformed message.
inline bool pbNextField(const uint8_t* p, size_t n, size_t& pos, PbField& f) {
    if (pos >= n) return false;
    uint64_t key;
    if (!pbReadVarint(p, n, pos, key)) return false;
    f.number = (unsigned)(key >> 3);
    f.wire = (unsigned)(key & 7u);
    f.varint = 0;
    f.data = NULL;
    f.len = 0;
    if (f.number == 0) return false;
    switch (f.wire) {
        case 0:
            return pbReadVarint(p, n, pos, f.varint);
        case 1:
            if (pos + 8 > n) return false;
            pos += 8;
            return true;
        case 2: {
            uint64_t len;
            if (!pbReadVarint(p, n, pos, len)) return false;
            if (len > (uint64_t)(n - pos)) return false;
            f.data = p + pos;
            f.len = (size_t)len;
            pos += (size_t)len;
            return true;
        }
        case 5:
            if (pos + 4 > n) return false;
            pos += 4;
            return true;
        default:
            return false;       // groups (3/4) never appear in these messages
    }
}

inline std::string pbAsString(const PbField& f) {
    return f.data ? std::string((const char*)f.data, f.len) : std::string();
}

// 0x8001 MediaPlaybackStatus. Returns false when the body is not parseable (a partial message still fills what it
// got - the phone sends this once a second and a truncated one must not poison the last known state).
inline bool aaParseMediaStatus(const uint8_t* p, size_t n, AaMediaStatus& out) {
    if (!p) return false;
    bool any = false;
    size_t pos = 0;
    PbField f;
    while (pbNextField(p, n, pos, f)) {
        switch (f.number) {
            case 1:
                if (f.wire == 0) { out.state = (unsigned)f.varint; any = true; }
                break;
            case 2:
                if (f.wire == 2) { out.source = pbAsString(f); any = true; }
                break;
            case 3:
                if (f.wire == 0) { out.position = (unsigned)f.varint; any = true; }
                break;
            case 4:
                if (f.wire == 0) { out.shuffle = f.varint != 0; any = true; }
                break;
            case 5:
                if (f.wire == 0) { out.repeat = f.varint != 0; any = true; }
                break;
            case 6:
                if (f.wire == 0) { out.repeatOne = f.varint != 0; any = true; }
                break;
            default:
                break;
        }
    }
    return any;
}

// 0x8003 MediaPlaybackMetadata. Album art is only measured, never copied.
inline bool aaParseMediaMetadata(const uint8_t* p, size_t n, AaMediaMetadata& out) {
    if (!p) return false;
    bool any = false;
    size_t pos = 0;
    PbField f;
    while (pbNextField(p, n, pos, f)) {
        switch (f.number) {
            case 1:
                if (f.wire == 2) { out.song = pbAsString(f); any = true; }
                break;
            case 2:
                if (f.wire == 2) { out.artist = pbAsString(f); any = true; }
                break;
            case 3:
                if (f.wire == 2) { out.album = pbAsString(f); any = true; }
                break;
            case 4:
                if (f.wire == 2) { out.albumArtBytes = f.len; any = true; }
                break;
            case 5:
                if (f.wire == 2) { out.playlist = pbAsString(f); any = true; }
                break;
            case 6:
                if (f.wire == 0) { out.duration = (unsigned)f.varint; any = true; }
                break;
            default:
                break;          // rating (7) is not shown anywhere
        }
    }
    return any;
}

inline const char* aaMediaStateName(unsigned s) {
    switch (s) {
        case 1: return "stopped";
        case 2: return "playing";
        case 3: return "paused";
        default: return "?";
    }
}

// One line for the log: what the phone says is playing, and where in the track it is.
inline std::string aaMediaNowPlayingLine(const AaMediaStatus& st, const AaMediaMetadata& md) {
    std::string s;
    if (!md.song.empty()) {
        s = md.song;
        if (!md.artist.empty()) { s += " - "; s += md.artist; }
        if (!md.album.empty()) { s += " ("; s += md.album; s += ")"; }
    } else {
        s = "(no metadata yet)";
    }
    char tail[192];
    snprintf(tail, sizeof(tail), " [%s%s%s, %us/%us%s%s%s%s]", aaMediaStateName(st.state),
             st.source.empty() ? "" : ", ", st.source.c_str(), st.position, md.duration,
             st.shuffle ? ", shuffle" : "", st.repeat ? (st.repeatOne ? ", repeat one" : ", repeat") : "",
             md.playlist.empty() ? "" : ", playlist ", md.playlist.empty() ? "" : md.playlist.c_str());
    s += tail;
    return s;
}

} // namespace nbt

#endif // NBT_AA_MEDIA_META_HPP
