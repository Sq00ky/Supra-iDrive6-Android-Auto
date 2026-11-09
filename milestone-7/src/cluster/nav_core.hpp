// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// kombi_nav: Android Auto turn-by-turn on the J29 cluster and windscreen HUD, with no iDrive route. The same state
// machine drives an F-series 6WA cluster with NBT_CLUSTER=6wa (KombiNav below, cluster/hud6wa_display.hpp); what
// follows describes the J29 path.
//
// The phone's navigation status (ch 10, via AASession's nav-status callback) is turned into the KOMBI's own
// route-guidance graph, sent from the OMAP straight to the KOMBI (cluster/j29_client.hpp):
//   guidance starts         -> full template graph with maneuver 0 patched (direction, distance + unit, road)
//   distance text changes   -> one in-place distance frame
//   next maneuver           -> in-place maneuver-0 update (the factory's update block, no route rebuild); the full
//                              graph after a guidance gap (reroute), a clear, or if the KOMBI rejects the update
//   guidance off for 8 s, Android Auto session end, the setting turned off, daemon exit, fatal signal
//                           -> the factory cancel (arrow cleared)
// Off by default. On with the iDrive menu checkbox "Turn arrows in cluster/HUD" (flag file /var/opt/aa/kombi-nav; a
// hand-made /opt/aa/kombi-nav also counts) or NBT_KOMBI_NAV=1; NBT_KOMBI_NAV=0 forces it off. The flag is checked
// once a second, so unticking the box clears the arrow at once.
// Fail closed: a frame the KOMBI rejects stops the feature for the rest of the Android Auto session (wrong car or
// stale instance handle are rejected at the first frame). Factory navigation wins: when an iDrive route starts
// (seen in the system log) kombi_nav stops writing without a cancel, until the next Android Auto route.
#ifndef CLUSTER_NAV_CORE_HPP
#define CLUSTER_NAV_CORE_HPP

#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
#include <string>
#include <vector>
#include <pthread.h>
#include <sys/types.h>

#include "cluster/display.hpp"
#include "cluster/hud6wa_display.hpp"
#include "cluster/hud_terminal.hpp"
#include "cluster/j29_display.hpp"
#include "cluster/nav_msg.hpp"
#include "cluster/j29_graph.hpp"
#include "cluster/j29_client.hpp"

namespace nbt {
namespace kombinav {

// The guidance state machine, the same for every cluster: what to show and when. How it is drawn is the
// ClusterDisplay's (cluster/display.hpp). Single-threaded: the worker calls it. now = seconds (monotonic).
class Core {
public:
    enum Stop { kRunning = 0, kStoppedLink, kStoppedRejected };

    Core(ClusterDisplay& d, const Settings& s, LogFn log = NULL, void* log_user = NULL);

    // One tick. v = the next maneuver, or NULL when there is no guidance (or it went stale).
    void step(const NavView* v, double now, const char* why_none = "guidance ended");
    // Clears the arrow if we drew anything.
    void clear(const char* why);
    // An iDrive route is active: forget our state WITHOUT a cancel (the factory graph replaces ours).
    void yieldToFactory();

    bool shown() const { return has_shown_; }
    bool sentAny() const { return d_.sentAny(); }
    bool lastCancelOk() const { return last_cancel_ok_; }   // the last clear's cancel was acknowledged
    Stop stopped() const { return stop_; }
    void resetStop() { stop_ = kRunning; }
    const NavView& current() const { return shown_; }
    const Settings& settings() const { return s_; }
    void setSettings(const Settings& s) { s_ = s; d_.setSettings(s); }
    // kApproachTime had to use the distance rule since the last call (a view with no seconds): once per session in
    // the daemon's log, as the sign that a phone does not send them.
    bool takeFallback() { bool f = fallback_; fallback_ = false; return f; }

private:
    enum Stage { kHidden = 0, kWarned, kMain };
    bool approachHold(const NavView& v, double now);
    bool timeHold(const NavView& v, double now);
    bool distanceHold(const NavView& v);
    bool shortDist() const {   // an approach rule is on: the distance text is kept short
        return s_.approach == kApproachTime || (s_.approach == kApproachDistance && s_.approach_mi > 0);
    }
    void fail(ClusterDisplay::Result r);   // stops and clears after a draw went wrong
    void forget();
    void log(const char* fmt, ...);

    ClusterDisplay& d_;
    Settings s_;
    LogFn log_fn_;
    void* log_user_;
    bool has_shown_;
    NavView shown_;
    NavView last_dist_;
    double last_dist_t_;
    bool off_;              // off_since valid
    double off_since_;
    bool no_dist_;          // no_dist_since valid
    double no_dist_since_;
    bool last_cancel_ok_;
    Stop stop_;
    // kApproachTime: the next maneuver as last seen, shown or not, and how far it got.
    bool have_next_;
    NavView next_;
    double next_since_;
    Stage stage_;
    bool remind_;           // the main prompt is due on the maneuver shown
    bool fallback_;
};

// iDrive route detection from the system log (NavNBTEvoController / MapViewer lines, car-seen 2026-10-31):
// "RouteGuidanceStarted" = a route is active; it ends with "disable route: routeId N" for the route shown since that
// start ("NaviRouteVisibleInMap sourceHandle = N"). A START is followed ~1 s later by a disable of the OLD preview
// route, so a bare disable doesn't end it. Lines in the first `settle` seconds after the log opens are its backlog
// dump and are ignored. Pure: lines in, state out.
class FactoryRouteWatch {
public:
    explicit FactoryRouteWatch(double settle_s = 3.0) : settle_(settle_s) { reset(); }
    void reset() { active_ = false; have_first_ = false; route_[0] = 0; }
    void restartSettle() { have_first_ = false; }        // the log was reopened: skip its backlog again
    void line(const char* s, double now);
    bool active() const { return active_; }
    void release() { active_ = false; route_[0] = 0; }   // a new Android Auto route takes over
private:
    double settle_;
    bool active_;
    bool have_first_;
    double t_first_;
    char route_[24];
};

// The daemon-side runtime: worker thread, setting, system-log watch, session hooks. One instance (g_kombiNav).
//
// The cluster driver (cluster/display.hpp) is chosen once at start() by NBT_CLUSTER:
//   j29 (default)  the J29's SOME/IP KOMBI graph
//   6wa            an F-series 6WA's MOST HUDTerminal (cluster/hud6wa_display.hpp), through the MOST client passed to
//                  start(); NBT_HUDTERM=log runs it in shadow mode: worked out and logged, nothing sent
// The 6WA's own transport also sees the factory navigation drawing, and the worker yields to that as it does to an
// iDrive route in the system log.
class KombiNav {
public:
    KombiNav();
    ~KombiNav();

    // Reads NBT_CLUSTER, NBT_HUDTERM and NBT_KOMBI_NAV_*, and starts the worker (idle until the setting is on). Call
    // once from main(), before the MOST client starts: the 6WA driver attaches to it.
    void start(most::Client* most = NULL);
    // Stops the worker and clears the arrow (one quick attempt). Safe to call twice.
    void stop();
    // The 6WA transport's health line, from main's 60 s loop (nothing for the J29).
    void logHealth();
    const char* clusterName() const { return display_ ? display_->name() : "none"; }

    // AASession nav-status callback (session thread): copies the message and returns.
    static void navStatusCallback(uint16_t msg_id, const uint8_t* body, size_t len, void* user);
    // From onSessionEnded (session thread).
    void onSessionEnded();
    // From the fatal-signal handler: async-signal-safe cancel if an arrow may be up.
    static void onFatalSignal();

    // The effective on/off: NBT_KOMBI_NAV if set, else the flag files.
    static bool settingOn();
    // Paths (variables so host tests can point them elsewhere before start()).
    static const char* kFlagFile;     // /var/opt/aa/kombi-nav (iDrive menu checkbox)
    static const char* kManualFile;   // /opt/aa/kombi-nav
    static const char* kShownFile;    // /dev/shmem/nbt-aad-kombi-nav.shown: an arrow may be up (survives a SIGKILL)
    // How `sloginfo -w` is started, tried in this order until one works (the HU's posix_spawn refused all paths on
    // the car, 2026-11-01): posix_spawn with file actions, QNX spawn() with an fd map (QNX only), popen.
    enum WatchMethod { kWatchPosixSpawn = 0, kWatchQnxSpawn = 1, kWatchPopen = 2, kWatchMethods = 3 };
    // Host tests: bit (1 << method) set = that method is treated as failing.
    static unsigned kWatchSkip;
    // The method the running watch was started with, -1 if none.
    int watchMethod() const { return watch_method_; }

private:
    static void* workerStub(void* self);
    void worker();
    void tick(double now);
    void startWatch(double now);
    void stopWatch();
    static void* watchStub(void* self);
    void watchReader(int fd);
    static void logLine(const char* line, void* user);
    static bool quitting(void* self);
    bool enabledNow() const;
    void markShown(bool on, bool force = false);

    pthread_mutex_t mu_;
    NavMessages msgs_;           // guarded by mu_
    double last_msg_t_;          // guarded by mu_
    bool session_ended_;         // guarded by mu_
    bool quit_;                  // guarded by mu_
    bool started_;
    pthread_t thread_;

    Settings settings_;
    uint32_t handle_;
    std::string host_;
    uint16_t port_;
    bool yield_enabled_;
    int forced_;                 // NBT_KOMBI_NAV: -1 unset, 0 off, 1 on

    // worker-thread state
    bool enabled_;
    double t_setting_;
    bool prev_guidance_;
    bool disabled_session_;      // KOMBI rejected a frame: off until the next session
    double backoff_until_;
    int link_failures_;
    bool marked_;
    bool fallback_noted_;        // the "no step seconds" line was printed this session

    // system-log watch
    pthread_mutex_t watch_mu_;
    FactoryRouteWatch watch_;    // guarded by watch_mu_
    pid_t watch_pid_;
    int watch_fd_;
    bool watch_thread_;
    pthread_t watch_tid_;
    double watch_spawned_;
    bool watch_failed_logged_;
    FILE* watch_fp_;             // kWatchPopen: the popen stream (watch_fd_ is its descriptor)
    int watch_method_;
    int watch_method_logged_;

    bool createDisplay(const char* id, most::Client* most);   // the place to add a cluster
    void readApproach();                                      // NBT_KOMBI_NAV_APPROACH* into settings_
    std::string approachText() const;                         // for the start-up line

    Graph* graph_;                   // j29
    KombiClient* client_;            // j29
    hudterm::HudTerminal* terminal_; // 6wa
    bool shadow_;                    // 6wa: NBT_HUDTERM=log
    std::string target_;             // for the log: what the driver talks to
    const char* stopHint_;           // for the log: why a driver may stop for the session
    bool stale_shown_;               // a picture a killed daemon left: cleared once the transport is ready
    ClusterDisplay* display_;
    Core* core_;
    static KombiClient::Result linkSend(void* self, const std::vector<Frame>& frames, std::string* detail);
    static bool linkCancel(void* self, std::string* detail);
};

} // namespace kombinav
} // namespace nbt

#endif
