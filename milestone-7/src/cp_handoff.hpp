// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef NBT_CP_HANDOFF_HPP
#define NBT_CP_HANDOFF_HPP

#include <stdint.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

namespace nbt {

// The existing RHMI graphical-context file: a header followed by the CDS JSON. Native projection uses a widget,
// not a fixed context number; Android Auto's own RHMI states are >= 50000.
struct CpContext {
    bool valid, native, projected;
    int id;
    time_t modified;
    CpContext() : valid(false), native(false), projected(false), id(0), modified(0) {}
    bool same(const CpContext& b) const {
        return valid == b.valid && id == b.id && native == b.native && projected == b.projected && modified == b.modified;
    }
};

inline const char* cpContextValue(const char* text, const char* key) {
    const char* p = strstr(text, key);
    if (!p) return NULL;
    p += strlen(key);
    p += strspn(p, " \t\r\n");
    if (*p++ != ':') return NULL;
    return p + strspn(p, " \t\r\n");
}

inline CpContext parseCpContext(const char* text, time_t modified) {
    CpContext c;
    if (!text || strncmp(text, "vc ", 3)) return c;
    char* end = NULL;
    long value = strtol(text + 3, &end, 10);
    if (end == text + 3 || value <= 0 || value > INT_MAX || *end != '\n') return c;
    c.id = (int)value;
    const char* json = end + 1;
    size_t len = strlen(json);
    while (len && strchr(" \t\r\n", json[len - 1])) --len;
    if (!len || json[len - 1] != '}') return CpContext();   // RHMI may be in the middle of rewriting it
    const char* id = cpContextValue(json, "\"visibleContext\"");
    const char* widget = cpContextValue(json, "\"widgetType\"");
    if (!id || !widget || *widget != '"') return CpContext();
    value = strtol(id, &end, 10);
    if (end == id || value != c.id || !*end || !strchr(" ,}\t\r\n", *end)) return CpContext();
    const char* closing = strchr(widget + 1, '"');
    if (!closing) return CpContext();
    size_t wl = (size_t)(closing - widget - 1);
    const char* projected = "eWidgetType_ProjectedUIWidget";
    const char* invalid = "GFXIDENTIFIER_INVALID";
    c.valid = true;
    c.native = wl && c.id < 50000 && !(wl == strlen(invalid) && !memcmp(widget + 1, invalid, wl));
    c.projected = c.native && wl == strlen(projected) && !memcmp(widget + 1, projected, wl);
    c.modified = modified;
    return c;
}

class CpHandoff {
public:
    struct Input {
        bool active, minimized, hidden, blocked, restoreEnabled, factoryMode, yieldEnabled;
        Input() : active(false), minimized(false), hidden(false), blocked(false), restoreEnabled(false),
                  factoryMode(false), yieldEnabled(false) {}
    };
    struct Action {
        bool restore, yield;
        Action() : restore(false), yield(false) {}
    };

    CpHandoff() : minimizing_(false), attempted_(false), timing_(false), haveLast_(false), sinceWall_(0),
                  sinceMs_(0) {}

    void reset() { minimizing_ = attempted_ = timing_ = haveLast_ = false; }

    Action step(uint32_t nowMs, time_t wall, const Input& in, const CpContext& context) {
        Action a;
        if (!in.active || !in.minimized) {
            minimizing_ = attempted_ = timing_ = false;
            last_ = context;
            haveLast_ = true;
            return a;
        }
        if (!minimizing_) {
            minimizing_ = true;
            attempted_ = timing_ = false;
            sinceWall_ = wall;
            baseline_ = haveLast_ ? last_ : context;
        }
        if (!in.hidden || in.blocked || !context.valid || !context.native) { timing_ = false; return a; }
        if (!context.projected) {
            timing_ = false;
            if (in.restoreEnabled && !in.factoryMode && !attempted_) {
                attempted_ = true;   // failure waits for an explicit return/new session, never a timed retry
                a.restore = true;
            }
            return a;
        }
        // A stale projected file from a previous daemon cannot claim the screen. MENU may precede the hide flag,
        // so freshness is needed for yielding, not for handing back a decoder the driver has deliberately left.
        bool fresh = context.modified >= sinceWall_ && context.modified <= wall && !context.same(baseline_);
        if (!fresh) { timing_ = false; return a; }
        if (!in.factoryMode || !in.yieldEnabled) { timing_ = false; return a; }
        if (!timing_ || !context.same(projected_)) {
            timing_ = true;
            // shortcut: same-second leave/return can share a revision; use an RHMI generation if it occurs in car logs.
            projected_ = context;   // a rewritten file can reveal a leave/return between polling ticks
            sinceMs_ = nowMs;
        } else if ((uint32_t)(nowMs - sinceMs_) >= 500u) {
            a.yield = true;
        }
        return a;
    }

private:
    bool minimizing_, attempted_, timing_, haveLast_;
    time_t sinceWall_;
    uint32_t sinceMs_;
    CpContext last_, baseline_, projected_;
};

} // namespace nbt
#endif
