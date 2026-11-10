// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test: the merged now-playing state and the record the iDrive app reads (audio/media_state.hpp).
#include "audio/media_state.hpp"
#include <stdio.h>
#include <string.h>
#include <vector>

using namespace nbt;

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

// The phone's field encoding: field n = (n << 3) | wire type.
static void putVarint(std::vector<uint8_t>& v, uint64_t x) {
    while (x >= 0x80) { v.push_back((uint8_t)(x | 0x80)); x >>= 7; }
    v.push_back((uint8_t)x);
}
static void putKey(std::vector<uint8_t>& v, unsigned n, unsigned wire) { putVarint(v, ((uint64_t)n << 3) | wire); }
static void putInt(std::vector<uint8_t>& v, unsigned n, uint64_t x) { putKey(v, n, 0); putVarint(v, x); }
static void putStr(std::vector<uint8_t>& v, unsigned n, const char* s, size_t len) {
    putKey(v, n, 2); putVarint(v, len); v.insert(v.end(), s, s + len);
}
static void putStr(std::vector<uint8_t>& v, unsigned n, const char* s) { putStr(v, n, s, strlen(s)); }

static bool has(const std::string& rec, const char* line) {
    std::string want(line);
    return rec.find(want + "\n") != std::string::npos;
}

int main() {
    // ---- status 0x8001 { state 2, source "Spotify", position 57, shuffle, repeat }
    {
        std::vector<uint8_t> b;
        putInt(b, 1, 2); putStr(b, 2, "Spotify"); putInt(b, 3, 57); putInt(b, 4, 1); putInt(b, 5, 0);
        MediaState st;
        CHECK(mediaMerge(st, 0x8001, &b[0], b.size()));
        CHECK(st.state == 2 && st.source == "Spotify" && st.position == 57 && st.shuffle && !st.repeat);
        CHECK(st.haveStatus && !st.haveMetadata);
        std::string rec = mediaRecord(st);
        CHECK(has(rec, "state=playing"));
        CHECK(has(rec, "source=Spotify"));
        CHECK(has(rec, "pos=57"));
        CHECK(has(rec, "shuffle=1"));
        CHECK(has(rec, "have=1"));
        // A second identical status changes nothing.
        CHECK(!mediaMerge(st, 0x8001, &b[0], b.size()));
        // Position alone still counts as a change (the record follows the track).
        std::vector<uint8_t> b2;
        putInt(b2, 1, 2); putStr(b2, 2, "Spotify"); putInt(b2, 3, 58);
        CHECK(mediaMerge(st, 0x8001, &b2[0], b2.size()));
        CHECK(st.position == 58 && st.song.empty());
    }

    // ---- metadata 0x8003 { song, artist, album, art, playlist, duration }
    {
        std::vector<uint8_t> b;
        putStr(b, 1, "Song A"); putStr(b, 2, "Artist B"); putStr(b, 3, "Album C");
        putStr(b, 4, "\x89PNG..."); putStr(b, 5, "Driving"); putInt(b, 6, 214);
        MediaState st;
        CHECK(mediaMerge(st, 0x8003, &b[0], b.size()));
        CHECK(st.song == "Song A" && st.artist == "Artist B" && st.album == "Album C");
        CHECK(st.playlist == "Driving" && st.duration == 214 && st.artBytes == 7);
        CHECK(st.haveMetadata && !st.haveStatus);
        std::string rec = mediaRecord(st);
        CHECK(has(rec, "song=Song A"));
        CHECK(has(rec, "artist=Artist B"));
        CHECK(has(rec, "album=Album C"));
        CHECK(has(rec, "dur=214"));
        CHECK(has(rec, "art=7"));
        CHECK(has(rec, "have=2"));

        // A truncated message (cut inside the artist string) must not replace anything at all.
        std::vector<uint8_t> part(b.begin(), b.begin() + 10);
        CHECK(!mediaMerge(st, 0x8003, &part[0], part.size()));
        CHECK(st.song == "Song A" && st.artist == "Artist B");
    }

    // ---- a new track with empty strings keeps the old ones (the phone omits what it does not have)
    {
        MediaState st;
        std::vector<uint8_t> a, b;
        putStr(a, 1, "First"); putStr(a, 2, "Someone");
        putStr(b, 1, "Second");
        CHECK(mediaMerge(st, 0x8003, &a[0], a.size()));
        CHECK(mediaMerge(st, 0x8003, &b[0], b.size()));
        CHECK(st.song == "Second" && st.artist == "Someone");
    }

    // ---- unknown message ids and empty bodies are ignored
    {
        MediaState st;
        const uint8_t junk[] = { 0xFF, 0xFF, 0xFF };
        CHECK(!mediaMerge(st, 0x8002, junk, sizeof(junk)));
        CHECK(!mediaMerge(st, 0x8001, NULL, 0));
        CHECK(!mediaMerge(st, 0x8003, junk, sizeof(junk)));
        CHECK(st.state == 0 && !st.haveStatus && !st.haveMetadata);
        std::string rec = mediaRecord(st);
        CHECK(has(rec, "state=unknown"));
        CHECK(has(rec, "have=0"));
    }

    // ---- cleaning: control characters become spaces, ends trimmed, cap at kMediaValueMax
    {
        MediaState st;
        std::vector<uint8_t> b;
        const char* nasty = "\nstate=playing\r\nArtist\ttab  ";
        putStr(b, 1, nasty);
        CHECK(mediaMerge(st, 0x8003, &b[0], b.size()));
        CHECK(st.song == "state=playing  Artist tab");   // no line break can forge a record line
        CHECK(st.song.find('\n') == std::string::npos && st.song.find('\r') == std::string::npos);

        std::string longname(300, 'x');
        MediaState st2;
        std::vector<uint8_t> b2;
        putStr(b2, 2, longname.c_str());
        CHECK(mediaMerge(st2, 0x8003, &b2[0], b2.size()));
        CHECK(st2.artist.size() == (size_t)kMediaValueMax);
        CHECK(mediaRecord(st2).size() <= (size_t)kMediaRecordMax);
    }

    // ---- UTF-8 cut: never leave half a sequence at the cap
    {
        std::string s;
        for (int i = 0; i < 47; ++i) s += "\xC3\xA9";   // é, 2 bytes each: 94 bytes
        s += "\xE2\x82\xAC";                            // €, 3 bytes: lands across the cap
        MediaState st;
        std::vector<uint8_t> b;
        putStr(b, 1, s.c_str(), s.size());
        CHECK(mediaMerge(st, 0x8003, &b[0], b.size()));
        const std::string& v = st.song;
        CHECK(v.size() <= (size_t)kMediaValueMax);
        // Every byte must belong to a complete sequence: decode and check we never end mid-character.
        size_t i = 0;
        while (i < v.size()) {
            unsigned char c = (unsigned char)v[i];
            size_t need = (c & 0x80) == 0 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 1;
            CHECK(i + need <= v.size());
            i += need;
        }
    }

    // ---- the log line names the app and the track
    {
        MediaState st;
        std::vector<uint8_t> s;
        putInt(s, 1, 2); putStr(s, 2, "Apple Music"); putInt(s, 3, 12);
        std::vector<uint8_t> m;
        putStr(m, 1, "Song"); putStr(m, 2, "Artist"); putInt(m, 6, 200);
        mediaMerge(st, 0x8001, &s[0], s.size());
        mediaMerge(st, 0x8003, &m[0], m.size());
        std::string line = mediaLine(st);
        CHECK(line.find("Song - Artist") == 0);
        CHECK(line.find("Apple Music") != std::string::npos);
        CHECK(line.find("12s/200s") != std::string::npos);
    }

    if (fails) { printf("%d check(s) failed\n", fails); return 1; }
    printf("test_media_state: all checks passed\n");
    return 0;
}
