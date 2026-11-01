// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// kombi_nav: Android Auto turn-by-turn on the J29 cluster and windscreen HUD, with no iDrive route.
//
// The phone's navigation status (ch 10, via AASession's nav-status callback) is turned into the KOMBI's own
// route-guidance graph, sent from the OMAP straight to the KOMBI (kombi_client.hpp):
//   guidance starts         -> full template graph with maneuver 0 patched (direction, distance + unit, road)
//   distance text changes   -> one in-place distance frame
//   next maneuver           -> full graph again
//   guidance off for 8 s, Android Auto session end, the setting turned off, daemon exit, fatal signal
//                           -> the factory cancel (arrow cleared)
// Off by default. On with the iDrive menu checkbox "Turn arrows in cluster/HUD" (flag file /var/opt/aa/kombi-nav; a
// hand-made /opt/aa/kombi-nav also counts) or NBT_KOMBI_NAV=1; NBT_KOMBI_NAV=0 forces it off. The flag is checked
// once a second, so unticking the box clears the arrow at once.
// Fail closed: a frame the KOMBI rejects stops the feature for the rest of the Android Auto session (wrong car or
// stale instance handle are rejected at the first frame). Factory navigation wins: when an iDrive route starts
// (seen in the system log) kombi_nav stops writing without a cancel, until the next Android Auto route.
#ifndef KOMBI_NAV_HPP
#define KOMBI_NAV_HPP

#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
#include <string>
#include <vector>
#include <pthread.h>
#include <sys/types.h>

#include "kombi_nav_msg.hpp"
#include "kombi_graph.hpp"
#include "kombi_client.hpp"

namespace nbt {
namespace kombinav {

struct Settings {
    double approach_mi;    // show a maneuver only within this many miles (0 = at once); like a production nav HUD
    double hysteresis;     // once shown, the same maneuver hides again only past approach_mi * hysteresis
    size_t dist_chars;     // with approach on: distance text <= 3 chars + the KOMBI's mi/ft
    bool hide_bar;         // red distance bar hidden (pct 255); its percentage means nothing without a factory route
    double grace_s;        // guidance off this long before the clear (rerouting blips survive)
    double first_dist_s;   // a new maneuver without a distance yet waits this long, then shows "-"
    double min_update_s;   // distance updates at most this often
    double nav_stale_s;    // guidance on but no ch 10 message this long = ended
    Settings() : approach_mi(1.0), hysteresis(1.15), dist_chars(3), hide_bar(true), grace_s(8.0), first_dist_s(3.0),
                 min_update_s(0.5), nav_stale_s(300.0) {}
};

// What the state machine drives (callbacks, like AASession's): the real client in kombi_nav.cpp, a test's own in tests.
struct Link {
    KombiClient::Result (*send)(void* user, const std::vector<Frame>& frames, std::string* detail);
    bool (*cancel)(void* user, std::string* detail);   // the factory cancel; true = acknowledged
    void* user;
};

typedef void (*LogFn)(const char* line, void* user);

// The guidance state machine. Single-threaded: the worker calls it. now = seconds (monotonic).
class Core {
public:
    enum Stop { kRunning = 0, kStoppedLink, kStoppedRejected };

    Core(const Graph& g, const Link& link, const Settings& s, LogFn log = NULL, void* log_user = NULL);

    // One tick. v = the next maneuver, or NULL when there is no guidance (or it went stale).
    void step(const NavView* v, double now, const char* why_none = "guidance ended");
    // Clears the arrow if we drew anything.
    void clear(const char* why);
    // An iDrive route is active: forget our state WITHOUT a cancel (the factory graph replaces ours).
    void yieldToFactory();

    bool shown() const { return has_shown_; }
    bool sentAny() const { return sent_any_; }
    bool lastCancelOk() const { return last_cancel_ok_; }   // the last clear's cancel was acknowledged
    Stop stopped() const { return stop_; }
    void resetStop() { stop_ = kRunning; }
    const NavView& current() const { return shown_; }
    const Settings& settings() const { return s_; }
    void setSettings(const Settings& s) { s_ = s; }

private:
    bool approachHold(const NavView& v, double now);
    bool sendFrames(const char* label, const std::vector<Frame>& frames);
    void log(const char* fmt, ...);

    const Graph& g_;
    Link link_;
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
    bool sent_any_;
    bool last_cancel_ok_;
    Stop stop_;
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
class KombiNav {
public:
    KombiNav();
    ~KombiNav();

    // Reads NBT_KOMBI_NAV_* and starts the worker (idle until the setting is on). Call once from main().
    void start();
    // Stops the worker and clears the arrow (one quick attempt). Safe to call twice.
    void stop();

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

    Graph* graph_;
    KombiClient* client_;
    Core* core_;
    static KombiClient::Result linkSend(void* self, const std::vector<Frame>& frames, std::string* detail);
    static bool linkCancel(void* self, std::string* detail);
};

} // namespace kombinav
} // namespace nbt

#endif
