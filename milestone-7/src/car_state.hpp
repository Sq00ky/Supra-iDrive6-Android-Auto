// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef NBT_CAR_STATE_HPP
#define NBT_CAR_STATE_HPP

// The car values the iDrive app last saw, for a daemon that has just started. Changes also go over the control port;
// this file only answers "what is the value right now?". The app writes it, the daemon reads it, and both include this
// header, so it stays free of the C++ library (the two link different ones).
//
// One line per value, key then number, padded so that a rewrite keeps every line in place:
//     gear              2
//     pdc               0
//     mode1            -1
// A reader takes the keys it knows and ignores the rest, so the app and the daemon can differ by a version: they are
// installed separately. -1 means no value yet. `cat` shows the whole state.
//
// This is for state that changes at human speed, about once a second at most. Each change rewrites the file and sends a
// message, on the app's thread. Faster signals (speed, RPM, steering angle) belong in the daemon, read from MOST.
//
// The file is overwritten in place and only then shortened. QNX /dev/shmem has no rename(), and truncating first leaves
// a moment where a reader finds nothing (see writeUiState in main.cpp). A reader that catches a rewrite can still see a
// mix of the old and the new bytes. The message on the control port follows the write and corrects it.

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

namespace nbt {
namespace carstate {

static const char* const kPath = "/dev/shmem/nbt-aad-car";
enum {
    kKeyWidth = 9,                                 // longest key
    kLineBytes = kKeyWidth + 1 + 6 + 1,            // key, space, number, newline
    kMaxBytes = 512,                               // the whole file
    kMaxItems = (kMaxBytes - 1) / kLineBytes       // a writer with more can't update the file at all
};
const int kNoValue = -1;

struct Item {
    const char* key;
    int value;
};

// The text of the whole file, or -1 when a key is empty or too long, or the text doesn't fit.
inline int format(char* out, size_t cap, const Item* items, size_t count) {
    size_t used = 0;
    for (size_t i = 0; i < count; ++i) {
        size_t len = items[i].key ? strlen(items[i].key) : 0;
        if (len == 0 || len > kKeyWidth) return -1;
        if (used >= cap) return -1;
        int n = snprintf(out + used, cap - used, "%-*s %6d\n", (int)kKeyWidth, items[i].key, items[i].value);
        if (n < 0 || (size_t)n >= cap - used) return -1;
        used += (size_t)n;
    }
    if (count == 0 && cap > 0) out[0] = 0;
    return (int)used;
}

// The value of one key. False when the key is missing or its number can't be read. The whole key has to match.
inline bool parse(const char* text, const char* key, int* value) {
    if (!text || !key || !value) return false;
    size_t len = strlen(key);
    if (len == 0) return false;
    for (const char* line = text; line && *line;) {
        if (strncmp(line, key, len) == 0 && line[len] == ' ') {
            char* end = NULL;
            long v = strtol(line + len, &end, 10);
            if (end == line + len || (*end != '\n' && *end != 0)) return false;
            *value = (int)v;
            return true;
        }
        line = strchr(line, '\n');
        if (line) ++line;
    }
    return false;
}

// Replaces the file's content without a moment where it is empty.
inline bool writeFile(const char* path, const Item* items, size_t count) {
    char text[kMaxBytes];
    int n = format(text, sizeof(text), items, count);
    if (n < 0) return false;
    int fd = open(path, O_WRONLY | O_CREAT, 0666);
    if (fd < 0) return false;
    fchmod(fd, 0666);   // readable whatever the writer's umask is
    bool ok = write(fd, text, (size_t)n) == (ssize_t)n;
    if (ok) ok = ftruncate(fd, (off_t)n) == 0;   // drop a longer old text only after the new one is in place
    close(fd);
    return ok;
}

// The file's text, always terminated. False when there is no file or it is empty.
inline bool readFile(const char* path, char* out, size_t cap) {
    if (cap == 0) return false;
    out[0] = 0;
    int fd = open(path, O_RDONLY);
    if (fd < 0) return false;
    ssize_t n = read(fd, out, cap - 1);
    close(fd);
    if (n <= 0) return false;
    out[n] = 0;
    return true;
}

} // namespace carstate
} // namespace nbt

#endif
