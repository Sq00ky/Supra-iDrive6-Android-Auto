// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef NBT_CDS_PARSE_HPP
#define NBT_CDS_PARSE_HPP

// Tiny readers for the CDS property texts the iDrive app subscribes to (JSON-like, ASCII after UTF-16 narrowing).
// Car-verified layout (2026-10-28): communication.callStatus0 = '{"callStatus":{"id":0,"state":3}}',
// callStatus1 = '{"callStatus":{"id":1,"state":0}}'. "id" is the phone line, "state" 0 = no call.

#include <stdlib.h>
#include <string.h>

namespace nbt {

// Integer after "<key>": anywhere in text; def when the key or a number is missing.
inline int cdsIntField(const char* text, const char* key, int def) {
    if (!text || !key) return def;
    char pat[48];
    size_t kl = strlen(key);
    if (kl + 3 > sizeof(pat)) return def;
    pat[0] = '"';
    memcpy(pat + 1, key, kl);
    pat[kl + 1] = '"';
    pat[kl + 2] = 0;
    const char* p = strstr(text, pat);
    if (!p) return def;
    p += kl + 2;
    while (*p == ' ' || *p == ':') ++p;
    if (!(*p == '-' || (*p >= '0' && *p <= '9'))) return def;
    return atoi(p);
}

// A phone line is in a call when its "state" is not 0. Unknown text (no "state") counts as no call, so a
// layout change can never pin the screen hold in "call in progress".
inline bool cdsCallActive(const char* text) { return cdsIntField(text, "state", 0) != 0; }

} // namespace nbt

#endif
