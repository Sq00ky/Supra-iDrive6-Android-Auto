// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// The 6WA driver: Android Auto guidance drawn on an F-series cluster's MOST HUDTerminal (cluster/hudterm.hpp) through
// HudTerminal (cluster/hud_terminal.hpp), with the instances of a fixed profile (cluster/hud6wa_profile.hpp) and our
// own pictures (cluster/hudterm_shapes.hpp). Each event is one batch of calls, sent back to back:
//   maneuver    RemoveAllItems(hidden buffer), SetFillColor(hidden), the picture's polygons into it,
//               SwitchVisibility(shown -> hidden), RemoveAllItems(old buffer), [the three texts switched on, the first
//               time], street, distance and unit texts, the panel (frame) on
//   distance    the distance text, and the unit text when it changed
//   clear       panel off, both buffers emptied, SwitchVisibility(5 -> 6) so the navigation's buffer is on show
//               whatever happened before, texts emptied, street off: the state the factory navigation leaves at the
//               end of a route
// A maneuver or a clear is a full batch, a distance a partial one (HudTerminal merges it into a full one still waiting).
// A maneuver is built on what the batches before it leave (which buffer is on show, whether the texts are on), so it
// never takes the place of a waiting batch: while one waits, the maneuver is held and drawn on the next step, from the
// newest view. A clear does not depend on what came before, so it may take a waiting batch's place.
//
// The transport works on its own thread, so a draw returns once the batch is queued; how the cluster took it shows on
// the next call: a failed terminal (no answer, an error answer, a profile mismatch) stops guidance for the session
// like a rejected J29 frame, an offline one like a lost link. Work the terminal dropped (its status left ready) makes
// the next step draw the maneuver again. While the factory navigation uses the terminal, factoryActive() is true and
// the state machine yields; nothing is sent.
//
// Shadow mode (NBT_HUDTERM=log): everything is worked out and each call is logged, nothing is submitted. The terminal
// still watches the bus (and is attached without send rights), so the log shows the factory navigation coming and going.
#ifndef CLUSTER_HUD6WA_DISPLAY_HPP
#define CLUSTER_HUD6WA_DISPLAY_HPP

#include <string>

#include "cluster/display.hpp"
#include "cluster/hud6wa_profile.hpp"
#include "cluster/hud_terminal.hpp"
#include "cluster/hudterm_shapes.hpp"

namespace nbt {
namespace kombinav {

// The three texts the cluster shows for a view: street, distance and unit, as the profile's font can take them.
struct HudTexts {
    std::string street, distance, unit;
};
HudTexts hudTexts(const NavView& v, const hudterm::HudProfile& p);
const char* hudUnitText(int aa_unit);   // "m", "km", "mi", "ft", "yd"; "" when unknown

class Hud6waDisplay : public ClusterDisplay {
public:
    enum { kClearWaitMs = 3000 };

    Hud6waDisplay(hudterm::Port& port, const hudterm::HudProfile& p, bool shadow);

    const char* name() const { return p_.name; }
    void setSettings(const Settings& s) { s_ = s; }
    void setLog(LogFn log, void* user) { log_fn_ = log; log_user_ = user; }

    bool sameManeuver(const NavView& v, double now);
    Result showManeuver(const NavView& v, double now, bool isNew, bool first);
    Result updateDistance(const NavView& v, double now);
    Result keepUp(const NavView& v, double now);
    void guidanceGap() {}
    bool clear(std::string* detail);
    void forget();
    bool sentAny() const { return sent_any_; }
    bool factoryActive();
    bool ready();

    // The batches, built without sending (tests; the driver uses them itself).
    hudterm::Batch showBatch(bool isNew, const HudTexts& t, const hudterm::ShapeSpec& spec) const;
    hudterm::Batch clearBatch() const;
    uint16_t visibleBuffer() const { return visible_; }

private:
    Result health(uint32_t* epoch);
    bool submit(const hudterm::Batch& b, hudterm::SubmitResult* r);
    void log(const char* fmt, ...);

    hudterm::Port& port_;
    hudterm::HudProfile p_;
    bool shadow_;
    Settings s_;
    LogFn log_fn_;
    void* log_user_;
    bool sent_any_;
    bool fresh_;             // the texts' visibility is not known: the next picture switches them on
    bool redraw_;            // the terminal dropped work since our last picture: draw it again
    bool held_;              // a maneuver waits for the transport's queue: draw it on the next step
    uint16_t visible_;       // the buffer on show
    uint32_t epoch_;         // the terminal's epoch when our last picture was queued
    HudTexts shown_;         // the texts last queued
};

} // namespace kombinav
} // namespace nbt

#endif
