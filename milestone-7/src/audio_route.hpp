// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef AUDIO_ROUTE_HPP
#define AUDIO_ROUTE_HPP

// Stop-gap source->speaker routing through the stock `audioconn` CLI.
//
// audioconn is the test client of the head unit's ConnectionMaster. Stock
// CarPlay audio is routed by the HMI building a ConnectionMaster connection for
// an AuxInCarPlay source; the corresponding DSI service has not been
// reverse-engineered yet, so for now we shell out to:
//
//     audioconn -b 0x<connId>     (build)       audioconn -r 0x<connId>   (remove)
//
// Two forms exist (from `audioconn` usage):
//     -b|-r <connId>        direct ConnectionMaster build/remove. Observed to
//                           FAIL ("CMCommand error in execution") for CarPlay
//                           connections on the car.
//     -b|-r <src> <snk>     requestFocus/releaseFocus over the AudioManagement
//                           DSI interface (no SourceActivity sent). This is the
//                           HMI-style path and the default here.
// <src>/<snk> are the eSRC/eSNK codes shown in `audioconn -l`:
//     0xb9 eSRC_A4A_CARPLAY_ENT_MEDIA   0xba eSRC_A4A_CARPLAY_INT_MIX
//     0xb0 eSRC_CARPLAY_ENT             0xb3 eSRC_CARPLAY_NAV     sink 0x1 = SPEAKERS
//
// Commands run on a detached worker thread so they can never stall the
// protocol thread. Environment overrides:
//     NBT_AUDIOCONN         path to audioconn
//     NBT_AUDIOCONN_SUFFIX  extra arguments appended to -b/-r (e.g. " 0x01")

#include <stdint.h>
#include <pthread.h>
#include <string>

namespace nbt {

// Pause before build retry `attempt` (1-based): NBT_AUDIOCONN_RETRY_S (default 5), +2 s per retry, capped at 12 s.
int audioconnRetryDelaySec(int attempt);

class AudioRoute {
public:
    AudioRoute();

    // Resolves the audioconn binary (env override, then known locations).
    bool configure();
    bool available() const { return !path_.empty(); }

    // Fire-and-forget; return false only if the job could not be queued. A build is retried
    // (NBT_AUDIOCONN_RETRIES) while the car refuses it; onGranted(user) runs on the worker thread once
    // audioconn exits 0 (not called when every attempt failed).
    typedef void (*GrantedFn)(void* user);
    // Builds are single-flight per AudioRoute and command: while a build (with its retries) is pending, the same
    // build() again only logs and returns true. delayMs waits before the first attempt (cancelPending() during the wait = no
    // audioconn at all).
    bool build(uint32_t conn_id, GrantedFn onGranted = 0, void* user = 0, int delayMs = 0);
    bool remove(uint32_t conn_id);
    bool buildFocus(uint32_t src, uint32_t sink, GrantedFn onGranted = 0, void* user = 0, int delayMs = 0);
    bool removeFocus(uint32_t src, uint32_t sink);
    // Read-only `audioconn -l` after delayMs, logged as "[AudioRoute]   | ..." (40 lines max).
    bool listConnections(const char* tag, int delayMs);

    // Session ended: every queued / sleeping / retrying job of this route stops before its next audioconn run
    // (an audioconn already running finishes). Jobs queued afterwards (e.g. a release) are not affected.
    void cancelPending();

    // Worker-thread hooks (internal).
    bool stale(unsigned gen);
    void buildFinished(unsigned gen);

private:
    bool run(char op, uint32_t a, uint32_t b, bool two_args, GrantedFn onGranted, void* user, int delayMs);
    bool spawn(void* job);

    std::string path_;
    std::string suffix_;
    pthread_mutex_t lock_;
    unsigned gen_;          // bumped by cancelPending()
    bool buildBusy_;        // a build worker of generation busyGen_ is alive
    unsigned busyGen_;
    std::string busyCmd_;   // the command the busy build runs (single flight is per command)
};

} // namespace nbt

#endif // AUDIO_ROUTE_HPP
