// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "kombi_nav.hpp"
#include "nbt_env.hpp"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>

extern char** environ;

namespace nbt {
namespace kombinav {

namespace {

const size_t kMaxRoad = 40;   // characters; the cluster shows far fewer
const size_t kMaxDist = 8;
const double kTickS = 0.25;
const double kLinkBackoffS = 30.0;
const int kLinkGiveUp = 5;    // failed sends in a row without ever drawing: no KOMBI there (not a J29?)
const double kExitCancelRetryS = 0.0;
const double kCancelRetryS = 60.0;

double monoS() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

volatile int g_mayShow = 0;   // an arrow may be up: the fatal-signal handler sends the cancel

} // namespace

// ---- Core ---------------------------------------------------------------------------------------------------------

Core::Core(const Graph& g, const Link& link, const Settings& s, LogFn log, void* log_user)
    : g_(g), link_(link), s_(s), log_fn_(log), log_user_(log_user), has_shown_(false), last_dist_t_(0), off_(false),
      off_since_(0), no_dist_(false), no_dist_since_(0), sent_any_(false), last_cancel_ok_(true), stop_(kRunning),
      full_next_(false), inplace_ok_(true), t_graph_(0), t_pos_(0), has_last_pos_(false), last_pos_(0) {}

void Core::log(const char* fmt, ...) {
    char buf[320];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (log_fn_) log_fn_(buf, log_user_);
}

KombiClient::Result Core::sendLogged(const char* label, const std::vector<Frame>& frames, bool quiet) {
    std::string detail;
    KombiClient::Result r = link_.send(link_.user, frames, &detail);
    sent_any_ = true;
    if (!quiet || r != KombiClient::kOk)
        log("%s: %u frame(s) %s%s%s", label, (unsigned)frames.size(), r == KombiClient::kOk ? "OK" : "FAILED",
            detail.empty() || r == KombiClient::kOk ? "" : ": ", r == KombiClient::kOk ? "" : detail.c_str());
    return r;
}

bool Core::sendFrames(const char* label, const std::vector<Frame>& frames, bool quiet) {
    KombiClient::Result r = sendLogged(label, frames, quiet);
    if (r == KombiClient::kOk) return true;
    stop_ = r == KombiClient::kRejected ? kStoppedRejected : kStoppedLink;
    clear(r == KombiClient::kRejected ? "KOMBI rejected a frame" : "KOMBI link failed");
    return false;
}

bool Core::approachHold(const NavView& v, double /*now*/) {
    if (s_.approach_mi <= 0) return false;
    double m;
    if (!distanceMetres(v, m)) return false;           // unknown distance: show as before (fail open)
    bool same = has_shown_ && v.sameManeuver(shown_);  // hysteresis: same maneuver only
    double lim = s_.approach_mi * 1609.344 * (same ? s_.hysteresis : 1.0);
    if (m <= lim) return false;
    if (has_shown_) {
        log("APPROACH: next maneuver %.1f mi away (> %g mi): hidden", m / 1609.344, s_.approach_mi);
        clear("approach");
    }
    no_dist_ = false;
    return true;
}

void Core::step(const NavView* in, double now, const char* why_none) {
    if (stop_ != kRunning) return;
    if (!in) {
        no_dist_ = false;
        if (!has_shown_) return;
        full_next_ = true;                             // guidance gap (a reroute): the next maneuver gets a full graph
        if (!off_) { off_ = true; off_since_ = now; }
        if (now - off_since_ >= s_.grace_s) clear(why_none);
        return;
    }
    off_ = false;
    NavView v = *in;
    if (approachHold(v, now)) return;
    // A snapshot without a distance (0x8006 before its 0x8007): keep the last one rather than flicker "-".
    if (v.dist_text.empty() && has_shown_ && !shown_.dist_text.empty() && shown_.dist_text != "-") {
        v.dist_text = shown_.dist_text;
        v.aa_unit = shown_.aa_unit;
        v.has_step_m = shown_.has_step_m;
        v.step_m = shown_.step_m;
    }
    if (v.dist_text.empty()) {                         // never show the template's own "600"
        if (!no_dist_) { no_dist_ = true; no_dist_since_ = now; }
        if (!has_shown_ && now - no_dist_since_ < s_.first_dist_s) return;
        v.dist_text = "-";
    } else {
        no_dist_ = false;
    }
    std::string text;
    int unit;
    if (kombiUnit(v.aa_unit) < 0 && v.has_step_m && v.dist_text != "-") {   // unknown display unit: mi / ft
        mapsDisplay((double)v.step_m, text, unit);
        v.dist_text = text;
        v.aa_unit = unit;
    }
    if (s_.approach_mi > 0 && v.dist_text != "-") {
        shortDistance(v, s_.dist_chars, text, unit);
        v.dist_text = text;
        v.aa_unit = unit;
    }
    v.road = utf8Prefix(v.road.empty() ? std::string(" ") : v.road, kMaxRoad);
    v.dist_text = utf8Prefix(v.dist_text, kMaxDist);
    int dir = kombiDirection(v.aa_type), ku = kombiUnit(v.aa_unit);
    std::vector<Frame> frames;
    KombiLanes lanes;
    if (s_.lanes) kombiLanes(v, lanes);                  // empty = no lane row (none sent, or nothing we can map)
    bool new_man = !has_shown_ || !v.sameManeuver(shown_);
    if (!new_man) full_next_ = false;                    // guidance came back on the same maneuver: nothing to rebuild
    // the phone adds / re-highlights / drops lanes as the junction nears: the same maneuver's graph is resent with the
    // new lane row (the car-proven path), at most every lane_resend_s
    bool lanes_changed = !new_man && lanes != shown_lanes_ && now - t_graph_ >= s_.lane_resend_s;
    if (new_man || lanes_changed) {
        std::string lane_note;
        if (!lanes.empty() || lanes_changed) lane_note = " | lanes " + describeLanes(lanes);
        log("%s type %d -> direction %d | %s %s | road '%s'%s", new_man ? "SHOW" : "LANES", v.aa_type, dir,
            v.dist_text.c_str(), kombiUnitName(ku), v.road.c_str(),
            lane_note.c_str());
        const KombiLanes* lp = lanes.empty() ? NULL : &lanes;
        bool done = false;
        if (has_shown_ && !full_next_ && inplace_ok_ && s_.inplace > 0 &&
            g_.maneuver(dir, v.road, v.dist_text, ku, frames, lp, s_.inplace != 2)) {
            KombiClient::Result r = sendLogged("maneuver", frames, false);
            if (r == KombiClient::kOk) {
                done = true;
            } else if (r == KombiClient::kRejected) {
                log("the KOMBI rejected the in-place update: full graphs until the next clear");
                inplace_ok_ = false;
            } else {
                stop_ = kStoppedLink;
                clear("KOMBI link failed");
                return;
            }
        }
        if (!done) {
            if (!g_.start(dir, v.road, v.dist_text, ku, frames, lp)) {
                log("internal error: the graph template has an unexpected layout");
                stop_ = kStoppedRejected;
                clear("template layout");
                return;
            }
            if (!sendFrames("graph", frames)) return;
        }
        full_next_ = false;
        has_shown_ = true;
        shown_ = v;
        last_dist_ = v;
        last_dist_t_ = now;
        shown_lanes_ = lanes;
        t_graph_ = now;
        has_last_pos_ = false;
        if (!lanes.empty()) feed(v, now, true);           // the KOMBI checks lanes on the next position update
        return;
    }
    if (!v.sameDistance(last_dist_) && now - last_dist_t_ >= s_.min_update_s) {
        if (!g_.distance(v.dist_text, ku, frames)) {
            log("internal error: the distance template has an unexpected layout");
            stop_ = kStoppedRejected;
            clear("template layout");
            return;
        }
        log("DIST %s %s", v.dist_text.c_str(), (ku < 0 ? "" : kombiUnitName(ku)));
        if (!sendFrames("distance", frames)) return;
        last_dist_ = v;
        last_dist_t_ = now;
    }
    if (!shown_lanes_.empty()) {
        feed(v, now, false);
        if (stop_ != kRunning) return;
    }
    shown_ = v;
}

// The lane row's position feed: the car's route position from the metres to the maneuver (one 0x11 update, at most
// every lane_feed_s and only when it moved >= 1 m), so the KOMBI's lane-section check sees the car inside our section.
void Core::feed(const NavView& v, double now, bool force) {
    if (!force && now - t_pos_ < s_.lane_feed_s) return;
    double m = 0, pos = 0;
    bool has = distanceMetres(v, m);
    if (!g_.feedPosition(has, m, pos)) return;
    if (!force && has_last_pos_ && pos - last_pos_ < 1.0 && last_pos_ - pos < 1.0) return;
    std::vector<Frame> frames;
    if (!g_.position(pos, frames)) {
        log("internal error: the graph template has no destination info: lane row without position updates");
        return;
    }
    if (!sendFrames("position", frames, true)) return;
    t_pos_ = now;
    has_last_pos_ = true;
    last_pos_ = pos;
}

void Core::forget() {
    has_shown_ = false;
    shown_ = NavView();
    last_dist_ = NavView();
    off_ = false;
    no_dist_ = false;
    sent_any_ = false;
    shown_lanes_.clear();
    has_last_pos_ = false;
    full_next_ = false;
    inplace_ok_ = true;
}

void Core::clear(const char* why) {
    log("CLEAR (%s)", why);
    if (sent_any_ || has_shown_) {
        std::string detail;
        bool ok = link_.cancel(link_.user, &detail);
        log("cancel: %s%s%s", ok ? "OK" : "FAILED", ok ? "" : ": ", ok ? "" : detail.c_str());
        last_cancel_ok_ = ok;
    }
    forget();
}

void Core::yieldToFactory() {
    if (has_shown_ || sent_any_) log("YIELD (iDrive route active): stop writing, no cancel");
    forget();
}

// ---- FactoryRouteWatch --------------------------------------------------------------------------------------------

void FactoryRouteWatch::line(const char* s, double now) {
    if (!have_first_) { have_first_ = true; t_first_ = now; }
    if (now - t_first_ < settle_) return;             // the log's backlog dump, not live events
    if (strstr(s, "RouteGuidanceStarted")) {
        active_ = true;
        route_[0] = 0;
        return;
    }
    const char* p = strstr(s, "NaviRouteVisibleInMap sourceHandle = ");
    if (p && active_) {
        p += strlen("NaviRouteVisibleInMap sourceHandle = ");
        size_t n = 0;
        while (p[n] >= '0' && p[n] <= '9' && n + 1 < sizeof(route_)) ++n;
        if (n) { memcpy(route_, p, n); route_[n] = 0; }
        return;
    }
    p = strstr(s, "disable route: routeId ");
    if (p && active_ && route_[0]) {
        p += strlen("disable route: routeId ");
        size_t n = strlen(route_);
        if (!strncmp(p, route_, n) && !(p[n] >= '0' && p[n] <= '9')) {
            active_ = false;
            route_[0] = 0;
        }
    }
}

// ---- KombiNav -----------------------------------------------------------------------------------------------------

const char* KombiNav::kFlagFile = "/var/opt/aa/kombi-nav";
const char* KombiNav::kManualFile = "/opt/aa/kombi-nav";
const char* KombiNav::kShownFile = "/dev/shmem/nbt-aad-kombi-nav.shown";
unsigned KombiNav::kWatchSkip = 0;

KombiNav::KombiNav()
    : last_msg_t_(0), session_ended_(false), quit_(false), started_(false), handle_(0xC8), host_("160.48.199.96"),
      port_(30502), yield_enabled_(true), forced_(-1), enabled_(false), t_setting_(-1e9), prev_guidance_(false),
      disabled_session_(false), backoff_until_(0), link_failures_(0), marked_(false), watch_pid_(-1), watch_fd_(-1),
      watch_thread_(false), watch_spawned_(-1e9), watch_failed_logged_(false), watch_fp_(NULL),
      watch_method_(-1), watch_method_logged_(-1), graph_(NULL), client_(NULL),
      core_(NULL) {
    pthread_mutex_init(&mu_, NULL);
    pthread_mutex_init(&watch_mu_, NULL);
}

KombiNav::~KombiNav() {
    stop();
    delete core_;
    delete client_;
    delete graph_;
}

bool KombiNav::settingOn() {
    int f = envFlag("NBT_KOMBI_NAV");
    if (f >= 0) return f == 1;
    return access(kFlagFile, F_OK) == 0 || access(kManualFile, F_OK) == 0;
}

bool KombiNav::quitting(void* self) {
    KombiNav* k = static_cast<KombiNav*>(self);
    pthread_mutex_lock(&k->mu_);
    bool q = k->quit_;
    pthread_mutex_unlock(&k->mu_);
    return q;
}

KombiClient::Result KombiNav::linkSend(void* self, const std::vector<Frame>& frames, std::string* detail) {
    return static_cast<KombiNav*>(self)->client_->send(frames, detail);
}

// The cancel retries through a link blip (up to 60 s), unless we're quitting (one attempt).
bool KombiNav::linkCancel(void* self, std::string* detail) {
    KombiNav* k = static_cast<KombiNav*>(self);
    return k->client_->cancel(*k->graph_, quitting(k) ? kExitCancelRetryS : kCancelRetryS, 5.0, &KombiNav::quitting, k,
                              detail);
}

bool KombiNav::enabledNow() const {
    if (forced_ >= 0) return forced_ == 1;
    return access(kFlagFile, F_OK) == 0 || access(kManualFile, F_OK) == 0;
}

void KombiNav::logLine(const char* line, void* /*user*/) {
    printf("[KombiNav] %s\n", line);
}

void KombiNav::start() {
    if (started_) return;
    forced_ = envFlag("NBT_KOMBI_NAV");
    const char* e = getenv("NBT_KOMBI_NAV_HANDLE");
    if (e && *e) handle_ = (uint32_t)strtoul(e, NULL, 0);
    e = getenv("NBT_KOMBI_NAV_HOST");
    if (e && *e) host_ = e;
    e = getenv("NBT_KOMBI_NAV_PORT");
    if (e && *e) port_ = (uint16_t)atoi(e);
    e = getenv("NBT_KOMBI_NAV_APPROACH_MI");
    if (e && *e) {
        double a = atof(e);
        if (a == 0 || (a >= 0.1 && a <= 9.9)) settings_.approach_mi = a;
        else printf("[KombiNav] NBT_KOMBI_NAV_APPROACH_MI=%s out of range (0 or 0.1..9.9): keeping %g\n", e,
                    settings_.approach_mi);
    }
    if (envFlag("NBT_KOMBI_NAV_SHOW_BAR") == 1) settings_.hide_bar = false;
    if (envFlag("NBT_KOMBI_NAV_YIELD") == 0) yield_enabled_ = false;
    if (envFlag("NBT_KOMBI_NAV_LANES") == 0) settings_.lanes = false;
    e = getenv("NBT_KOMBI_NAV_INPLACE");
    if (e && *e && e[0] >= '0' && e[0] <= '2' && !e[1]) settings_.inplace = e[0] - '0';
    graph_ = new Graph(handle_, settings_.hide_bar ? 255 : -1);
    client_ = new KombiClient(host_, port_);
    client_->prepareEmergency(*graph_);
    Link link;
    link.send = &KombiNav::linkSend;
    link.cancel = &KombiNav::linkCancel;
    link.user = this;
    core_ = new Core(*graph_, link, settings_, &KombiNav::logLine, this);
    printf("[KombiNav] cluster/HUD arrows %s (%s); KOMBI %s:%u handle 0x%X, approach %g mi, bar %s, iDrive-route "
           "yield %s, lanes %s, in-place %d\n", enabledNow() ? "ON" : "off",
           forced_ >= 0 ? "NBT_KOMBI_NAV" : "iDrive menu / flag file", host_.c_str(), (unsigned)port_, handle_,
           settings_.approach_mi, settings_.hide_bar ? "hidden" : "factory", yield_enabled_ ? "on" : "off",
           settings_.lanes ? "on" : "off", settings_.inplace);
    if (pthread_create(&thread_, NULL, &KombiNav::workerStub, this) != 0) {
        printf("[KombiNav] cannot start the worker thread: feature disabled\n");
        return;
    }
    started_ = true;
}

void KombiNav::stop() {
    if (!started_) return;
    pthread_mutex_lock(&mu_);
    quit_ = true;
    pthread_mutex_unlock(&mu_);
    pthread_join(thread_, NULL);
    started_ = false;
}

void KombiNav::navStatusCallback(uint16_t msg_id, const uint8_t* body, size_t len, void* user) {
    KombiNav* self = static_cast<KombiNav*>(user);
    if (!self) return;
    pthread_mutex_lock(&self->mu_);
    if (self->msgs_.store(msg_id, body ? body : (const uint8_t*)"", body ? len : 0)) self->last_msg_t_ = monoS();
    pthread_mutex_unlock(&self->mu_);
}

void KombiNav::onSessionEnded() {
    pthread_mutex_lock(&mu_);
    msgs_.clear();
    session_ended_ = true;
    pthread_mutex_unlock(&mu_);
}

void KombiNav::onFatalSignal() {
    if (g_mayShow) KombiClient::emergencyCancel();
}

// Marker file + flag for the fatal-signal handler while an arrow may be up. It is removed only after an acknowledged
// cancel (or a yield: the factory graph owns the models then), so the next start retries a cancel that failed.
void KombiNav::markShown(bool on, bool force) {
    if (on == marked_) return;
    if (on) {
        int fd = open(kShownFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) close(fd);
        g_mayShow = 1;
        marked_ = true;
    } else if (force || core_->lastCancelOk()) {
        unlink(kShownFile);
        g_mayShow = 0;
        marked_ = false;
    }
}

void* KombiNav::workerStub(void* self) {
    static_cast<KombiNav*>(self)->worker();
    return NULL;
}

void KombiNav::worker() {
    // An arrow left up by a killed daemon (SIGKILL after Force Stop, power loss of the HU only): clear it once.
    if (access(kShownFile, F_OK) == 0) {
        std::string detail;
        bool ok = client_->cancel(*graph_, 0.0, 5.0, &KombiNav::quitting, this, &detail);
        printf("[KombiNav] arrow possibly left by the previous run: cancel %s%s%s\n", ok ? "OK" : "FAILED",
               ok ? "" : ": ", ok ? "" : detail.c_str());
        if (ok) unlink(kShownFile);
        else g_mayShow = 1;   // the file stays: the crash handler and the next start try again
    }
    while (!quitting(this)) {
        tick(monoS());
        for (double t = 0; t < kTickS && !quitting(this); t += 0.05) usleep(50000);
    }
    client_->setTimeoutMs(1500);   // Force Stop sends SIGKILL 10 s after 'quit': keep the exit clear short
    if (core_->shown() || core_->sentAny()) core_->clear("nbt-aad exiting");
    markShown(false);
    stopWatch();
}

void KombiNav::tick(double now) {
    if (now - t_setting_ >= 1.0) {
        t_setting_ = now;
        bool on = enabledNow();
        if (on != enabled_) {
            printf("[KombiNav] cluster/HUD arrows turned %s\n", on ? "ON" : "off");
            enabled_ = on;
            if (!on) {
                if (core_->shown() || core_->sentAny()) core_->clear("turned off");
                stopWatch();
            } else {
                pthread_mutex_lock(&watch_mu_);
                watch_.reset();
                pthread_mutex_unlock(&watch_mu_);
            }
        }
    }
    markShown(core_->shown() || core_->sentAny());
    if (!enabled_) return;
    if (yield_enabled_) startWatch(now);

    NavMessages m;
    double last_msg;
    bool ended;
    pthread_mutex_lock(&mu_);
    m = msgs_;
    last_msg = last_msg_t_;
    ended = session_ended_;
    session_ended_ = false;
    pthread_mutex_unlock(&mu_);

    if (ended) {
        if (core_->shown() || core_->sentAny()) core_->clear("Android Auto session ended");
        core_->resetStop();
        disabled_session_ = false;
        backoff_until_ = 0;
        link_failures_ = 0;
        prev_guidance_ = false;
        markShown(core_->shown() || core_->sentAny());
        return;
    }
    if (disabled_session_ || now < backoff_until_) return;

    bool guidance = m.guidanceActive();
    bool factory = false;
    if (yield_enabled_) {
        pthread_mutex_lock(&watch_mu_);
        factory = watch_.active();
        if (factory && guidance && m.has_status && !prev_guidance_) {
            watch_.release();
            factory = false;
            printf("[KombiNav] iDrive-route yield released: new Android Auto route\n");
        }
        pthread_mutex_unlock(&watch_mu_);
    }
    if (m.has_status) prev_guidance_ = guidance;
    if (factory) {
        core_->yieldToFactory();
        markShown(false, true);
        return;
    }
    NavView v;
    const NavView* pv = NULL;
    const char* why = "guidance ended";
    if (guidance && viewFromMessages(m, v)) {
        if (now - last_msg > settings_.nav_stale_s) why = "navigation status frozen";
        else pv = &v;
    }
    core_->step(pv, now, why);
    if (core_->shown()) link_failures_ = 0;
    if (core_->stopped() == Core::kStoppedRejected) {
        printf("[KombiNav] the KOMBI rejected a frame: arrows off until the next Android Auto session "
               "(not a J29, or a changed instance handle: NBT_KOMBI_NAV_HANDLE)\n");
        disabled_session_ = true;
        core_->resetStop();
    } else if (core_->stopped() == Core::kStoppedLink) {
        core_->resetStop();
        if (++link_failures_ >= kLinkGiveUp) {
            printf("[KombiNav] no KOMBI at %s:%u after %d tries: arrows off until the next Android Auto session\n",
                   host_.c_str(), (unsigned)port_, link_failures_);
            disabled_session_ = true;
        } else {
            printf("[KombiNav] KOMBI link failed: retrying in %.0f s\n", kLinkBackoffS);
            backoff_until_ = now + kLinkBackoffS;
        }
    }
    markShown(core_->shown() || core_->sentAny());
}

// ---- system-log watch (iDrive route yield) -------------------------------------------------------------------------

namespace {

const char* const kWatchMethodName[KombiNav::kWatchMethods] = { "posix_spawn", "spawn", "popen" };

void addError(std::string& why, const char* what, int err) {
    if (!why.empty()) why += ", ";
    why += what;
    why += ": ";
    why += strerror(err);
}

// posix_spawn with file actions: stdout = the pipe's write end, stderr = /dev/null, the read end closed. Every path's
// error is kept (the first car build reported only the last one).
pid_t spawnPosix(int rd, int wr, char* const* argv, char* const* envp, std::string& why) {
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, wr, 1);
    posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addclose(&fa, rd);
    posix_spawn_file_actions_addclose(&fa, wr);
    static const char* const kPaths[] = { "sloginfo", "/usr/bin/sloginfo", "/bin/sloginfo" };
    pid_t pid = -1;
    for (size_t i = 0; i < sizeof(kPaths) / sizeof(kPaths[0]); ++i) {
        int rc = i == 0 ? posix_spawnp(&pid, kPaths[i], &fa, NULL, argv, envp)
                        : posix_spawn(&pid, kPaths[i], &fa, NULL, argv, envp);
        if (rc == 0) break;
        if (rc == -1) rc = errno;     // some libcs return -1 + errno instead of the error number
        addError(why, kPaths[i], rc);
        pid = -1;
    }
    posix_spawn_file_actions_destroy(&fa);
    return pid;
}

#ifdef __QNX__
// QNX spawn() with an fd map, the call the HU's own popen() is built on: the child gets exactly stdin closed,
// stdout = the pipe, stderr = /dev/null (fds not in the map are not inherited).
pid_t spawnQnx(int wr, char* const* argv, char* const* envp, std::string& why) {
    int nul = open("/dev/null", O_WRONLY);
    int map[3] = { SPAWN_FDCLOSED, wr, nul >= 0 ? nul : SPAWN_FDCLOSED };
    struct inheritance inh;
    memset(&inh, 0, sizeof(inh));
    static const char* const kPaths[] = { "/usr/bin/sloginfo", "/bin/sloginfo" };
    pid_t pid = -1;
    for (size_t i = 0; i < sizeof(kPaths) / sizeof(kPaths[0]) && pid == -1; ++i) {
        pid = spawn(kPaths[i], 3, map, &inh, argv, envp);
        if (pid == -1) addError(why, kPaths[i], errno);
    }
    if (nul >= 0) close(nul);
    return pid;
}
#endif

// popen through the shell: it prints its pid, then becomes sloginfo (exec keeps the pid) so stopWatch can signal it.
// The pid line is read straight from the descriptor, so no stdio buffer holds back later lines.
pid_t spawnPopen(FILE** fp_out, std::string& why) {
    FILE* fp = popen("unset LD_PRELOAD; echo $$; exec sloginfo -w 2>/dev/null", "r");
    if (!fp) {
        addError(why, "popen", errno ? errno : ENOMEM);
        return -1;
    }
    int fd = fileno(fp);
    char line[24];
    size_t n = 0;
    while (n < sizeof(line) - 1) {
        ssize_t r = read(fd, line + n, 1);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0 || line[n] == '\n') break;
        ++n;
    }
    line[n] = 0;
    pid_t pid = (pid_t)atol(line);
    if (pid <= 0) {
        addError(why, "popen (no sloginfo)", EIO);
        pclose(fp);              // the shell has exited (no pid line), so this does not block
        return -1;
    }
    *fp_out = fp;
    return pid;
}

} // namespace

void KombiNav::startWatch(double now) {
    if (watch_pid_ > 0) {
        int st;
        if (waitpid(watch_pid_, &st, WNOHANG) != watch_pid_) return;   // still running
        if (watch_thread_) pthread_join(watch_tid_, NULL);
        watch_thread_ = false;
        if (watch_fp_) pclose(watch_fp_);                               // already reaped: returns at once
        else if (watch_fd_ >= 0) close(watch_fd_);
        watch_fp_ = NULL;
        watch_fd_ = -1;
        watch_pid_ = -1;
        watch_method_ = -1;
        printf("[KombiNav] sloginfo ended: restarting the iDrive-route watch\n");
    }
    if (now - watch_spawned_ < 10.0) return;
    watch_spawned_ = now;
    // The child gets our environment minus LD_PRELOAD (the HMI's graphics preload is no use to sloginfo).
    std::vector<char*> env;
    for (char** e = environ; e && *e; ++e)
        if (strncmp(*e, "LD_PRELOAD=", 11) != 0) env.push_back(*e);
    env.push_back(NULL);
    char a0[] = "sloginfo", a1[] = "-w";
    char* argv[] = { a0, a1, NULL };
    std::string why;
    pid_t pid = -1;
    int fd = -1, method = -1;
    FILE* fp = NULL;
    for (int m = 0; m < kWatchMethods && pid <= 0; ++m) {
        std::string err;
        if (kWatchSkip & (1u << m)) {
            err = "skipped (test)";
        } else if (m == kWatchPopen) {
            pid = spawnPopen(&fp, err);
            if (pid > 0) fd = fileno(fp);
        } else {
            int p[2];
            if (pipe(p) != 0) { addError(err, "pipe", errno); }
            else {
                fcntl(p[0], F_SETFD, FD_CLOEXEC);   // other children (popen / system elsewhere) must not keep it open
                fcntl(p[1], F_SETFD, FD_CLOEXEC);
                if (m == kWatchPosixSpawn) pid = spawnPosix(p[0], p[1], argv, &env[0], err);
#ifdef __QNX__
                else pid = spawnQnx(p[1], argv, &env[0], err);
#else
                else err = "not QNX";
#endif
                close(p[1]);
                if (pid > 0) fd = p[0];
                else close(p[0]);
            }
        }
        if (pid > 0) method = m;
        else { if (!why.empty()) why += "; "; why += std::string(kWatchMethodName[m]) + " (" + err + ")"; }
    }
    if (pid <= 0) {
        if (!watch_failed_logged_)
            printf("[KombiNav] cannot start sloginfo [%s]: no iDrive-route yield, don't start an iDrive route while "
                   "Android Auto guides\n", why.c_str());
        watch_failed_logged_ = true;
        return;
    }
    if (method != watch_method_logged_) {
        printf("[KombiNav] iDrive-route watch: sloginfo -w via %s (pid %d)%s%s\n", kWatchMethodName[method], (int)pid,
               why.empty() ? "" : "; failed first: ", why.c_str());
        watch_method_logged_ = method;
    }
    watch_failed_logged_ = false;
    watch_pid_ = pid;
    watch_fd_ = fd;
    watch_fp_ = fp;
    watch_method_ = method;
    pthread_mutex_lock(&watch_mu_);
    watch_.restartSettle();
    pthread_mutex_unlock(&watch_mu_);
    watch_thread_ = pthread_create(&watch_tid_, NULL, &KombiNav::watchStub, this) == 0;
    if (!watch_thread_) { kill(pid, SIGTERM); }
}

void* KombiNav::watchStub(void* self) {
    KombiNav* k = static_cast<KombiNav*>(self);
    k->watchReader(k->watch_fd_);
    return NULL;
}

void KombiNav::watchReader(int fd) {
    char buf[1024];
    size_t n = 0;
    for (;;) {
        ssize_t r = read(fd, buf + n, sizeof(buf) - 1 - n);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) return;
        n += (size_t)r;
        size_t start = 0;
        for (size_t i = 0; i < n; ++i) {
            if (buf[i] != '\n') continue;
            buf[i] = 0;
            pthread_mutex_lock(&watch_mu_);
            watch_.line(buf + start, monoS());
            pthread_mutex_unlock(&watch_mu_);
            start = i + 1;
        }
        if (start) { memmove(buf, buf + start, n - start); n -= start; }
        else if (n == sizeof(buf) - 1) n = 0;            // over-long line: drop it
    }
}

void KombiNav::stopWatch() {
    bool reaped = true;
    if (watch_pid_ > 0) {
        kill(watch_pid_, SIGTERM);
        int st;
        reaped = false;
        for (int i = 0; i < 20; ++i) {
            if (waitpid(watch_pid_, &st, WNOHANG) == watch_pid_) { reaped = true; break; }
            if (i == 10) kill(watch_pid_, SIGKILL);
            usleep(50000);
        }
        watch_pid_ = -1;
    }
    if (watch_thread_) pthread_join(watch_tid_, NULL);
    watch_thread_ = false;
    if (watch_fp_ && reaped) pclose(watch_fp_);          // the child is reaped: pclose does not block
    else if (watch_fp_) close(fileno(watch_fp_));        // never reaped: pclose would wait forever, leak the FILE
    else if (watch_fd_ >= 0) close(watch_fd_);
    watch_fp_ = NULL;
    watch_fd_ = -1;
    watch_method_ = -1;
    watch_spawned_ = -1e9;
}

} // namespace kombinav
} // namespace nbt
