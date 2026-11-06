// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test: AA media-channel messages, byte for byte against the layout of Google's receiver library (DHU r2.1).
#include "audio/aa_media_msgs.hpp"
#include "audio/aa_media_meta.hpp"
#include <stdio.h>
#include <string.h>
#include <vector>

using namespace nbt;

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

static bool same(const std::vector<uint8_t>& v, const uint8_t* want, size_t n) {
    return v.size() == n && memcmp(&v[0], want, n) == 0;
}

int main() {
    // AudioUnderflowNotification 0x800b { session = 1 }
    { const uint8_t w[] = { 0x80, 0x0B, 0x08, 0x00 }; CHECK(same(aaUnderflowNotification(0), w, sizeof(w))); }
    { const uint8_t w[] = { 0x80, 0x0B, 0x08, 0x05 }; CHECK(same(aaUnderflowNotification(5), w, sizeof(w))); }
    { const uint8_t w[] = { 0x80, 0x0B, 0x08, 0xAC, 0x02 }; CHECK(same(aaUnderflowNotification(300), w, sizeof(w))); }

    // Ack 0x8004 { session = 1, ack = 2, receive_time = 3 (repeated, unpacked) }
    {
        const uint8_t w[] = { 0x80, 0x04, 0x08, 0x00, 0x10, 0x01 };   // what nbt-aad always sent: no times
        CHECK(same(aaMediaAck(0, 1, NULL, 0), w, sizeof(w)));
    }
    {
        const uint8_t w[] = { 0x80, 0x04, 0x08, 0x03, 0x10, 0x01, 0x18, 0x96, 0x01 };   // 150 ns
        const uint64_t t = 150;
        CHECK(same(aaMediaAck(3, 1, &t, 1), w, sizeof(w)));
    }
    {
        // 2026-11-03 12:00:00 UTC in ns (a 9-byte varint), two values: each gets its own 0x18 tag
        const uint64_t t[2] = { 1793707200000000000ull, 1793707200042666000ull };
        std::vector<uint8_t> m = aaMediaAck(300, 2, t, 2);
        CHECK(m.size() == 2 + 3 + 2 + 2 * (1 + 9));
        CHECK(m[2] == 0x08 && m[3] == 0xAC && m[4] == 0x02 && m[5] == 0x10 && m[6] == 0x02);
        CHECK(m[7] == 0x18 && m[17] == 0x18);
        // decode the first value back
        uint64_t v = 0; int sh = 0; size_t i = 8;
        while (i < m.size()) { uint8_t b = m[i++]; v |= (uint64_t)(b & 0x7F) << sh; sh += 7; if (!(b & 0x80)) break; }
        CHECK(v == t[0] && i == 17);
    }


    // ---- channel 12 media playback: status + metadata (what the HUD needs) ------------------------------
    {
        // real capture: 80 01 08 03 12 0B "Apple Music" 18 2A 20 01
        const uint8_t body[] = { 0x08, 0x03, 0x12, 0x0B, 'A','p','p','l','e',' ','M','u','s','i','c',
                                 0x18, 0x2A, 0x20, 0x01 };
        AaMediaStatus st;
        CHECK(aaParseMediaStatus(body, sizeof(body), st));
        CHECK(st.state == 3);
        CHECK(st.source == "Apple Music");
        CHECK(st.position == 42);
        CHECK(st.shuffle && !st.repeat && !st.repeatOne);
        CHECK(strcmp(aaMediaStateName(st.state), "paused") == 0);
    }
    {
        // metadata: song=1 "Song One", artist=2 "Artist", album=3 "Album", art=4 (3 bytes), playlist=5, duration=6
        const uint8_t body[] = {
            0x0A, 0x08, 'S','o','n','g',' ','O','n','e',
            0x12, 0x06, 'A','r','t','i','s','t',
            0x1A, 0x05, 'A','l','b','u','m',
            0x22, 0x03, 0xFF, 0xD8, 0xFF,
            0x2A, 0x08, 'M','y',' ','M','i','x',' ','1',
            0x30, 0xD2, 0x01                                            // 210 s
        };
        AaMediaMetadata md;
        CHECK(aaParseMediaMetadata(body, sizeof(body), md));
        CHECK(md.song == "Song One");
        CHECK(md.artist == "Artist");
        CHECK(md.album == "Album");
        CHECK(md.playlist == "My Mix 1");
        CHECK(md.duration == 210);
        CHECK(md.albumArtBytes == 3);                                   // measured, not copied
    }
    {
        // a truncated status must still yield what arrived rather than throwing the state away
        const uint8_t body[] = { 0x08, 0x02, 0x12, 0x04, 'S','p','o','t' };   // says "Spot" of "Spotify"
        AaMediaStatus st;
        CHECK(aaParseMediaStatus(body, sizeof(body), st));
        CHECK(st.state == 2 && st.source == "Spot");
        const uint8_t junk[] = { 0xFF };
        AaMediaStatus st2;
        CHECK(!aaParseMediaStatus(junk, sizeof(junk), st2));
    }
    {
        // the log line carries track, artist, album, app, position and duration
        AaMediaStatus st; AaMediaMetadata md;
        st.state = 2; st.source = "Spotify"; st.position = 61;
        md.song = "Song One"; md.artist = "Artist"; md.album = "Album"; md.duration = 210;
        std::string line = aaMediaNowPlayingLine(st, md);
        CHECK(line.find("Song One - Artist (Album)") == 0);
        CHECK(line.find("playing") != std::string::npos);
        CHECK(line.find("Spotify") != std::string::npos);
        CHECK(line.find("61s/210s") != std::string::npos);
    }

    printf("test_media_msgs: %s\n", fails ? "FAILED" : "all passed");
    return fails ? 1 : 0;
}
