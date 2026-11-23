// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#pragma once
// Car local media messages 0x8001 and 0x8002 (car -> phone). Pure, no I/O.
#include "aa_proto.hpp"

#include <stdint.h>
#include <string>
#include <vector>

namespace nbt {

enum {
    AA_MSG_LOCAL_MEDIA_STATUS = 0x8001,
    AA_MSG_LOCAL_MEDIA_METADATA = 0x8002
};

inline std::vector<uint8_t> localMediaStatus(int state, const std::string& source, const std::vector<int>& actions) {
    enum { F_STATE = 1, F_SOURCE = 2, F_ACTION = 4 };
    proto::ProtoWriter w(AA_MSG_LOCAL_MEDIA_STATUS);
    w.integer(F_STATE, state);
    if (!source.empty()) w.string(F_SOURCE, source);
    w.repeated(F_ACTION, actions);
    return w.bytes();
}

inline std::vector<uint8_t> localMediaMetadata(const std::string& song, const std::string& artist,
                                               const std::string& album, const std::vector<uint8_t>& art) {
    enum { F_SONG = 1, F_ARTIST = 2, F_ALBUM = 3, F_ART = 4 };
    proto::ProtoWriter w(AA_MSG_LOCAL_MEDIA_METADATA);
    if (!song.empty()) w.string(F_SONG, song);
    if (!artist.empty()) w.string(F_ARTIST, artist);
    if (!album.empty()) w.string(F_ALBUM, album);
    if (!art.empty()) w.blob(F_ART, &art[0], art.size());
    return w.bytes();
}

} // namespace nbt
