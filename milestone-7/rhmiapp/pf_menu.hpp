// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
//
// "Firewall rules for Android Auto (pf_aa)" checkbox (rhmiapp-47): whether pf_aa's rules are in place, read without
// STL (the plugin lives inside the HMI process). Same rules as tools/pf_aa.hpp coverage() / setKeepPf(), which
// tests/test_pf_aa.cpp cross-checks: a non-comment "pass" line naming port 5277 on uap0 / wfd0 (or on no interface)
// in pf.conf, and NBT_KEEP_PF=1 among the first kMaxEnvSettings settings of nbt-aad.env (the app passes no more).
#ifndef NBT_RHMIAPP_PF_MENU_HPP
#define NBT_RHMIAPP_PF_MENU_HPP

#include <string.h>

namespace nbt {
namespace pfmenu {

static const int kMaxEnvSettings = 15;

inline bool wordChar(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

// tok as a whole word in [b, e).
inline bool hasToken(const char* b, const char* e, const char* tok) {
    const size_t n = strlen(tok);
    for (const char* p = b; p + n <= e; ++p) {
        if (memcmp(p, tok, n) != 0) continue;
        bool before = p == b || !wordChar(p[-1]);
        bool after = p + n == e || !wordChar(p[n]);
        if (before && after) return true;
    }
    return false;
}

// Which interfaces pf.conf passes 5277 on (comments from '#' ignored).
inline void coverage(const char* conf, bool* uap0, bool* wfd0) {
    *uap0 = *wfd0 = false;
    for (const char* p = conf; p && *p;) {
        const char* end = strchr(p, '\n');
        if (!end) end = p + strlen(p);
        const char* e = p;
        while (e < end && *e != '#') ++e;
        if (hasToken(p, e, "pass") && hasToken(p, e, "5277")) {
            bool u = hasToken(p, e, "uap0"), w = hasToken(p, e, "wfd0");
            if (u) *uap0 = true;
            if (w) *wfd0 = true;
            if (!u && !w && !hasToken(p, e, "on")) *uap0 = *wfd0 = true;
        }
        p = *end ? end + 1 : end;
    }
}

// NBT_KEEP_PF=1 within the first kMaxEnvSettings settings (non-blank lines not starting with '#').
inline bool keepPfSet(const char* env) {
    int settings = 0;
    for (const char* p = env; p && *p;) {
        const char* end = strchr(p, '\n');
        if (!end) end = p + strlen(p);
        const char* s = p;
        while (s < end && (*s == ' ' || *s == '\t' || *s == '\r')) ++s;
        if (s < end && *s != '#') {
            if (++settings > kMaxEnvSettings) return false;
            if ((size_t)(end - s) >= 12 && !memcmp(s, "NBT_KEEP_PF=", 12)) {
                const char* v = s + 12;
                const char* ve = end;
                while (ve > v && (ve[-1] == ' ' || ve[-1] == '\t' || ve[-1] == '\r')) --ve;
                return ve - v == 1 && *v == '1';
            }
        }
        p = *end ? end + 1 : end;
    }
    return false;
}

} // namespace pfmenu
} // namespace nbt

#endif
