// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// The 6WA driver: Android Auto guidance drawn on an F-series cluster's MOST HUDTerminal (cluster/hudterm.hpp) through
// HudTerminal (cluster/hud_terminal.hpp), with the instances of a fixed profile (cluster/hud6wa_profile.hpp) and our
// own pictures (cluster/hudterm_shapes.hpp, cluster/hudterm_junction.hpp).
//
// Two stages per maneuver, like the factory navigation's: from the warning, the maneuver's arrow; from the main prompt
// (or within near_m), a junction view: the maneuver's roads in the picture buffers and the route over them in the
// route overlay, its tail shorter at each of kStages stages on the way in. Lanes, when on, go in a layer of their own.
// Each event is one batch of calls, sent back to back:
//   maneuver    RemoveAllItems(hidden buffer), the picture's items into it (SetFillColor whenever the colour changes),
//               SwitchVisibility(shown -> hidden), RemoveAllItems(old buffer), [both route buffers emptied, for an
//               arrow after a route of ours was in them], [the three texts switched on, the first time], street,
//               distance and unit texts, the panel (frame) on. A junction view is drawn the same way, as a redraw of
//               the maneuver.
//   route       the same double-buffered drawing in the route overlay: its dark edge, then the route
//   lanes       the same in the lane layer, or both lane buffers emptied when the phone's lanes went away (lanes
//               are the phone's, not the maneuver's: they come and go on their own)
//   distance    the distance text, and the unit text when it changed
//   remind      the panel (frame) on again, at the main prompt: the driver may have dismissed it with the stalk
//               button, as the factory navigation's turns can be (what the cluster does with it is a car check)
//   clear       panel off, every buffer emptied, SwitchVisibility(5 -> 6, 7 -> 8, 9 -> 10) so the navigation's buffers
//               are on show whatever happened before, texts emptied, street off: the state the factory navigation
//               leaves at the end of a route
// Each batch belongs to a transport slot (main, route, lanes; a clear to all). A maneuver or a clear is a full batch,
// a distance or a reminder a partial one (HudTerminal merges it into a main batch still waiting). A batch is built on
// what the batches of its slot before it leave (which buffer is on show), so it never takes the place of a waiting one
// of its slot: while one waits, it is held and drawn on a later step, from the newest view. A clear does not depend on
// what came before, so it may take the place of anything waiting. The route goes into the queue after the picture it
// is drawn on, so it never shows over the wrong roads for more than the time between two batches.
//
// If a profile has no route overlay, a junction view's route is drawn into the picture with its roads, and each stage
// redraws them.
//
// The transport works on its own thread, so a draw returns once the batch is queued; how the cluster took it shows on
// the next call: a failed terminal (no answer, an error answer) stops guidance for the session
// like a rejected J29 frame, an offline one like a lost link. Work the terminal dropped (its status left ready) makes
// the next step draw the maneuver again, and its route and lanes. While the factory navigation uses the terminal,
// factoryActive() is true and the state machine yields; nothing is sent.
//
// Shadow mode (NBT_HUDTERM=log): everything is worked out and each call is logged, nothing is submitted. The terminal
// still watches the bus (and is attached without send rights), so the log shows the factory navigation coming and going.
#ifndef CLUSTER_HUD6WA_DISPLAY_HPP
#define CLUSTER_HUD6WA_DISPLAY_HPP

#include <string>
#include <vector>

#include "cluster/display.hpp"
#include "cluster/hud6wa_profile.hpp"
#include "cluster/hud_terminal.hpp"
#include "cluster/hudterm_junction.hpp"
#include "cluster/hudterm_shapes.hpp"

namespace nbt {
namespace kombinav {

// The three texts the cluster shows for a view: street, distance and unit, as the profile's font can take them.
struct HudTexts {
    std::string street, distance, unit;
};
HudTexts hudTexts(const NavView& v, const hudterm::HudProfile& p);
const char* hudUnitText(int aa_unit);   // "m", "km", "mi", "ft", "yd"; "" when unknown

bool sameLanes(const std::vector<AaLane>& a, const std::vector<AaLane>& b);

class Hud6waDisplay : public ClusterDisplay {
public:
    enum { kClearWaitMs = 3000 };
    enum { kStages = 4 };    // route stages on the way into a junction

    Hud6waDisplay(hudterm::Port& port, const hudterm::HudProfile& p, bool shadow);

    const char* name() const { return p_.name; }
    void setSettings(const Settings& s) { s_ = s; }
    void setLog(LogFn log, void* user) { log_fn_ = log; log_user_ = user; }

    bool sameManeuver(const NavView& v, double now);
    Result showManeuver(const NavView& v, double now, bool isNew, bool first);
    Result updateDistance(const NavView& v, double now);
    Result keepUp(const NavView& v, double now);
    Result remind(const NavView& v, double now);
    void guidanceGap() {}
    bool clear(std::string* detail);
    void forget();
    bool sentAny() const { return sent_any_; }
    bool factoryActive();
    bool ready();

    // The batches, built without sending (tests; the driver uses them itself).
    hudterm::Batch showBatch(bool isNew, const HudTexts& t, const hudterm::ShapeSpec& spec) const;
    hudterm::Batch pictureBatch(const char* label, const hudterm::Picture& pic, const HudTexts& t,
                                bool emptyRoute) const;
    hudterm::Batch routeBatch(const hudterm::Picture& pic) const;
    hudterm::Batch lanesBatch(const hudterm::Picture& pic) const;   // an empty picture empties both lane buffers
    hudterm::Batch clearBatch() const;
    uint16_t visibleBuffer() const { return visible_; }
    uint16_t visibleRoute() const { return route_visible_; }
    bool nearShown() const { return near_shown_; }     // the picture queued is the junction view
    int routeStage() const { return stage_shown_; }    // the route stage queued (0 = none)

private:
    Result health(uint32_t* epoch);
    bool submit(const hudterm::Batch& b, hudterm::SubmitResult* r);
    void log(const char* fmt, ...);
    // The junction view: is it due for v, and which route stage is (1..kStages, kStages the longest tail).
    bool nearDue(const NavView& v, double* m) const;
    void startManeuver(const NavView& v);
    void updateStage(const NavView& v);
    bool routeInOverlay() const { return p_.routeA && p_.routeB; }
    bool lanesOn() const { return s_.hud_lanes && p_.laneA && p_.laneB; }
    hudterm::JunctionColours colours() const;
    Result sendRoute();
    Result sendLanes(const NavView& v);

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

    // the junction view of the maneuver shown
    bool has_layout_;        // the maneuver has one (the destination keeps its flag)
    hudterm::Layout layout_;
    bool near_;              // due: from the main prompt (or near_m) until the next maneuver
    bool near_shown_;        // the picture queued is the junction view
    double near_m_;          // metres to the maneuver when it became due (0 = not known yet): the stages divide it
    int stage_;              // the route stage due (0 = none yet)
    int stage_shown_;        // the route stage queued (0 = none)
    uint16_t route_visible_; // the route buffer on show
    bool route_dirty_;       // something of ours may be in the route overlay
    std::vector<AaLane> lanes_shown_;   // the lanes queued
    uint16_t lane_visible_;  // the lane buffer on show
    bool lanes_dirty_;       // something of ours may be in the lane layer
};

} // namespace kombinav
} // namespace nbt

#endif
