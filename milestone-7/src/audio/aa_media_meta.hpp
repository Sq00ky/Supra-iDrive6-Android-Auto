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

#include "aa_proto.hpp"

namespace nbt {

struct AaMediaStatus {
    enum { kState = 1, kSource = 2, kPosition = 4, kShuffle = 8, kRepeat = 16, kRepeatOne = 32 };
    unsigned fields;            // distinguish an absent field from an explicit empty/zero value
    unsigned state;             // 1 stopped, 2 playing, 3 paused (0 = not seen)
    std::string source;         // media_source: the app name, e.g. "Apple Music"
    unsigned position;          // playback_seconds
    bool shuffle;
    bool repeat;
    bool repeatOne;
    AaMediaStatus() : fields(0), state(0), position(0), shuffle(false), repeat(false), repeatOne(false) {}
};

struct AaMediaMetadata {
    enum { kSong = 1, kArtist = 2, kAlbum = 4, kArt = 8, kPlaylist = 16, kDuration = 32 };
    unsigned fields;
    std::string song;
    std::string artist;
    std::string album;
    std::string playlist;
    unsigned duration;          // duration_seconds
    size_t albumArtBytes;       // field 4 is the image itself; only its length is kept here
    AaMediaMetadata() : fields(0), duration(0), albumArtBytes(0) {}
};

inline std::string aaMediaText(const proto::PbField& f) { return std::string((const char*)f.data, f.len); }

// 0x8001 MediaPlaybackStatus. Returns false when the body is not parseable (a partial message still fills what it
// got - the phone sends this once a second and a truncated one must not poison the last known state).
inline bool aaParseMediaStatus(const uint8_t* p, size_t n, AaMediaStatus& out) {
    if (!p) return false;
    bool any = false;
    proto::PbReader r(p, n);
    proto::PbField f;
    while (r.next(f)) {
        switch (f.num) {
            case 1:
                if (f.wire == proto::WIRE_VARINT) { out.state = f.value <= 3 ? (unsigned)f.value : 4; out.fields |= AaMediaStatus::kState; any = true; }
                break;
            case 2:
                if (f.wire == proto::WIRE_LENGTH) { out.source = aaMediaText(f); out.fields |= AaMediaStatus::kSource; any = true; }
                break;
            case 3:
                if (f.wire == proto::WIRE_VARINT) { out.position = (unsigned)f.value; out.fields |= AaMediaStatus::kPosition; any = true; }
                break;
            case 4:
                if (f.wire == proto::WIRE_VARINT) { out.shuffle = f.value != 0; out.fields |= AaMediaStatus::kShuffle; any = true; }
                break;
            case 5:
                if (f.wire == proto::WIRE_VARINT) { out.repeat = f.value != 0; out.fields |= AaMediaStatus::kRepeat; any = true; }
                break;
            case 6:
                if (f.wire == proto::WIRE_VARINT) { out.repeatOne = f.value != 0; out.fields |= AaMediaStatus::kRepeatOne; any = true; }
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
    proto::PbReader r(p, n);
    proto::PbField f;
    while (r.next(f)) {
        switch (f.num) {
            case 1:
                if (f.wire == proto::WIRE_LENGTH) { out.song = aaMediaText(f); out.fields |= AaMediaMetadata::kSong; any = true; }
                break;
            case 2:
                if (f.wire == proto::WIRE_LENGTH) { out.artist = aaMediaText(f); out.fields |= AaMediaMetadata::kArtist; any = true; }
                break;
            case 3:
                if (f.wire == proto::WIRE_LENGTH) { out.album = aaMediaText(f); out.fields |= AaMediaMetadata::kAlbum; any = true; }
                break;
            case 4:
                if (f.wire == proto::WIRE_LENGTH) { out.albumArtBytes = f.len; out.fields |= AaMediaMetadata::kArt; any = true; }
                break;
            case 5:
                if (f.wire == proto::WIRE_LENGTH) { out.playlist = aaMediaText(f); out.fields |= AaMediaMetadata::kPlaylist; any = true; }
                break;
            case 6:
                if (f.wire == proto::WIRE_VARINT) { out.duration = (unsigned)f.value; out.fields |= AaMediaMetadata::kDuration; any = true; }
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
