// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "cluster/hud6wa_display.hpp"

#include <stdarg.h>
#include <stdio.h>

namespace nbt {
namespace kombinav {

using namespace hudterm;

const char* hudUnitText(int aa_unit) {
    switch (aa_unit) {
        case 1: return "m";
        case 2: case 3: return "km";
        case 4: case 5: return "mi";
        case 6: return "ft";
        case 7: return "yd";
        default: return "";
    }
}

// The state machine has already shortened the distance and put "-" where there is none yet: the 6WA shows nothing
// then, rather than a character its font may not have.
HudTexts hudTexts(const NavView& v, const HudProfile& p) {
    HudTexts t;
    t.street = toClusterText(v.road, p.streetPunct, p.streetChars, false);
    if (v.dist_text != "-") t.distance = toClusterText(v.dist_text, ".", p.distanceChars, true);
    if (!t.distance.empty()) t.unit = hudUnitText(v.aa_unit);
    return t;
}

Hud6waDisplay::Hud6waDisplay(Port& port, const HudProfile& p, bool shadow)
    : port_(port), p_(p), shadow_(shadow), log_fn_(NULL), log_user_(NULL), sent_any_(false), fresh_(true),
      redraw_(false), held_(false), visible_(p.bufB), epoch_(0) {}

void Hud6waDisplay::log(const char* fmt, ...) {
    char buf[320];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (log_fn_) log_fn_(buf, log_user_);
}

// The terminal's state as a draw result. The epoch is read first, so work dropped after this read shows next time.
ClusterDisplay::Result Hud6waDisplay::health(uint32_t* epoch) {
    uint32_t e = port_.epoch();
    if (epoch) *epoch = e;
    Status st = port_.status();
    if (st == kFailed) return kRejected;
    if (st == kOffline) return kLink;
    if (st == kReady && sent_any_ && !shadow_ && e != epoch_ && !redraw_) {
        log("the cluster may have lost what we drew: drawing it again");
        redraw_ = true;
        fresh_ = true;
    }
    return kOk;
}

bool Hud6waDisplay::factoryActive() { return port_.status() == kNavActive; }

bool Hud6waDisplay::ready() { return port_.status() == kReady; }

Batch Hud6waDisplay::showBatch(bool isNew, const HudTexts& t, const ShapeSpec& spec) const {
    Batch b(isNew ? "maneuver" : "redraw", true);
    uint16_t hidden = visible_ == p_.bufA ? p_.bufB : p_.bufA;
    Shape design, fitted;
    drawShape(spec, design);
    fitShape(design, p_.glyph, fitted);
    b.add(kRemoveAllItems, removeAllItems(hidden));
    b.add(kSetFillColor, setFillColor(hidden, p_.fill));
    for (size_t i = 0; i < fitted.size(); ++i)
        b.add(kAddPolygon, addPolygon(hidden, (uint16_t)i, fitted[i].type, fitted[i].pts));
    b.add(kSwitchVisibility, switchVisibility(visible_, hidden));
    b.add(kRemoveAllItems, removeAllItems(visible_));
    if (fresh_) {
        b.add(kSetVisibility, setVisibility(p_.street, true));
        b.add(kSetVisibility, setVisibility(p_.distance, true));
        b.add(kSetVisibility, setVisibility(p_.unit, true));
    }
    b.add(kTextareaSetText, setText(p_.street, t.street.c_str()));
    b.add(kTextareaSetText, setText(p_.distance, t.distance.c_str()));
    b.add(kTextareaSetText, setText(p_.unit, t.unit.c_str()));
    b.add(kSetVisibility, setVisibility(p_.frame, true));
    return b;
}

Batch Hud6waDisplay::clearBatch() const {
    Batch b("clear", true, kSlotAll);
    b.add(kSetVisibility, setVisibility(p_.frame, false));
    b.add(kRemoveAllItems, removeAllItems(p_.bufA));
    b.add(kRemoveAllItems, removeAllItems(p_.bufB));
    b.add(kSwitchVisibility, switchVisibility(p_.bufA, p_.bufB));   // always: after a restart or lost work we may not know
    b.add(kTextareaSetText, setText(p_.street, ""));
    b.add(kTextareaSetText, setText(p_.distance, ""));
    b.add(kTextareaSetText, setText(p_.unit, ""));
    b.add(kSetVisibility, setVisibility(p_.street, false));
    return b;
}

// Shadow: every call logged, nothing submitted. Live: submitted; false when the terminal refused it (a call breaks a
// limit: our own data is wrong) or was no longer ready.
bool Hud6waDisplay::submit(const Batch& b, SubmitResult* r) {
    if (shadow_) {
        for (size_t i = 0; i < b.calls.size(); ++i) log("  [shadow] %s", describeCall(b.calls[i]).c_str());
        *r = kQueued;
        return true;
    }
    *r = port_.submit(b);
    if (*r == kRefused) log("internal error: the terminal refused the %s batch", b.label.c_str());
    return *r == kQueued || *r == kReplaced || *r == kMerged;
}

bool Hud6waDisplay::sameManeuver(const NavView&, double) {
    health(NULL);
    return redraw_ || held_;
}

ClusterDisplay::Result Hud6waDisplay::showManeuver(const NavView& v, double, bool isNew, bool) {
    uint32_t e;
    Result r = health(&e);
    if (r != kOk) return r;
    if (port_.status() == kNavActive) return kOk;   // the state machine yields on its next tick
    if (!shadow_ && port_.waiting(kSlotMain)) {
        // Only we add batches and the transport only takes them away, so once nothing waits, every batch before this
        // one runs (or is dropped, which changes the epoch and redraws everything).
        if (!held_) log("HOLD type %d: a batch is still waiting, drawn once it goes out", v.aa_type);
        held_ = true;
        return kOk;
    }
    HudTexts t = hudTexts(v, p_);
    ShapeSpec spec = shapeFor(v.aa_type, v.has_exit_angle, v.exit_angle);
    Batch b = showBatch(isNew, t, spec);
    log("%s type %d -> %s | '%s' '%s' | road '%s' -> '%s' | %u calls%s", isNew ? "SHOW" : "REDRAW", v.aa_type,
        describeSpec(spec).c_str(), t.distance.c_str(), t.unit.c_str(), v.road.c_str(), t.street.c_str(),
        (unsigned)b.calls.size(), shadow_ ? " (shadow: not sent)" : "");
    SubmitResult sr;
    bool ok = submit(b, &sr);
    sent_any_ = true;
    if (!ok) return sr == kRefused ? kInternal : health(NULL);
    visible_ = visible_ == p_.bufA ? p_.bufB : p_.bufA;
    fresh_ = false;
    redraw_ = false;
    held_ = false;
    epoch_ = e;
    shown_ = t;
    return kOk;
}

ClusterDisplay::Result Hud6waDisplay::updateDistance(const NavView& v, double) {
    Result r = health(NULL);
    if (r != kOk || port_.status() == kNavActive) return r;
    HudTexts t = hudTexts(v, p_);
    Batch b("distance");
    if (t.distance != shown_.distance) b.add(kTextareaSetText, setText(p_.distance, t.distance.c_str()));
    if (t.unit != shown_.unit) b.add(kTextareaSetText, setText(p_.unit, t.unit.c_str()));
    if (b.calls.empty()) return kOk;
    log("DIST '%s' '%s'%s", t.distance.c_str(), t.unit.c_str(), shadow_ ? " (shadow: not sent)" : "");
    SubmitResult sr;
    bool ok = submit(b, &sr);
    sent_any_ = true;
    if (!ok) return sr == kRefused ? kInternal : health(NULL);
    shown_.distance = t.distance;
    shown_.unit = t.unit;
    return kOk;
}

ClusterDisplay::Result Hud6waDisplay::keepUp(const NavView&, double) { return health(NULL); }

// Partial, so it never takes a waiting maneuver's place: merged into it, or sent after it.
ClusterDisplay::Result Hud6waDisplay::remind(const NavView& v, double) {
    Result r = health(NULL);
    if (r != kOk || port_.status() == kNavActive || !sent_any_) return r;
    Batch b("remind");
    b.add(kSetVisibility, setVisibility(p_.frame, true));
    log("REMIND type %d: panel on again%s", v.aa_type, shadow_ ? " (shadow: not sent)" : "");
    SubmitResult sr;
    if (!submit(b, &sr)) return sr == kRefused ? kInternal : health(NULL);
    return kOk;
}

bool Hud6waDisplay::clear(std::string* detail) {
    Status st = port_.status();
    if (st == kNavActive) {
        log("the factory navigation has the panel: it clears it itself");
        return true;
    }
    if (st == kFailed && !shadow_) {
        port_.reset();                                // one best-effort try
        st = port_.status();
    }
    if (st != kReady && !shadow_) {
        if (detail) *detail = std::string("HUDTerminal ") + statusName(st);
        return false;
    }
    Batch b = clearBatch();
    if (shadow_) log("CLEAR batch: %u calls (shadow: not sent)", (unsigned)b.calls.size());
    SubmitResult sr;
    if (!submit(b, &sr)) {
        if (detail) *detail = std::string("HUDTerminal ") + submitResultName(sr);
        return false;
    }
    visible_ = p_.bufB;
    if (shadow_) return true;
    if (port_.waitIdle(kClearWaitMs)) return true;
    if (detail) *detail = std::string("no answer to every call, HUDTerminal ") + statusName(port_.status());
    return false;
}

void Hud6waDisplay::forget() {
    sent_any_ = false;
    fresh_ = true;
    redraw_ = false;
    held_ = false;
    visible_ = p_.bufB;                               // where a clear or the navigation's end of route leaves it
    shown_ = HudTexts();
}

} // namespace kombinav
} // namespace nbt
