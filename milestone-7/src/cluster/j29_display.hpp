// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// The J29 driver: Android Auto guidance as the KOMBI's own route-guidance graph over SOME/IP (cluster/j29_graph.hpp,
// cluster/j29_client.hpp).
//   new maneuver          -> in-place maneuver-0 update (the factory's update block, no route rebuild) on a shown
//                            route; the full template graph for the first one, after a guidance gap (reroute), or
//                            once the KOMBI rejected an in-place update (until the next clear)
//   lane row changed      -> the same maneuver drawn again, at most every lane_resend_s
//   distance              -> one in-place distance frame
//   lane row shown        -> a route-position update the KOMBI checks the lanes against, at most every lane_feed_s
//   clear                 -> the factory cancel
#ifndef CLUSTER_J29_DISPLAY_HPP
#define CLUSTER_J29_DISPLAY_HPP

#include <string>
#include <vector>

#include "cluster/display.hpp"
#include "cluster/j29_client.hpp"
#include "cluster/j29_graph.hpp"

namespace nbt {
namespace kombinav {

// How frames reach the KOMBI (callbacks): the daemon's client in cluster/nav_core.cpp, a test's own in tests.
struct Link {
    KombiClient::Result (*send)(void* user, const std::vector<Frame>& frames, std::string* detail);
    bool (*cancel)(void* user, std::string* detail);   // the factory cancel; true = acknowledged
    void* user;
};

class J29Display : public ClusterDisplay {
public:
    J29Display(const Graph& g, const Link& link);

    const char* name() const { return "j29"; }
    void setSettings(const Settings& s) { s_ = s; }
    void setLog(LogFn log, void* user) { log_fn_ = log; log_user_ = user; }

    bool sameManeuver(const NavView& v, double now);
    Result showManeuver(const NavView& v, double now, bool isNew, bool first);
    Result updateDistance(const NavView& v, double now);
    Result keepUp(const NavView& v, double now);
    void guidanceGap() { full_next_ = true; }
    bool clear(std::string* detail);
    void forget();
    bool sentAny() const { return sent_any_; }

private:
    KombiClient::Result sendLogged(const char* label, const std::vector<Frame>& frames, bool quiet);
    Result send(const char* label, const std::vector<Frame>& frames, bool quiet = false);
    Result feed(const NavView& v, double now, bool force);
    void lanesOf(const NavView& v, KombiLanes& out) const;
    void log(const char* fmt, ...);

    const Graph& g_;
    Link link_;
    Settings s_;
    LogFn log_fn_;
    void* log_user_;
    bool sent_any_;
    KombiLanes shown_lanes_;  // the lane row in the graph we sent (empty = none)
    bool full_next_;          // the next maneuver gets the full graph (after a guidance gap: likely a reroute)
    bool inplace_ok_;         // false after the KOMBI rejected an in-place update (full graphs until the next clear)
    double t_graph_;
    double t_pos_;
    bool has_last_pos_;
    double last_pos_;
};

} // namespace kombinav
} // namespace nbt

#endif
