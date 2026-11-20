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
//   idle text (now playing, "Song title in gauge cluster", experimental) -> the KOMBI has no free text field, so the
//                            title goes where a maneuver's road name goes: the full template graph the first time,
//                            in-place maneuver-0 updates when the title changes. Arrow NBT_KOMBI_MEDIA_DIR (KOMBI
//                            direction enum, default 1 = STRAIGHT_ON, car-proven), distance text NBT_KOMBI_MEDIA_DIST
//                            (default empty; the template's own "600" never shows), its unit byte
//                            NBT_KOMBI_MEDIA_UNIT (default: the template's), at most NBT_KOMBI_MEDIA_CHARS characters
//                            (default 40, at most 60). A real maneuver replaces it with a full graph.
//                            Search mode: NBT_KOMBI_MEDIA_CYCLE=dir|unit steps that value from NBT_KOMBI_MEDIA_CYCLE_FROM
//                            to _TO every NBT_KOMBI_MEDIA_CYCLE_S seconds (default 8) with "[d N]" / "[u N]" in front
//                            of the title; a value the KOMBI rejects is logged and skipped, never fatal.
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
    std::string idleText(const std::string& utf8) const;
    Result showIdleText(const std::string& line, double now);
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
    bool idleFrames(int dir, int unit, const std::string& road, bool inplace, std::vector<Frame>& out) const;
    int idle_dir_;            // the idle text's arrow (KOMBI direction enum)
    int idle_unit_;           // the idle distance's unit byte, -1 = the template's
    std::string idle_dist_;   // the idle text's distance text ("" = empty string, not the template's "600")
    size_t idle_chars_;       // the idle text's length limit
    int cycle_;               // 0 off, 1 direction, 2 unit (search mode)
    int cycle_from_, cycle_to_;
    double cycle_s_;
    double cycle_t0_;         // < 0: not started
    int rejected_value_;      // the last cycle value the KOMBI rejected (not resent), -1 = none
    bool idle_up_;            // our idle graph is on the KOMBI (since the last forget())
    std::string idle_line_;   // the line it shows
    int shown_dir_, shown_unit_;
    double idle_t_;           // when it was last sent
};

} // namespace kombinav
} // namespace nbt

#endif
