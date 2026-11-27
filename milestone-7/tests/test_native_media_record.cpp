// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "audio/native_media_record.hpp"
#include <assert.h>
#include <stdio.h>

using namespace nbt;

static bool read(const std::string& record, uint32_t now, NativeMediaSnapshot& out) {
    return parseNativeMediaRecord(record.data(), record.size(), now, out);
}

static void selectionTests(const MediaState& playing, const std::string& oldRecord) {
    NativeMediaSelection selection;
    NativeMediaSnapshot out;
    selection.turn(0x7F, 1000); selection.touch(1010, false);
    assert(selection.cursor == kNativeMediaCursorNone && selection.eventMs == 0);
    assert(selection.selected() == kNativeMediaCursorNext);
    selection.touch(1020);
    assert(selection.cursor == kNativeMediaCursorNext && selection.eventMs == 1020);
    selection.turn(0x7E, 1100); selection.turn(0x7F, 1150);
    assert(selection.cursor == kNativeMediaCursorNext && selection.eventMs == 1150);
    assert(selection.selected() == kNativeMediaCursorNext);
    selection.turn(0x7F, 5151); // Rest retains the wheel's last direction, including after a quiet gap.
    assert(selection.cursor == kNativeMediaCursorNext && selection.eventMs == 5151);
    selection.touch(9152);
    assert(selection.cursor == kNativeMediaCursorNext && selection.eventMs == 9152); // Popup timeout keeps the choice.
    selection.turn(0x80, 5200);
    assert(selection.cursor == kNativeMediaCursorPrev);
    selection.turn(0x79, 5300); // Direction, not magnitude or a cumulative row increment.
    assert(selection.cursor == kNativeMediaCursorNext);
    selection.touch(5400); selection.turn(0x7F, 5450);
    assert(selection.eventMs == 5450);
    selection.touch(10000, false); // A long-held confirm's release keeps the existing direction alive.
    assert(selection.selected() == kNativeMediaCursorNext && selection.eventMs == 10000);
    selection.clear();
    assert(selection.cursor == kNativeMediaCursorNone && selection.eventMs == 0);
    selection.turn(0x80, UINT32_MAX - 10);
    assert(selection.selected() == kNativeMediaCursorPrev);
    NativeMediaSnapshot wrapped;
    wrapped.active = true; wrapped.updatedMs = UINT32_MAX - 20;
    wrapped.cursor = selection.cursor; wrapped.cursorMs = selection.eventMs;
    assert(nativeMediaCursor(wrapped, 5) == kNativeMediaCursorPrev);
    assert(nativeMediaCursor(wrapped, 3990) == kNativeMediaCursorPrev);
    selection.turn(0x7E, 1200);
    assert(read(nativeMediaRecord(playing, true, 8, 42, &selection), 1250, out));
    assert(out.cursor == kNativeMediaCursorNext && out.cursorMs == 1200);
    assert(nativeMediaCursor(out, 1250) == kNativeMediaCursorNext);
    assert(nativeMediaCursor(out, 5201) == kNativeMediaCursorNext);
    out.updatedMs = 5200; // Fresh playback preserves selection without changing its event timestamp.
    assert(nativeMediaCursor(out, 5201) == kNativeMediaCursorNext && out.cursorMs == 1200);
    out.cursorMs = 5200; out.active = false;
    assert(nativeMediaCursor(out, 5201) == kNativeMediaCursorNone);
    out.active = true; out.updatedMs = 0;
    assert(nativeMediaCursor(out, kMediaStaleMs) == kNativeMediaCursorNone);
    assert(read(oldRecord, 1250, out) && out.active && out.cursor == kNativeMediaCursorNone && out.cursorMs == 0);
    assert(nativeMediaCursor(out, 1250) == kNativeMediaCursorNext); // No middle/title focus, including legacy records.

    const char* badPairs[] = {
        "native_cursor=next\n", "native_cursor_ms=1200\n",
        "native_cursor=2\nnative_cursor_ms=1200\n", "native_cursor=current\nnative_cursor_ms=1200\n",
        "native_cursor=prev\nnative_cursor_ms=-1\n", "native_cursor=prev\nnative_cursor_ms=4294967296\n",
        "native_cursor=next\nnative_cursor_ms=1200\nnative_cursor=prev\n",
        "native_cursor=next\nnative_cursor_ms=1200\nnative_cursor_ms=1201\n"
    };
    for (size_t i = 0; i != sizeof(badPairs) / sizeof(*badPairs); ++i) {
        std::string record = oldRecord;
        record.insert(record.find("native_end="), badPairs[i]);
        out.active = true; out.cursor = kNativeMediaCursorNext;
        assert(!read(record, 1250, out) && !out.active && out.cursor == kNativeMediaCursorNone && out.cursorMs == 0);
    }
    MediaState maximum = playing;
    maximum.source = maximum.song = maximum.artist = maximum.album = maximum.playlist = std::string(kMediaValueMax, 'x');
    maximum.position = maximum.duration = UINT32_MAX; maximum.artBytes = ~0ul;
    const std::string bounded = nativeMediaRecord(maximum, true, UINT32_MAX, UINT32_MAX, &selection);
    assert(bounded.size() <= kMediaRecordMax && read(bounded, 1250, out) && out.active);
}

int main() {
    MediaState st;
    st.state = 2; st.song = "Название"; st.artist = "Исполнитель";
    st.haveStatus = st.haveMetadata = true; st.updatedMs = 1000;
    NativeMediaSnapshot out;
    const std::string record = nativeMediaRecord(st, true, 7, 42);
    assert(record.size() <= kMediaRecordMax);
    selectionTests(st, record);
    assert(read(record, 1100, out) && out.active && out.sequence == 7);
    assert(st.song == out.song && st.artist == out.artist);
    assert(read(record, 1000 + kMediaStaleMs - 1, out) && out.active);
    assert(read(record, 1000 + kMediaStaleMs, out) && !out.active);
    assert(read(record, 999, out) && !out.active); // a future timestamp cannot renew freshness
    st.updatedMs = UINT32_MAX - 10;
    assert(read(nativeMediaRecord(st, true, UINT32_MAX, 42), 5, out) && out.active);
    st.updatedMs = 1000;
    assert(read(nativeMediaRecord(st, false, 8, 42), 1100, out) && !out.active);
    st.state = 1; // Between titles the stream can stay active despite Stopped playback status.
    assert(read(nativeMediaRecord(st, true, 8, 42), 1100, out) && out.active);
    assert(read(nativeMediaRecord(st, false, 8, 42), 1100, out) && !out.active);
    st.state = 3;
    assert(read(nativeMediaRecord(st, true, 8, 42), 1100, out) && !out.active);
    st.state = 2; st.haveMetadata = false;
    assert(read(nativeMediaRecord(st, true, 8, 42), 1100, out) && !out.active);
    st.haveMetadata = true; st.song.clear();
    assert(read(nativeMediaRecord(st, true, 8, 42), 1100, out) && !out.active);

    // Truncated and mixed generations must never become visible native metadata.
    for (size_t n = 0; n < record.size(); ++n)
        assert(!parseNativeMediaRecord(record.data(), n, 1100, out) && !out.active);
    std::string mixed = record;
    mixed.replace(mixed.find("native_end=42:7"), 15, "native_end=42:8");
    assert(!read(mixed, 1100, out) && !out.active);
    std::string duplicate = record;
    duplicate.insert(duplicate.find("native_end="), "song=Other\n");
    assert(!read(duplicate, 1100, out) && !out.active);
    std::string malformed = record;
    malformed.insert(malformed.find("native_end="), "bad_line\n");
    assert(!read(malformed, 1100, out));
    assert(!read(std::string(kMediaRecordMax + 1, 'x'), 1100, out));
    assert(!read(mediaRecord(st), 1100, out)); // an old daemon does not enable the native adapter
    uint32_t value = 0;
    assert(nativeMediaUint("4294967295", 10, value) && value == UINT32_MAX);
    assert(!nativeMediaUint("4294967296", 10, value));
    assert(!nativeMediaUint("-1", 2, value));
    // A reader spanning a write can see matching old markers around a new body. Rechecking the header detects it.
    uint32_t pid = 0, sequence = 0;
    assert(nativeMediaGeneration("native_begin=42:8\n", 18, pid, sequence));
    assert(pid != 42 || sequence != 7);
    assert(nativeMediaGeneration("native_begin=43:7\n", 18, pid, sequence));
    assert(pid != 42 || sequence != 7); // a daemon restart is a different writer even at the same sequence
    NativeMediaSnapshot line;
    line.active = true;
    strcpy(line.song, "Трек"); strcpy(line.artist, "Автор");
    uint16_t text[26];
    const uint16_t expected[] = {0x422,0x440,0x435,0x43A,0};
    assert(nativeMediaText(line.song, text) == 4 && !memcmp(text, expected, sizeof(expected)));
    const uint16_t artist[] = {0x410,0x432,0x442,0x43E,0x440,0};
    assert(nativeMediaText(line.artist, text) == 5 && !memcmp(text, artist, sizeof(artist)));
    memset(line.song, 'S', 60); line.song[60] = 0;
    memset(line.artist, 'A', 60); line.artist[60] = 0;
    assert(nativeMediaText(line.song, text) == 25 && text[24] == 'S' && text[25] == 0);
    assert(nativeMediaText(line.artist, text) == 25 && text[24] == 'A' && text[25] == 0);
    line.artist[0] = 0;
    strcpy(line.song, "123456789012345678901234\xF0\x9F\x8E\xB5");
    assert(nativeMediaText(line.song, text) == 24 && text[24] == 0); // no half of a surrogate pair
    strcpy(line.song, "bad\xED\xA0\x80");
    assert(nativeMediaText(line.song, text) == 6 && text[3] == '?');
    puts("test_native_media_record: all checks passed");
    return 0;
}
