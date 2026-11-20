// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "cluster/j29_display.hpp"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cluster/nav_msg.hpp"

namespace nbt {
namespace kombinav {

namespace {
int envInt(const char* name, int lo, int hi, int def) {
    const char* e = getenv(name);
    if (!e || !*e) return def;
    char* end = NULL;
    long v = strtol(e, &end, 10);
    if (!end || *end || v < lo || v > hi) return def;
    return (int)v;
}
} // namespace

J29Display::J29Display(const Graph& g, const Link& link)
    : g_(g), link_(link), log_fn_(NULL), log_user_(NULL), sent_any_(false), full_next_(false), inplace_ok_(true),
      t_graph_(0), t_pos_(0), has_last_pos_(false), last_pos_(0), idle_dir_(0), idle_unit_(-1), idle_chars_(40),
      cycle_(0), cycle_from_(0), cycle_to_(63), cycle_s_(8), cycle_t0_(-1), rejected_value_(-1), idle_up_(false),
      shown_dir_(-1), shown_unit_(-1), idle_t_(0) {
    idle_dir_ = envInt("NBT_KOMBI_MEDIA_DIR", 0, 255, 0);
    idle_unit_ = envInt("NBT_KOMBI_MEDIA_UNIT", 0, 255, -1);
    idle_chars_ = (size_t)envInt("NBT_KOMBI_MEDIA_CHARS", 1, 60, 40);
    const char* d = getenv("NBT_KOMBI_MEDIA_DIST");
    if (d && utf8Length(d) <= 8) idle_dist_ = d;
    const char* c = getenv("NBT_KOMBI_MEDIA_CYCLE");
    if (c && !strcmp(c, "dir")) { cycle_ = 1; cycle_from_ = 0; cycle_to_ = 63; }
    if (c && !strcmp(c, "unit")) { cycle_ = 2; cycle_from_ = 0; cycle_to_ = 15; }
    cycle_from_ = envInt("NBT_KOMBI_MEDIA_CYCLE_FROM", 0, 255, cycle_from_);
    cycle_to_ = envInt("NBT_KOMBI_MEDIA_CYCLE_TO", 0, 255, cycle_to_);
    if (cycle_to_ < cycle_from_) cycle_to_ = cycle_from_;
    cycle_s_ = envInt("NBT_KOMBI_MEDIA_CYCLE_S", 2, 120, 8);
}

void J29Display::log(const char* fmt, ...) {
    char buf[320];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (log_fn_) log_fn_(buf, log_user_);
}

KombiClient::Result J29Display::sendLogged(const char* label, const std::vector<Frame>& frames, bool quiet) {
    std::string detail;
    KombiClient::Result r = link_.send(link_.user, frames, &detail);
    sent_any_ = true;
    if (!quiet || r != KombiClient::kOk)
        log("%s: %u frame(s) %s%s%s", label, (unsigned)frames.size(), r == KombiClient::kOk ? "OK" : "FAILED",
            detail.empty() || r == KombiClient::kOk ? "" : ": ", r == KombiClient::kOk ? "" : detail.c_str());
    return r;
}

ClusterDisplay::Result J29Display::send(const char* label, const std::vector<Frame>& frames, bool quiet) {
    KombiClient::Result r = sendLogged(label, frames, quiet);
    if (r == KombiClient::kOk) return kOk;
    return r == KombiClient::kRejected ? kRejected : kLink;
}

void J29Display::lanesOf(const NavView& v, KombiLanes& out) const {
    out.clear();
    if (s_.lanes) kombiLanes(v, out);                   // empty = no lane row (none sent, or nothing we can map)
}

// The phone adds / re-highlights / drops lanes as the junction nears: the same maneuver's graph is resent with the new
// lane row (the car-proven path), at most every lane_resend_s.
bool J29Display::sameManeuver(const NavView& v, double now) {
    full_next_ = false;                                  // guidance came back on the same maneuver: nothing to rebuild
    KombiLanes lanes;
    lanesOf(v, lanes);
    return lanes != shown_lanes_ && now - t_graph_ >= s_.lane_resend_s;
}

ClusterDisplay::Result J29Display::showManeuver(const NavView& v, double now, bool isNew, bool first) {
    int dir = kombiDirection(v.aa_type), ku = kombiUnit(v.aa_unit);
    std::vector<Frame> frames;
    KombiLanes lanes;
    lanesOf(v, lanes);
    std::string lane_note;
    if (!lanes.empty() || !isNew) lane_note = " | lanes " + describeLanes(lanes);
    log("%s type %d -> direction %d | %s %s | road '%s'%s", isNew ? "SHOW" : "LANES", v.aa_type, dir,
        v.dist_text.c_str(), kombiUnitName(ku), v.road.c_str(), lane_note.c_str());
    const KombiLanes* lp = lanes.empty() ? NULL : &lanes;
    bool done = false;
    if (!first && !full_next_ && inplace_ok_ && s_.inplace > 0 &&
        g_.maneuver(dir, v.road, v.dist_text, ku, frames, lp, s_.inplace != 2)) {
        KombiClient::Result r = sendLogged("maneuver", frames, false);
        if (r == KombiClient::kOk) {
            done = true;
        } else if (r == KombiClient::kRejected) {
            log("the KOMBI rejected the in-place update: full graphs until the next clear");
            inplace_ok_ = false;
        } else {
            return kLink;
        }
    }
    if (!done) {
        if (!g_.start(dir, v.road, v.dist_text, ku, frames, lp)) {
            log("internal error: the graph template has an unexpected layout");
            return kInternal;
        }
        Result r = send("graph", frames);
        if (r != kOk) return r;
    }
    full_next_ = false;
    shown_lanes_ = lanes;
    t_graph_ = now;
    has_last_pos_ = false;
    if (!lanes.empty()) return feed(v, now, true);       // the KOMBI checks lanes on the next position update
    return kOk;
}

ClusterDisplay::Result J29Display::updateDistance(const NavView& v, double) {
    int ku = kombiUnit(v.aa_unit);
    std::vector<Frame> frames;
    if (!g_.distance(v.dist_text, ku, frames)) {
        log("internal error: the distance template has an unexpected layout");
        return kInternal;
    }
    log("DIST %s %s", v.dist_text.c_str(), (ku < 0 ? "" : kombiUnitName(ku)));
    return send("distance", frames);
}

ClusterDisplay::Result J29Display::keepUp(const NavView& v, double now) {
    if (shown_lanes_.empty()) return kOk;
    return feed(v, now, false);
}

// The lane row's position feed: the car's route position from the metres to the maneuver (one 0x11 update, at most
// every lane_feed_s and only when it moved >= 1 m), so the KOMBI's lane-section check sees the car inside our section.
ClusterDisplay::Result J29Display::feed(const NavView& v, double now, bool force) {
    if (!force && now - t_pos_ < s_.lane_feed_s) return kOk;
    double m = 0, pos = 0;
    bool has = distanceMetres(v, m);
    if (!g_.feedPosition(has, m, pos)) return kOk;
    if (!force && has_last_pos_ && pos - last_pos_ < 1.0 && last_pos_ - pos < 1.0) return kOk;
    std::vector<Frame> frames;
    if (!g_.position(pos, frames)) {
        log("internal error: the graph template has no destination info: lane row without position updates");
        return kOk;
    }
    Result r = send("position", frames, true);
    if (r != kOk) return r;
    t_pos_ = now;
    has_last_pos_ = true;
    last_pos_ = pos;
    return kOk;
}

// ---- the idle text ("Song title in gauge cluster", experimental on the J29) ----

std::string J29Display::idleText(const std::string& utf8) const {
    std::string s;                                       // control characters out, runs of spaces folded
    for (size_t i = 0; i < utf8.size(); ++i) {
        unsigned char c = (unsigned char)utf8[i];
        if (c < 0x20 || c == 0x7F) c = ' ';
        if (c == ' ' && (s.empty() || s[s.size() - 1] == ' ')) continue;
        s += (char)c;
    }
    while (!s.empty() && s[s.size() - 1] == ' ') s.erase(s.size() - 1);
    if (utf8Length(s) <= idle_chars_) return s;
    std::string cut = utf8Prefix(s, idle_chars_ > 3 ? idle_chars_ - 3 : idle_chars_);
    while (!cut.empty() && cut[cut.size() - 1] == ' ') cut.erase(cut.size() - 1);
    return cut + "...";
}

// Our idle graph: the template patched like a maneuver with the title as its road, then maneuver 0's distance text
// replaced by ours (an empty string by default) and its unit byte by ours (if set).
bool J29Display::idleFrames(int dir, int unit, const std::string& road, bool inplace, std::vector<Frame>& out) const {
    bool ok = inplace ? g_.maneuver(dir, road, "", -1, out, NULL, s_.inplace != 2) : g_.start(dir, road, "", -1, out, NULL);
    if (!ok) return false;
    for (size_t i = 0; i < out.size(); ++i) {
        Bytes& b = out[i].body;
        if (out[i].method != 0x000A || modelType(b) != 29 || modelIndex(b) != 0) continue;
        size_t end = 0;
        if (!patchText(b, idle_dist_, &end)) return false;
        if (unit >= 0) {
            if (end >= b.size()) return false;
            b[end] = (uint8_t)unit;
        }
    }
    return true;
}

ClusterDisplay::Result J29Display::showIdleText(const std::string& title, double now) {
    if (title.empty()) return kOk;
    int dir = idle_dir_, unit = idle_unit_, value = -1;
    std::string line = title;
    if (cycle_) {
        if (cycle_t0_ < 0) cycle_t0_ = now;
        const int span = cycle_to_ - cycle_from_ + 1;
        value = cycle_from_ + (int)((now - cycle_t0_) / cycle_s_) % span;
        if (cycle_ == 1) dir = value; else unit = value;
        if (value == rejected_value_) return kOk;          // refused once: wait for the next value
        char tag[16];
        snprintf(tag, sizeof(tag), "[%c %d] ", cycle_ == 1 ? 'd' : 'u', value);
        line = tag + title;
    }
    const bool changed = !idle_up_ || line != idle_line_ || dir != shown_dir_ || unit != shown_unit_;
    const bool refresh = !changed && s_.media_refresh_s > 0 && now - idle_t_ >= s_.media_refresh_s;
    if (!changed && !refresh) return kOk;
    std::vector<Frame> frames;
    bool done = false;
    if (idle_up_ && inplace_ok_ && s_.inplace > 0 && idleFrames(dir, unit, line, true, frames)) {
        log("IDLE '%s' | direction %d | dist '%s' unit %d | in-place%s", line.c_str(), dir, idle_dist_.c_str(), unit,
            refresh ? " (refresh)" : "");
        KombiClient::Result r = sendLogged("idle", frames, false);
        if (r == KombiClient::kOk) {
            done = true;
        } else if (r == KombiClient::kRejected) {
            if (cycle_) {
                log("IDLE %s %d rejected by the KOMBI: skipped", cycle_ == 1 ? "direction" : "unit", value);
                rejected_value_ = value;
                idle_up_ = false;                            // the next value gets a full graph
                return kOk;
            }
            log("the KOMBI rejected the in-place idle update: full graphs until the next clear");
            inplace_ok_ = false;
        } else {
            return kLink;
        }
    }
    if (!done) {
        if (!idleFrames(dir, unit, line, false, frames)) {
            log("internal error: the graph template has an unexpected layout");
            return kInternal;
        }
        log("IDLE '%s' | direction %d | dist '%s' unit %d | full graph", line.c_str(), dir, idle_dist_.c_str(), unit);
        KombiClient::Result r = sendLogged("idle-graph", frames, false);
        if (r == KombiClient::kRejected && cycle_) {
            log("IDLE %s %d rejected by the KOMBI: skipped", cycle_ == 1 ? "direction" : "unit", value);
            rejected_value_ = value;
            idle_up_ = false;
            return kOk;
        }
        if (r != KombiClient::kOk) return r == KombiClient::kRejected ? kRejected : kLink;
    }
    idle_up_ = true;
    idle_line_ = line;
    shown_dir_ = dir;
    shown_unit_ = unit;
    idle_t_ = now;
    shown_lanes_.clear();
    return kOk;
}

bool J29Display::clear(std::string* detail) { return link_.cancel(link_.user, detail); }

void J29Display::forget() {
    sent_any_ = false;
    idle_up_ = false;
    idle_line_.clear();
    shown_lanes_.clear();
    has_last_pos_ = false;
    full_next_ = false;
    inplace_ok_ = true;
}

} // namespace kombinav
} // namespace nbt
