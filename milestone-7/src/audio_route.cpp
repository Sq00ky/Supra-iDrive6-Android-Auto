// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "audio_route.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>

namespace nbt {

namespace {

struct RouteJob {
    std::string command;
    int retries;        // extra attempts after a non-zero exit (build requests only)
    int delayMs;        // wait before running (connection snapshots)
    int maxLines;       // output lines copied into the log
    AudioRoute::GrantedFn onGranted;   // build requests: called once audioconn exits 0
    void* user;
    AudioRoute::GrantedFn onDone;      // remove requests: called when the worker is finished (any outcome)
    void* doneUser;
    AudioRoute* owner;  // for cancellation (generation check)
    unsigned gen;       // owner's generation when the job was queued
    bool isBuild;       // holds the owner's single-flight build slot
    RouteJob() : retries(0), delayMs(0), maxLines(20), onGranted(0), user(0), onDone(0), doneUser(0), owner(0), gen(0),
                 isBuild(false) {}
};

// At most one audioconn process daemon-wide. A tester's head unit rebooted 5 times (v1.8.35 log 2026-10-29): the
// phone reconnected every ~3 s, every session start queued a fresh `audioconn -b` build with 10 retries and none was
// ever cancelled, so 4-7 audioconn processes (each pulling in the graphics libraries) ran side by side until
// audioconn died in operator new (SIGSEGV ref=0x8), ldd could no longer preload libEGL_X.gpp.so and the unit reset.
pthread_mutex_t g_execLock = PTHREAD_MUTEX_INITIALIZER;
int g_execRunning = 0;
const int kMaxAudioconn = 1;

bool jobStale(RouteJob* j) { return j->owner && j->owner->stale(j->gen); }

// Sleeps ms in 100 ms slices; false = the job was cancelled meanwhile.
bool sleepUnlessStale(RouteJob* j, int ms) {
    for (int t = 0; t < ms; t += 100) {
        if (jobStale(j)) return false;
        usleep(100000);
    }
    return !jobStale(j);
}

// Waits (up to 15 s) for a free audioconn slot; false = cancelled or still busy.
bool acquireExec(RouteJob* j) {
    for (int i = 0; i < 150; ++i) {
        if (jobStale(j)) return false;
        pthread_mutex_lock(&g_execLock);
        if (g_execRunning < kMaxAudioconn) {
            ++g_execRunning;
            pthread_mutex_unlock(&g_execLock);
            return true;
        }
        pthread_mutex_unlock(&g_execLock);
        usleep(100000);
    }
    printf("[AudioRoute] another audioconn still running after 15 s: skipping this one\n");
    return false;
}

void releaseExec() {
    pthread_mutex_lock(&g_execLock);
    if (g_execRunning > 0) --g_execRunning;
    pthread_mutex_unlock(&g_execLock);
}

// NBT_AUDIOCONN_RETRIES (default 6, max 20): a build that fails right after the phone connects
// ("AMCommand error" while the car's BT audio source is still settling) is retried with a growing pause:
// NBT_AUDIOCONN_RETRY_S (default 5) s, then +2 s per retry up to 12 s (5, 7, 9, 11, 12, 12 = ~56 s in all). Car logs
// (Supra 22-2-6, MINI F60 issue #9) showed the car refusing 0x3a for the whole of the old 3-retry (~10 s) window; a
// MINI tester saw iDrive's watchdog warn about NBTMediaMainApp (and dump cores of HmiMain3D, screen, LayerManager,
// NBTMediaMainApp in /dev/shmem, nothing crashed) while audioconn ran every 3 s for ~20 s: give the car more air.
int buildRetries() {
    const char* e = getenv("NBT_AUDIOCONN_RETRIES");
    if (!e || !*e) return 6;
    int n = atoi(e);
    if (n < 0) n = 0;
    if (n > 20) n = 20;
    return n;
}

} // namespace

int audioconnRetryDelaySec(int attempt) {
    const char* e = getenv("NBT_AUDIOCONN_RETRY_S");
    int first = (e && *e) ? atoi(e) : 5;
    if (first < 1) first = 1;
    int d = first + 2 * (attempt - 1);
    int cap = first > 12 ? first : 12;
    return d > cap ? cap : d;
}

namespace {

// Seconds since boot, same clock as the [Main] lifecycle lines and the iDrive app log.
double routeUpSec() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

int runOnce(const std::string& command, int maxLines = 20) {
    printf("[AudioRoute] [%9.3f] exec: %s\n", routeUpSec(), command.c_str());
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        printf("[AudioRoute] popen failed\n");
        return -1;
    }
    char line[256];
    int lines = 0;
    while (fgets(line, sizeof(line), pipe)) {
        if (lines >= maxLines) continue;   // drain the pipe, log only the first maxLines
        size_t n = strlen(line);
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = '\0';
        if (n > 0) {
            printf("[AudioRoute]   | %s\n", line);
            ++lines;
        }
    }
    int status = pclose(pipe);
    printf("[AudioRoute] [%9.3f] exit status %d\n", routeUpSec(), status);
    return status;
}

// One audioconn run in a free slot; -2 = not run (cancelled / no slot).
int runGuarded(RouteJob* job, int maxLines) {
    if (!acquireExec(job)) return -2;
    int status = runOnce(job->command, maxLines);
    releaseExec();
    return status;
}

void* routeWorker(void* arg) {
    RouteJob* job = static_cast<RouteJob*>(arg);
    bool cancelled = false;
    int status = -2;
    int attempt = 0;
    if (job->delayMs > 0 && !sleepUnlessStale(job, job->delayMs)) cancelled = true;
    if (!cancelled) {
        status = runGuarded(job, job->maxLines);
        cancelled = (status == -2 && jobStale(job));
    }
    for (attempt = 1; !cancelled && status != 0 && attempt <= job->retries; ++attempt) {
        int d = audioconnRetryDelaySec(attempt);
        printf("[AudioRoute] request failed: retry %d/%d in %d s\n", attempt, job->retries, d);
        fflush(stdout);
        if (!sleepUnlessStale(job, d * 1000)) { cancelled = true; break; }
        status = runGuarded(job, 20);
        if (status == -2 && jobStale(job)) cancelled = true;
    }
    if (cancelled) {
        printf("[AudioRoute] %s cancelled (the session that asked for it ended)\n", job->command.c_str());
    } else if (status != 0 && job->retries > 0) {
        printf("[AudioRoute] request still failing after %d retries: select Bluetooth audio in iDrive manually\n",
               job->retries);
    }
    fflush(stdout);
    if (!cancelled && status == 0 && job->onGranted) {
        if (attempt > 1) printf("[AudioRoute] request granted on attempt %d\n", attempt);
        fflush(stdout);
        job->onGranted(job->user);
    }
    if (job->isBuild && job->owner) job->owner->buildFinished(job->gen);
    if (job->onDone) job->onDone(job->doneUser);
    delete job;
    return NULL;
}

} // namespace

AudioRoute::AudioRoute() : gen_(0), buildBusy_(false), busyGen_(0) { pthread_mutex_init(&lock_, NULL); }

void AudioRoute::cancelPending() {
    pthread_mutex_lock(&lock_);
    ++gen_;
    bool busy = buildBusy_;
    pthread_mutex_unlock(&lock_);
    if (busy) printf("[AudioRoute] session ended: cancelling the pending audioconn request\n");
}

bool AudioRoute::stale(unsigned gen) {
    pthread_mutex_lock(&lock_);
    bool s = (gen != gen_);
    pthread_mutex_unlock(&lock_);
    return s;
}

void AudioRoute::buildFinished(unsigned gen) {
    pthread_mutex_lock(&lock_);
    if (buildBusy_ && busyGen_ == gen) buildBusy_ = false;
    pthread_mutex_unlock(&lock_);
}

bool AudioRoute::configure() {
    path_.clear();
    suffix_.clear();

    const char* env_suffix = getenv("NBT_AUDIOCONN_SUFFIX");
    if (env_suffix) suffix_ = env_suffix;

    const char* candidates[3] = {
        getenv("NBT_AUDIOCONN"),
        "/fs/sda0/opt/mm/bin/audioconn",
        "/opt/mm/bin/audioconn"
    };
    for (int i = 0; i < 3; ++i) {
        if (candidates[i] && candidates[i][0] && access(candidates[i], X_OK) == 0) {
            path_ = candidates[i];
            break;
        }
    }
    if (path_.empty()) {
        printf("[AudioRoute] audioconn not found; automatic routing disabled.\n");
        return false;
    }
    printf("[AudioRoute] using %s\n", path_.c_str());
    return true;
}

bool AudioRoute::run(char op, uint32_t a, uint32_t b, bool two_args, GrantedFn onGranted, void* user, int delayMs) {
    if (path_.empty() || a == 0) return false;

    char arg[96];
    if (two_args) {
        snprintf(arg, sizeof(arg), " -%c 0x%x 0x%x", op, static_cast<unsigned>(a), static_cast<unsigned>(b));
    } else {
        snprintf(arg, sizeof(arg), " -%c 0x%x", op, static_cast<unsigned>(a));
    }

    RouteJob* job = new RouteJob();
    job->command = path_ + arg + suffix_ + " 2>&1";
    job->retries = (op == 'b') ? buildRetries() : 0;
    job->delayMs = delayMs > 0 ? delayMs : 0;
    job->owner = this;
    if (op == 'b') {
        job->onGranted = onGranted;
        job->user = user;
        pthread_mutex_lock(&lock_);
        job->gen = gen_;
        if (buildBusy_ && busyGen_ == gen_ && busyCmd_ == job->command) {   // single flight: already pending
            pthread_mutex_unlock(&lock_);
            printf("[AudioRoute] build already pending: not starting another audioconn%s\n", arg);
            delete job;
            return true;
        }
        if (!buildBusy_ || busyGen_ != gen_) {   // take the slot (a different pending command runs unguarded)
            buildBusy_ = true;
            busyGen_ = gen_;
            busyCmd_ = job->command;
            job->isBuild = true;
        }
        unsigned g = gen_;
        bool owns = job->isBuild;
        pthread_mutex_unlock(&lock_);
        if (!spawn(job)) { if (owns) buildFinished(g); return false; }
        return true;
    }
    job->onDone = onGranted;   // remove: completion callback (see remove())
    job->doneUser = user;
    pthread_mutex_lock(&lock_);
    job->gen = gen_;
    pthread_mutex_unlock(&lock_);
    return spawn(job);
}

// Read-only snapshot of the car's active audio connections (`audioconn -l`), e.g. while the Assistant
// replies, to see whether the Bluetooth source is ducked or another (voice) connection is active.
bool AudioRoute::listConnections(const char* tag, int delayMs) {
    if (path_.empty()) return false;
    printf("[AudioRoute] connection snapshot in %d ms (%s)\n", delayMs, tag ? tag : "");
    RouteJob* job = new RouteJob();
    job->command = path_ + " -l" + suffix_ + " 2>&1";
    job->delayMs = delayMs;
    job->maxLines = 40;
    job->owner = this;
    pthread_mutex_lock(&lock_);
    job->gen = gen_;
    pthread_mutex_unlock(&lock_);
    return spawn(job);
}

bool AudioRoute::spawn(void* p) {
    RouteJob* job = static_cast<RouteJob*>(p);
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_t tid;
    int rc = pthread_create(&tid, &attr, &routeWorker, job);
    pthread_attr_destroy(&attr);
    if (rc != 0) {
        delete job;
        return false;
    }
    return true;
}

bool AudioRoute::build(uint32_t conn_id, GrantedFn cb, void* user, int delayMs) {
    return run('b', conn_id, 0, false, cb, user, delayMs);
}
bool AudioRoute::remove(uint32_t conn_id, GrantedFn onDone, void* user) {
    return run('r', conn_id, 0, false, onDone, user, 0);
}
bool AudioRoute::buildFocus(uint32_t src, uint32_t sink, GrantedFn cb, void* user, int delayMs) {
    return run('b', src, sink, true, cb, user, delayMs);
}
bool AudioRoute::removeFocus(uint32_t src, uint32_t sink) { return run('r', src, sink, true, 0, 0, 0); }

} // namespace nbt
