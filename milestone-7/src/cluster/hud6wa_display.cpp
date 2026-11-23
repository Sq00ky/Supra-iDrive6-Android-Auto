// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "cluster/hud6wa_display.hpp"
#include "cluster/hudterm_digits.hpp"
#include "cluster/hudterm_ring.hpp"
#include "cluster/mapbox_hud.hpp"   // Mapbox maneuver glyphs (NBT_HUD_GLYPHS); falls back to the parametric shapes

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>   // getenv (NBT_HUD_CYRILLIC)
#include <string.h>   // strcmp

#include "nbt_paths.hpp"

namespace nbt {
namespace kombinav {

using namespace hudterm;

// NBT_HUD_CYRILLIC: keep Cyrillic street names (sent as UTF-16BE) instead of blanking them. Off unless set to a
// non-"0" value, since it needs the cluster font to carry the glyphs.
static bool hudCyrillicEnabled() {
    // The Settings checkbox writes <data dir>/hud-cyrillic. The env var wins when set, so a hand-set
    // NBT_HUD_CYRILLIC=0 turns it off even with the box ticked (and =1 turns it on without the box).
    const char* e = getenv("NBT_HUD_CYRILLIC");
    if (e && *e)
        return !(strcmp(e, "0") == 0 || strcmp(e, "off") == 0 || strcmp(e, "no") == 0 || strcmp(e, "false") == 0);
    return nbt::flagSet(nbt::kHudCyrillicFlagName);
}

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
    t.street = toClusterText(v.road, p.streetPunct, p.streetChars, false, hudCyrillicEnabled());
    if (v.dist_text != "-") t.distance = toClusterText(v.dist_text, ".", p.distanceChars, true);
    if (!t.distance.empty()) t.unit = hudUnitText(v.aa_unit);
    return t;
}

bool sameLanes(const std::vector<AaLane>& a, const std::vector<AaLane>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].size() != b[i].size()) return false;
        for (size_t k = 0; k < a[i].size(); ++k)
            if (a[i][k].shape != b[i][k].shape || a[i][k].highlighted != b[i][k].highlighted) return false;
    }
    return true;
}

namespace {
// A picture's items into handle h: the fill colour set whenever it changes (it applies to the items added after it).
void addItems(Batch& b, uint16_t h, const Picture& pic) {
    int fill = -1;
    for (size_t i = 0; i < pic.size(); ++i) {
        if (pic[i].colour != fill) {
            fill = pic[i].colour;
            b.add(kSetFillColor, setFillColor(h, pic[i].colour));
        }
        b.add(kAddPolygon, addPolygon(h, (uint16_t)i, pic[i].poly.type, pic[i].poly.pts));
    }
}
// Hide the surfaces the cluster draws over the panel itself during guidance (NBT_HUD_HIDE, e.g. 0x15/0x16): on every
// picture (after frame-on) and every distance update, so the strip never shows back through our drawing.
void addHides(Batch& b, const HudProfile& p) {
    for (int i = 0; i < p.nHide; i++) b.add(kSetVisibility, setVisibility(p.hide[i], false));
}
}

Hud6waDisplay::Hud6waDisplay(Port& port, const HudProfile& p, bool shadow)
    : port_(port), p_(p), base_(p), shadow_(shadow), log_fn_(NULL), log_user_(NULL), sent_any_(false), fresh_(true),
      redraw_(false), held_(false), claim_(true), visible_(p.bufB), epoch_(0), measured_w_(0), measured_h_(0), has_layout_(false),
      near_(false), near_shown_(false), near_m_(0), stage_(0), stage_shown_(0), route_visible_(p.routeB),
      route_dirty_(false), lane_visible_(p.laneB), lanes_dirty_(false), idle_(false), idle_t_(0) {}

void Hud6waDisplay::log(const char* fmt, ...) {
    char buf[320];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (log_fn_) log_fn_(buf, log_user_);
}

// The terminal's state as a draw result. The epoch is read first, so work dropped after this read shows next time.
// A guidance session that has drawn nothing yet takes the HUD from a navigation gone quiet with its route still on
// screen (Port::claim: what CarPlay Advanced leaves when its session ends mid-route), and draws everything afresh:
// the step it missed while yielding may already count as shown.
ClusterDisplay::Result Hud6waDisplay::health(uint32_t* epoch) {
    if (claim_ && !shadow_ && port_.status() == kNavActive && port_.claim()) {
        log("a quiet navigation still held the HUD: taken for our new guidance, drawn afresh");
        redraw_ = true;
        fresh_ = true;
    }
    uint32_t e = port_.epoch();
    if (epoch) *epoch = e;
    Status st = port_.status();
    if (st == kFailed) return kRejected;
    if (st == kOffline) return kLink;
    // The cluster's own picture-buffer size, once the terminal has it: the canvas becomes it, and the glyph box moves
    // where the profile's would be cut off (fitMeasured; env pins still win). On the F30's 126x77 HUD that moves the
    // F20-sized defaults to the factory arrow's place; on a buffer they fit, nothing moves.
    int mw, mh;
    if (port_.measuredSize(mw, mh) && (mw != measured_w_ || mh != measured_h_)) {
        p_.canvas = base_.canvas;   // from the profile as given: the HUD's box must not stay in the cluster's field
        p_.glyph = base_.glyph;
        fitMeasured(mw, mh, p_);
        has_layout_ = false;   // laid out for the old buffer: the next maneuver lays out (and checks) on this one
        measured_w_ = mw;
        measured_h_ = mh;
        log("HUD picture buffer measured %dx%d: canvas %d,%d-%d,%d glyph %d,%d-%d,%d", mw, mh, p_.canvas.x0, p_.canvas.y0,
            p_.canvas.x1, p_.canvas.y1, p_.glyph.x0, p_.glyph.y0, p_.glyph.x1, p_.glyph.y1);
        if (sent_any_ && !shadow_ && !redraw_) {   // something is up at the old geometry: draw it again, fitted
            redraw_ = true;
            fresh_ = true;
            stage_shown_ = 0;
            if (lanes_dirty_ || !lanes_shown_.empty()) { lanes_shown_.clear(); lanes_dirty_ = true; }
        }
    }
    if (st == kReady && sent_any_ && !shadow_ && e != epoch_ && !redraw_) {
        log("the cluster may have lost what we drew: drawing it again");
        redraw_ = true;
        fresh_ = true;
        stage_shown_ = 0;                    // the route and the lanes too, once the picture is back
        if (lanes_dirty_ || !lanes_shown_.empty()) {
            lanes_shown_.clear();
            lanes_dirty_ = true;
        }
    }
    return kOk;
}

bool Hud6waDisplay::factoryActive() { return port_.status() == kNavActive; }

bool Hud6waDisplay::ready() { return port_.status() == kReady; }

JunctionColours Hud6waDisplay::colours() const {
    JunctionColours c = { p_.edge, p_.road, p_.route };
    return c;
}

Batch Hud6waDisplay::pictureBatch(const char* label, const Picture& pic, const HudTexts& t, bool emptyRoute) const {
    Batch b(label, true, kSlotMain);
    uint16_t hidden = visible_ == p_.bufA ? p_.bufB : p_.bufA;
    b.add(kRemoveAllItems, removeAllItems(hidden));
    addItems(b, hidden, pic);
    b.add(kSwitchVisibility, switchVisibility(visible_, hidden));
    b.add(kRemoveAllItems, removeAllItems(visible_));
    if (emptyRoute && routeInOverlay()) {
        b.add(kRemoveAllItems, removeAllItems(p_.routeA));
        b.add(kRemoveAllItems, removeAllItems(p_.routeB));
    }
    if (fresh_) {
        b.add(kSetVisibility, setVisibility(p_.street, true));
        b.add(kSetVisibility, setVisibility(p_.distance, true));
        b.add(kSetVisibility, setVisibility(p_.unit, true));
    }
    b.add(kTextareaSetText, setText(p_.street, t.street.c_str()));
    b.add(kTextareaSetText, setText(p_.distance, t.distance.c_str()));
    b.add(kTextareaSetText, setText(p_.unit, t.unit.c_str()));
    b.add(kSetVisibility, setVisibility(p_.frame, true));
    addHides(b, p_);                                    // kill the cluster's own strips (0x15/0x16) after frame-on
    return b;
}

namespace {
// The F30 HUD was checked at this size; other buffers and hand-tuned boxes keep their own geometry.
bool readableHud(const HudProfile& p) {
    return !getenv("NBT_HUD_CANVAS") && !getenv("NBT_HUD_GLYPH") && p.canvas.x0 == 0 && p.canvas.y0 == 0
        && p.canvas.x1 == 125 && p.canvas.y1 == 76 && p.glyph.x0 == 37 && p.glyph.y0 == 23
        && p.glyph.x1 == 82 && p.glyph.y1 == 68;
}

void drawHudRoute(const Layout& layout, int tail, const HudProfile& profile, const JunctionColours& colours,
                  Picture& picture) {
    JunctionStyle style = kJunctionStyle;
    bool readable = readableHud(profile) && !layout.ring;
    if (readable) {
        int heading = layout.route.heads.empty() ? 0 : layout.route.heads.back();
        // Match the far icon's visual weight without enlarging its roads or moving its on-canvas tip.
        if (mbxEnabled()) {
            if (heading == -135 || heading == 135) {
                style.routeW = 20; style.routeCore = 16;
            }
            style.headL = 34; style.headW = heading == 180 ? 22 : 27;
        } else {
            style.routeW = 24; style.routeCore = 20;
            style.headL = 28; style.headW = heading == 180 ? 19 : 23;
        }
    }
    drawRoute(layout, tail, colours, style, picture, readable ? kJunctionStyle.headL : -1, mbxEnabled());
}

void readableArrow(const ShapeSpec& spec, const HudProfile& p, Picture& pic) {
    HudProfile design = p;
    design.glyph = Rect{ 0, 0, kDesign, kDesign };
    Picture source;
    if (!mbxManeuver(spec, design, source)) {
        Shape shape;
        drawShape(spec, shape);
        for (size_t i = 0; i < shape.size(); ++i) {
            Item item; item.colour = p.fill; item.poly = shape[i]; source.push_back(item);
        }
    }
    Rect bounds = { kDesign, kDesign, 0, 0 };
    for (size_t i = 0; i < source.size(); ++i)
        for (size_t j = 0; j < source[i].poly.pts.size(); ++j) {
            const Point& pt = source[i].poly.pts[j];
            bounds.x0 = std::min(bounds.x0, pt.x); bounds.x1 = std::max(bounds.x1, pt.x);
            bounds.y0 = std::min(bounds.y0, pt.y); bounds.y1 = std::max(bounds.y1, pt.y);
        }
    Rect original;
    if (mbxEnabled() && mbxOriginalBounds(kombinav::kombiDirection(spec.aa_type), original)) {
        Point lo = mbxPoint(original.x0, original.y0, false, design.glyph);
        Point hi = mbxPoint(original.x1, original.y1, false, design.glyph);
        bounds = Rect{ lo.x, lo.y, hi.x, hi.y };
    }
    int extent = std::max(bounds.x1 - bounds.x0, bounds.y1 - bounds.y0);
    if (extent <= 0) return;
    const Rect box = { 36, 16, 89, 69 };
    int side = box.x1 - box.x0;
    long denominator = 2L * extent;
    for (size_t i = 0; i < source.size(); ++i) {
        Item item = source[i];
        for (size_t j = 0; j < item.poly.pts.size(); ++j) {
            Point& pt = item.poly.pts[j];
            long x = (long)(box.x0 + box.x1) * extent + (2L * pt.x - bounds.x0 - bounds.x1) * side;
            long y = (long)(box.y0 + box.y1) * extent + (2L * pt.y - bounds.y0 - bounds.y1) * side;
            pt.x = (int16_t)((x + denominator / 2) / denominator);
            pt.y = (int16_t)((y + denominator / 2) / denominator);
        }
        long area = 0;
        for (size_t j = 0; j < item.poly.pts.size(); ++j) {
            const Point& a = item.poly.pts[j];
            const Point& b = item.poly.pts[j + 1 < item.poly.pts.size() ? j + 1 : 0];
            area += (long)a.x * b.y - (long)b.x * a.y;
        }
        if (area) pic.push_back(item);   // a tessellation sliver can vanish after rounding to HUD pixels
    }
}

bool arrowPicture(const ShapeSpec& spec, const HudProfile& p, Picture& pic, int exitNumber = 0) {
    if (spec.kind == kShapeRoundabout) return roundaboutPicture(spec, p, pic, exitNumber);
    if (readableHud(p)) {
        readableArrow(spec, p, pic);
        return true;
    }
    if (mbxManeuver(spec, p, pic)) return true;   // Mapbox ordinary glyph; else the parametric shape below
    Shape design, fitted;
    drawShape(spec, design);
    Rect box = p.glyph;
    fitShape(design, box, fitted);
    for (size_t i = 0; i < fitted.size(); ++i) {
        Item it;
        it.colour = p.fill;
        it.poly = fitted[i];
        pic.push_back(it);
    }
    return true;
}
}

Batch Hud6waDisplay::showBatch(bool isNew, const HudTexts& t, const ShapeSpec& spec) const {
    Picture pic;
    arrowPicture(spec, p_, pic);
    return pictureBatch(isNew ? "maneuver" : "redraw", pic, t, false);
}

Batch Hud6waDisplay::routeBatch(const Picture& pic) const {
    Batch b("route", true, kSlotRoute);
    uint16_t hidden = route_visible_ == p_.routeA ? p_.routeB : p_.routeA;
    b.add(kRemoveAllItems, removeAllItems(hidden));
    addItems(b, hidden, pic);
    b.add(kSwitchVisibility, switchVisibility(route_visible_, hidden));
    b.add(kRemoveAllItems, removeAllItems(route_visible_));
    return b;
}

Batch Hud6waDisplay::lanesBatch(const Picture& pic) const {
    if (pic.empty()) {
        Batch b("lanes off", true, kSlotLanes);
        b.add(kRemoveAllItems, removeAllItems(p_.laneA));
        b.add(kRemoveAllItems, removeAllItems(p_.laneB));
        return b;
    }
    Batch b("lanes", true, kSlotLanes);
    uint16_t hidden = lane_visible_ == p_.laneA ? p_.laneB : p_.laneA;
    b.add(kRemoveAllItems, removeAllItems(hidden));
    addItems(b, hidden, pic);
    b.add(kSwitchVisibility, switchVisibility(lane_visible_, hidden));
    b.add(kRemoveAllItems, removeAllItems(lane_visible_));
    return b;
}

Batch Hud6waDisplay::clearBatch() const {
    Batch b("clear", true, kSlotAll);
    b.add(kSetVisibility, setVisibility(p_.frame, false));
    b.add(kRemoveAllItems, removeAllItems(p_.bufA));
    b.add(kRemoveAllItems, removeAllItems(p_.bufB));
    b.add(kSwitchVisibility, switchVisibility(p_.bufA, p_.bufB));   // always: after a restart or lost work we may not know
    if (s_.junction && routeInOverlay()) {
        b.add(kRemoveAllItems, removeAllItems(p_.routeA));
        b.add(kRemoveAllItems, removeAllItems(p_.routeB));
        b.add(kSwitchVisibility, switchVisibility(p_.routeA, p_.routeB));
    }
    if (lanesOn()) {
        b.add(kRemoveAllItems, removeAllItems(p_.laneA));
        b.add(kRemoveAllItems, removeAllItems(p_.laneB));
        b.add(kSwitchVisibility, switchVisibility(p_.laneA, p_.laneB));
    }
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

// ---- the junction view --------------------------------------------------------------------------------------------

// Due from the main prompt, as the state machine times it (the phone's seconds, or an off-ramp's or a fork's distance),
// or within near_m whatever the approach rule. A position stored before the step changed may be the last step's.
bool Hud6waDisplay::nearDue(const NavView& v, double* m) const {
    if (m) *m = 0;
    if (!s_.junction || !has_layout_) return false;
    bool stale = v.pos_seq != 0 && v.pos_seq == v.step_pos_seq;
    double d = 0;
    bool has_m = !stale && distanceMetres(v, d);
    if (m && has_m) *m = d;
    if (has_m && d <= s_.near_m) return true;
    if (stale || s_.approach != kApproachTime || s_.main_s <= 0) return false;
    if (v.has_step_s && v.step_s <= s_.main_s) return true;
    return has_m && motorwayTurn(v.aa_type) && d <= s_.mway_main_m;
}

void Hud6waDisplay::startManeuver(const NavView& v) {
    has_layout_ = s_.junction && shapeFor(v.aa_type, v.has_exit_angle, v.exit_angle).kind != kShapeRoundabout &&
                  junctionLayout(v.aa_type, v.has_exit_angle, v.exit_angle, v.has_exit_number,
                                 v.exit_number, k6wa.glyph, k6wa.canvas.y1, layout_);
    if (has_layout_ && readableHud(p_)) lengthenHudRoute(v.aa_type, layout_);
    if (has_layout_ && !junctionShows()) {   // drawn scaled onto this buffer, its way would still be cut off
        has_layout_ = false;
        log("type %d: junction view would leave the %dx%d picture buffer: the plain arrow", v.aa_type,
            p_.canvas.x1 - p_.canvas.x0 + 1, p_.canvas.y1 - p_.canvas.y0 + 1);
    }
    near_ = false;
    near_m_ = 0;
    stage_ = 0;
    stage_shown_ = 0;
    double m;
    if (nearDue(v, &m)) {
        near_ = true;
        near_m_ = m;
    }
    updateStage(v);
}

// The stage only ever goes down on one maneuver: traffic or a detour doesn't grow the route back.
void Hud6waDisplay::updateStage(const NavView& v) {
    if (!near_) {
        stage_ = 0;
        return;
    }
    bool stale = v.pos_seq != 0 && v.pos_seq == v.step_pos_seq;
    double m = 0;
    bool has_m = !stale && distanceMetres(v, m);
    if (has_m && near_m_ <= 0) near_m_ = m;
    int q = kStages;
    if (has_m && near_m_ > 0) {
        double x = kStages * m / near_m_;
        q = (int)x;
        if (q < x) ++q;
        if (q < 1) q = 1;
        if (q > kStages) q = kStages;
    }
    stage_ = stage_ == 0 || q < stage_ ? q : stage_;
}

ClusterDisplay::Result Hud6waDisplay::sendRoute() {
    if (!near_ || !near_shown_ || !routeInOverlay() || redraw_ || held_ || stage_ == 0 || stage_ == stage_shown_)
        return kOk;
    if (!shadow_ && port_.waiting(kSlotRoute)) return kOk;   // drawn on a later step, from the newest stage
    Picture pic;
    int tail = tailFor(layout_, stage_, kStages);
    drawHudRoute(layout_, tail, p_, colours(), pic);
    fitJunction(pic);
    Batch b = routeBatch(pic);
    log("ROUTE stage %d/%d, tail %d | %u calls%s", stage_, (int)kStages, tail, (unsigned)b.calls.size(),
        shadow_ ? " (shadow: not sent)" : "");
    SubmitResult sr;
    bool ok = submit(b, &sr);
    sent_any_ = true;
    if (!ok) return sr == kRefused ? kInternal : health(NULL);
    route_visible_ = route_visible_ == p_.routeA ? p_.routeB : p_.routeA;
    route_dirty_ = true;
    stage_shown_ = stage_;
    return kOk;
}

ClusterDisplay::Result Hud6waDisplay::sendLanes(const NavView& v) {
    if (!lanesOn() || redraw_ || held_) return kOk;
    Picture pic;
    bool draw = !v.lanes.empty() && drawLanes(v.lanes, p_.lanes, p_.road, p_.route, pic);
    if (!draw) {
        if (!lanes_dirty_) return kOk;
        pic.clear();
    } else if (lanes_dirty_ && sameLanes(v.lanes, lanes_shown_)) {
        return kOk;
    }
    if (!shadow_ && port_.waiting(kSlotLanes)) return kOk;
    Batch b = lanesBatch(pic);
    log("LANES %u%s | %u calls%s", (unsigned)(draw ? v.lanes.size() : 0), draw ? "" : " (off)",
        (unsigned)b.calls.size(), shadow_ ? " (shadow: not sent)" : "");
    SubmitResult sr;
    bool ok = submit(b, &sr);
    sent_any_ = true;
    if (!ok) return sr == kRefused ? kInternal : health(NULL);
    if (draw) {
        lane_visible_ = lane_visible_ == p_.laneA ? p_.laneB : p_.laneA;
        lanes_dirty_ = true;
        lanes_shown_ = v.lanes;
    } else {
        lanes_dirty_ = false;
        lanes_shown_.clear();
    }
    return kOk;
}

// ---- the driver -----------------------------------------------------------------------------------------------------

bool Hud6waDisplay::sameManeuver(const NavView& v, double) {
    health(NULL);
    double m;
    if (!near_ && nearDue(v, &m)) {
        near_ = true;
        near_m_ = m;
        log("NEAR type %d: %lld s, %.0f m: junction view", v.aa_type, (long long)(v.has_step_s ? v.step_s : -1), m);
    }
    updateStage(v);
    bool junction = near_ && !near_shown_;
    bool stageInPicture = near_ && near_shown_ && !routeInOverlay() && stage_ != stage_shown_;
    return redraw_ || held_ || junction || stageInPicture;
}

ClusterDisplay::Result Hud6waDisplay::showManeuver(const NavView& v, double, bool isNew, bool) {
    uint32_t e;
    Result r = health(&e);
    if (r != kOk) return r;
    if (port_.status() == kNavActive) return kOk;   // the state machine yields on its next tick
    if (isNew) {
        startManeuver(v);
        if (near_) log("NEAR type %d: %lld s, %.0f m: junction view at once", v.aa_type,
                       (long long)(v.has_step_s ? v.step_s : -1), near_m_);
    } else {
        updateStage(v);
    }
    if (!shadow_ && port_.waiting(kSlotMain)) {
        // Only we add batches and the transport only takes them away, so once nothing of the slot waits, every batch
        // before this one runs (or is dropped, which changes the epoch and redraws everything).
        if (!held_) log("HOLD type %d: a batch is still waiting, drawn once it goes out", v.aa_type);
        held_ = true;
        return kOk;
    }
    HudTexts t = hudTexts(v, p_);
    ShapeSpec spec = shapeFor(v.aa_type, v.has_exit_angle, v.exit_angle);
    bool junction = near_ && has_layout_;
    Picture pic;
    std::string what;
    if (junction) {
        drawRoads(layout_, colours(), kJunctionStyle, pic);
        if (!routeInOverlay()) drawHudRoute(layout_, tailFor(layout_, stage_, kStages), p_, colours(), pic);
        fitJunction(pic);
        what = "junction view, " + describeSpec(spec);
        if (layout_.exitNumber) {
            char n[24];
            snprintf(n, sizeof(n), " exit %d", layout_.exitNumber);
            what += n;
        }
    } else {
        int number = v.has_exit_number && v.exit_number >= 1 && v.exit_number <= 99 ? (int)v.exit_number : 0;
        if (!arrowPicture(spec, p_, pic, number))
            log("exit number %d omitted: no room for a readable number in this canvas", number);
        what = describeSpec(spec);
        if (spec.kind == kShapeRoundabout && number) {
            char n[24];
            snprintf(n, sizeof(n), " number %d", number);
            what += n;
        }
    }
    bool emptyRoute = !junction && route_dirty_;
    Batch b = pictureBatch(isNew ? "maneuver" : "redraw", pic, t, emptyRoute);
    log("%s type %d -> %s | '%s' '%s' | road '%s' -> '%s' | %u calls%s", isNew ? "SHOW" : "REDRAW", v.aa_type,
        what.c_str(), t.distance.c_str(), t.unit.c_str(), v.road.c_str(), t.street.c_str(), (unsigned)b.calls.size(),
        shadow_ ? " (shadow: not sent)" : "");
    SubmitResult sr;
    bool ok = submit(b, &sr);
    sent_any_ = true;
    if (!ok) return sr == kRefused ? kInternal : health(NULL);
    visible_ = visible_ == p_.bufA ? p_.bufB : p_.bufA;
    fresh_ = false;
    redraw_ = false;
    held_ = false;
    claim_ = false;
    epoch_ = e;
    shown_ = t;
    near_shown_ = junction;
    idle_ = false;                                    // the maneuver's street text has replaced the idle text
    if (emptyRoute) route_dirty_ = false;
    if (!junction) stage_shown_ = 0;
    else if (!routeInOverlay()) stage_shown_ = stage_;
    r = sendRoute();
    if (r != kOk) return r;
    return sendLanes(v);
}

// ---- the idle text ("now playing") ----

std::string Hud6waDisplay::idleText(const std::string& utf8) const {
    return toClusterText(utf8, p_.streetPunct, p_.streetChars, false);
}

// Full: everything we show while idle. The picture buffers, and a route or lane layer a maneuver may have left, are
// emptied so that nothing is left under the text.
Batch Hud6waDisplay::idleBatch(const std::string& line) const {
    Batch b("media", true, kSlotMain);
    b.add(kRemoveAllItems, removeAllItems(p_.bufA));
    b.add(kRemoveAllItems, removeAllItems(p_.bufB));
    if (route_dirty_ && routeInOverlay()) {
        b.add(kRemoveAllItems, removeAllItems(p_.routeA));
        b.add(kRemoveAllItems, removeAllItems(p_.routeB));
    }
    if (lanes_dirty_ && p_.laneA && p_.laneB) {
        b.add(kRemoveAllItems, removeAllItems(p_.laneA));
        b.add(kRemoveAllItems, removeAllItems(p_.laneB));
    }
    if (fresh_) {
        b.add(kSetVisibility, setVisibility(p_.street, true));
        b.add(kSetVisibility, setVisibility(p_.distance, true));
        b.add(kSetVisibility, setVisibility(p_.unit, true));
    }
    b.add(kTextareaSetText, setText(p_.street, line.c_str()));
    b.add(kTextareaSetText, setText(p_.distance, ""));
    b.add(kTextareaSetText, setText(p_.unit, ""));
    b.add(kSetVisibility, setVisibility(p_.frame, true));
    return b;
}

ClusterDisplay::Result Hud6waDisplay::showIdleText(const std::string& line, double now) {
    uint32_t e;
    Result r = health(&e);
    if (r != kOk || port_.status() == kNavActive || line.empty()) return r;
    const bool whole = !idle_ || redraw_;
    const bool changed = !whole && line != shown_.street;
    const bool refresh = !whole && !changed && s_.media_refresh_s > 0 && now - idle_t_ >= s_.media_refresh_s;
    if (!whole && !changed && !refresh) return kOk;
    // Like a maneuver, the whole picture is built on what the batches before it leave: it waits for the queue.
    if (whole && !shadow_ && port_.waiting(kSlotMain)) return kOk;
    Batch b = whole ? idleBatch(line) : Batch(changed ? "media-text" : "media-refresh");
    if (changed) b.add(kTextareaSetText, setText(p_.street, line.c_str()));
    if (!whole) b.add(kSetVisibility, setVisibility(p_.frame, true));
    if (refresh) log("MEDIA panel on again%s", shadow_ ? " (shadow: not sent)" : "");
    else log("MEDIA '%s' | %u calls%s", line.c_str(), (unsigned)b.calls.size(), shadow_ ? " (shadow: not sent)" : "");
    SubmitResult sr;
    bool ok = submit(b, &sr);
    sent_any_ = true;
    if (!ok) return sr == kRefused ? kInternal : health(NULL);
    if (whole) {
        fresh_ = false;
        redraw_ = false;
        held_ = false;
        epoch_ = e;
        shown_ = HudTexts();
        route_dirty_ = false;
        lanes_dirty_ = false;
        near_shown_ = false;
        stage_shown_ = 0;
    }
    shown_.street = line;
    idle_ = true;
    idle_t_ = now;
    return kOk;
}

ClusterDisplay::Result Hud6waDisplay::updateDistance(const NavView& v, double) {
    Result r = health(NULL);
    if (r != kOk || port_.status() == kNavActive) return r;
    HudTexts t = hudTexts(v, p_);
    Batch b("distance");
    if (t.distance != shown_.distance) b.add(kTextareaSetText, setText(p_.distance, t.distance.c_str()));
    if (t.unit != shown_.unit) b.add(kTextareaSetText, setText(p_.unit, t.unit.c_str()));
    addHides(b, p_);                                    // the cluster redraws its strips as the distance changes
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

// Each step on the maneuver shown: the route's next stage, and the phone's lanes.
ClusterDisplay::Result Hud6waDisplay::keepUp(const NavView& v, double) {
    Result r = health(NULL);
    if (r != kOk || port_.status() == kNavActive) return r;
    updateStage(v);
    r = sendRoute();
    if (r != kOk) return r;
    return sendLanes(v);
}

// Partial, so it never takes a waiting maneuver's place: merged into it, or sent after it.
ClusterDisplay::Result Hud6waDisplay::remind(const NavView& v, double) {
    Result r = health(NULL);
    if (r != kOk || port_.status() == kNavActive || !sent_any_) return r;
    Batch b("remind");
    b.add(kSetVisibility, setVisibility(p_.frame, true));
    addHides(b, p_);
    log("REMIND type %d: panel on again%s", v.aa_type, shadow_ ? " (shadow: not sent)" : "");
    SubmitResult sr;
    if (!submit(b, &sr)) return sr == kRefused ? kInternal : health(NULL);
    return kOk;
}

bool Hud6waDisplay::clear(std::string* detail) {
    claim_ = true;                                    // our guidance ends: the next one starts a new session
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
    route_visible_ = p_.routeB;
    route_dirty_ = false;
    if (lanesOn()) {
        lane_visible_ = p_.laneB;
        lanes_dirty_ = false;
        lanes_shown_.clear();
    }
    if (shadow_) return true;
    if (port_.waitIdle(kClearWaitMs)) return true;
    if (detail) *detail = std::string("no answer to every call, HUDTerminal ") + statusName(port_.status());
    return false;
}

void Hud6waDisplay::forget() {
    sent_any_ = false;
    claim_ = true;
    fresh_ = true;
    redraw_ = false;
    held_ = false;
    visible_ = p_.bufB;                               // where a clear or the navigation's end of route leaves it
    shown_ = HudTexts();
    has_layout_ = false;
    near_ = false;
    near_shown_ = false;
    near_m_ = 0;
    stage_ = 0;
    stage_shown_ = 0;
    route_visible_ = p_.routeB;
    route_dirty_ = false;
    lanes_shown_.clear();
    lane_visible_ = p_.laneB;
    lanes_dirty_ = false;
    idle_ = false;
}

} // namespace kombinav
} // namespace nbt
