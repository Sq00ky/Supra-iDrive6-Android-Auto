// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// The swap point between the guidance state machine (cluster/nav_core.hpp) and a cluster: ClusterDisplay draws what
// the state machine decides, in that cluster's own protocol. One driver per protocol: J29Display (SOME/IP KOMBI graph,
// cluster/j29_display.hpp) and Hud6waDisplay (MOST HUDTerminal, cluster/hud6wa_display.hpp). A cluster on the same
// protocol with another layout is a new profile for its driver, not a new driver. KombiNav (cluster/nav_core.hpp)
// picks one per daemon run: createDisplay() is the place to add a cluster.
//
// What stays in the state machine, for every cluster: when guidance counts as gone (grace time), the approach hold,
// a maneuver with no distance yet, the distance text clean-up, the distance update rate, and what is a new maneuver.
// What a driver decides: how a maneuver, a distance and a clear are drawn, what else it shows of a maneuver (the J29's
// lane row), and its own fallbacks. A driver logs what it draws through the log function it is given.
#ifndef CLUSTER_DISPLAY_HPP
#define CLUSTER_DISPLAY_HPP

#include <stddef.h>
#include <string>

#include "cluster/nav_msg.hpp"

namespace nbt {
namespace kombinav {

// When a maneuver is shown (the approach rule):
//   kApproachTime      like the factory navigation, which draws a turn on the cluster at its first announcement and
//                      again at the main one: shown once the phone's seconds to it are <= warn_s, shown again (the
//                      driver's remind(), e.g. after the driver dismissed it) at <= main_s. Off-ramps and forks are
//                      also due by distance (mway_warn_m, mway_main_m), so a motorway exit in a queue is still warned
//                      early enough to change lanes. Once shown it stays until the next maneuver. A view with no
//                      seconds (and no motorway distance) falls back to kApproachDistance.
//   kApproachDistance  shown within approach_mi miles, hidden again past approach_mi * hysteresis
//   kApproachOff       shown at once
enum Approach { kApproachOff = 0, kApproachDistance, kApproachTime };
const char* approachName(Approach a);

struct Settings {
    Approach approach;
    double warn_s;         // kApproachTime: the warning, seconds to the maneuver
    double main_s;         // kApproachTime: the main prompt (0 = none), below warn_s
    double mway_warn_m;    // kApproachTime: off-ramps and forks are also warned within this many metres
    double mway_main_m;    // ... and get their main prompt within this many
    double approach_mi;    // kApproachDistance: show a maneuver only within this many miles (0 = at once)
    double hysteresis;     // once shown, the same maneuver hides again only past approach_mi * hysteresis
    size_t dist_chars;     // with an approach rule on: distance text <= 3 chars + the KOMBI's mi/ft
    bool hide_bar;         // red distance bar hidden (pct 255); its percentage means nothing without a factory route
    double grace_s;        // guidance off this long before the clear (rerouting blips survive)
    double first_dist_s;   // a new maneuver without a distance yet waits this long, then shows "-"
    double min_update_s;   // distance updates at most this often
    double nav_stale_s;    // guidance on but no ch 10 message this long = ended
    bool lanes;            // the phone's lanes as the KOMBI lane row (NBT_KOMBI_NAV_LANES=0 turns it off)
    double lane_resend_s;  // a lane-only change on the same maneuver resends the graph at most this often
    double lane_feed_s;    // the position update the KOMBI checks lanes against: at most this often
    int inplace;           // next maneuver / lane change on a shown route: 1 = in-place maneuver-0 update (default),
                           // 2 = the same without the 0x07 guiding model, 0 = full graph (NBT_KOMBI_NAV_INPLACE)
    bool junction;         // 6WA: a junction view (roads and route) in place of the arrow near a maneuver
                           // (NBT_HUD_JUNCTION=0 turns it off)
    double near_m;         // 6WA: the junction view comes in at the main prompt (kApproachTime), or this close
    bool hud_lanes;        // 6WA: the phone's lanes in their own layer (NBT_HUD_LANES=1; off until their place is known)
    Settings() : approach(kApproachTime), warn_s(50), main_s(20), mway_warn_m(2000), mway_main_m(1000),
                 approach_mi(1.0), hysteresis(1.15), dist_chars(3), hide_bar(true), grace_s(8.0), first_dist_s(3.0),
                 min_update_s(0.5), nav_stale_s(300.0), lanes(true), lane_resend_s(3.0), lane_feed_s(1.0), inplace(1),
                 junction(true), near_m(300), hud_lanes(false) {}
};

typedef void (*LogFn)(const char* line, void* user);

class ClusterDisplay {
public:
    // kRejected: the cluster refused what we sent. kLink: it could not be reached. kInternal: our own data for it is
    // wrong (a template with an unexpected layout). Each stops guidance on the cluster and clears it.
    enum Result { kOk = 0, kLink, kRejected, kInternal };

    virtual ~ClusterDisplay() {}

    virtual const char* name() const = 0;
    virtual void setSettings(const Settings& s) = 0;
    virtual void setLog(LogFn log, void* user) = 0;

    // The maneuver shown carries on (same type, road and exit). True when something else this cluster shows of it has
    // changed and the maneuver is to be drawn again (the J29: its lane row).
    virtual bool sameManeuver(const NavView& v, double now) = 0;
    // Draws a maneuver with its distance and road: a new one (isNew), or the one shown again. first: nothing of ours
    // is up. v's distance text is already cleaned up and truncated.
    virtual Result showManeuver(const NavView& v, double now, bool isNew, bool first) = 0;
    // The distance changed on the maneuver shown.
    virtual Result updateDistance(const NavView& v, double now) = 0;
    // Each step on a maneuver shown that was not drawn again: whatever the cluster needs kept up (the J29: the
    // position its lane row is checked against).
    virtual Result keepUp(const NavView& v, double now) = 0;
    // The maneuver shown reached its main prompt (kApproachTime): show it again if the cluster can hide it by
    // itself (the 6WA: the driver can dismiss the panel with the stalk button). Nothing by default.
    virtual Result remind(const NavView& v, double now) { (void)v; (void)now; return kOk; }
    // Guidance went away while a maneuver is shown: a gap, likely a reroute.
    virtual void guidanceGap() = 0;
    // Removes what we drew. True when the cluster confirmed it; detail says why not.
    virtual bool clear(std::string* detail) = 0;
    // Drops every drawing state: after a clear, or when the factory navigation took the cluster.
    virtual void forget() = 0;
    // Something was sent since the last forget(), even if it failed, so a clear is due.
    virtual bool sentAny() const = 0;
    // The factory navigation is using the cluster, as this driver's own transport sees it: the state machine yields as
    // it does for an iDrive route seen in the system log. The J29's transport has no such view.
    virtual bool factoryActive() { return false; }
    // The transport could take a clear now: the clear for a picture a killed daemon left waits for this.
    virtual bool ready() { return true; }
};

} // namespace kombinav
} // namespace nbt

#endif
