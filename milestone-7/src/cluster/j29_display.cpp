// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "cluster/j29_display.hpp"

#include <stdarg.h>
#include <stdio.h>

namespace nbt {
namespace kombinav {

J29Display::J29Display(const Graph& g, const Link& link)
    : g_(g), link_(link), log_fn_(NULL), log_user_(NULL), sent_any_(false), full_next_(false), inplace_ok_(true),
      t_graph_(0), t_pos_(0), has_last_pos_(false), last_pos_(0) {}

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

bool J29Display::clear(std::string* detail) { return link_.cancel(link_.user, detail); }

void J29Display::forget() {
    sent_any_ = false;
    shown_lanes_.clear();
    has_last_pos_ = false;
    full_next_ = false;
    inplace_ok_ = true;
}

} // namespace kombinav
} // namespace nbt
