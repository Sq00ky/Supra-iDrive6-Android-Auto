// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef CLUSTER_HUD_TERMINAL_HPP
#define CLUSTER_HUD_TERMINAL_HPP

// The MOST transport for the cluster's HUDTerminal (FB 82/00, cluster/hudterm.hpp). A cluster driver hands it batches
// of calls; it sends them to the registry's host of 82/00, matches the cluster's answers, and steps aside whenever the
// factory navigation uses the terminal. It knows nothing of what is drawn: the handles it may use and the factory's
// frame instance come from the driver's profile.
//
// Core is the logic, pure and single-threaded, with time passed in: host-tested in tests/test_hud_terminal.cpp against
// a scripted cluster. HudTerminal runs a Core on its own send thread, fed by most::Client's callbacks.
//
// Status:
//   kOffline    no MOST link, no registered address, or no host of 82/00 in the registry
//   kReady      batches are sent
//   kNavActive  the factory navigation is using the terminal: nothing is sent, and queued work is dropped
//   kFailed     a call got no answer within the reply timeout, or an error answer: nothing is sent until reset(), the
//               link comes back, or the host changes
// Leaving kReady drops the batch in progress and the one queued, so on the way back to kReady the driver redraws its
// whole state. It is told through the status listener.
//
// The factory navigation, as the 6WA showed it (car, S19):
//   - any FB 82 message to or from the host that is neither ours, nor an answer to us, nor the cluster's copy of an answer
//     to us (RecentCalls) is the navigation: it is active from that message on.
//   - quiet alone does not end it: the navigation only sends on a change, so a parked car on a route is quiet too. A
//     route ends with SetVisibility(frame, 0), then the navigation clearing its instances, about 3 s later. So it is
//     over once the frame was switched off and the terminal has been quiet for navQuietMs.
//   - at start-up the navigation is taken to be idle: whether a route that is already running can be seen without
//     traffic is a car test still to do. Its state is kept when the link drops (a route that ended meanwhile leaves us
//     yielded until the next one ends, the safe side) and reset when the host changes.

#include "cluster/hudterm.hpp"
#include "most/client.hpp"
#include <pthread.h>
#include <stdint.h>
#include <string>
#include <vector>

namespace nbt {
namespace hudterm {

enum Status { kOffline, kReady, kNavActive, kFailed };
const char* statusName(Status s);

struct Config {
    HandleSet handles;       // the only handles calls may use
    int navFrame;            // the factory navigation's frame instance: SetVisibility(navFrame, 0) ends a route
    uint32_t replyMs;        // per call, and the copy window
    uint32_t navQuietMs;     // quiet after the frame went off before the navigation counts as finished
    unsigned maxInFlight;    // calls sent and not yet answered; a batch goes out back to back up to this
    unsigned maxBatch;       // calls in one batch
    bool logBatches;         // a line per finished batch (otherwise only status changes, refusals and failures)
    Config() : navFrame(-1), replyMs(1500), navQuietMs(5000), maxInFlight(8), maxBatch(32), logBatches(false) {}
};

struct Batch {
    std::string label;       // for the log
    std::vector<Call> calls;
    Batch() {}
    explicit Batch(const char* l) : label(l) {}
    void add(uint16_t fkt, const std::vector<uint8_t>& p) { calls.push_back(Call(fkt, p)); }
};

enum SubmitResult {
    kQueued,      // will be sent after the batch in progress, if any
    kReplaced,    // took the place of a queued batch that had not started: the newest state wins
    kNotReady,    // status is not kReady: dropped, the driver redraws when kReady comes back
    kRefused      // empty, too long, or a call breaks a limit (cluster/hudterm.hpp): nothing of it is sent
};
const char* submitResultName(SubmitResult r);

struct TerminalStats {
    uint32_t batches, batchesDone, batchesDropped, replaced, refused;
    uint32_t calls, answers, copies, strays, navMsgs, timeouts, errors;
};

class Core {
public:
    explicit Core(const Config& cfg);

    // Inputs. now: monotonic ms.
    void setLink(bool up, uint16_t self, uint32_t now);
    void setHost(uint16_t host, uint32_t now);
    void onMessage(const most::View& m, uint32_t now);
    void tick(uint32_t now);                  // timeouts and the end of the navigation's route
    void reset(uint32_t now);                 // leave kFailed

    SubmitResult submit(const Batch& b, uint32_t now);

    // The next message to send, if any, already counted as in flight. After a failed send, sendFailed().
    bool next(uint32_t now, most::Msg& out);
    void sendFailed(uint32_t now);

    Status status() const { return status_; }
    bool busy() const { return haveCurrent_ || havePending_; }
    uint16_t host() const { return host_; }
    // How long until tick() has something to do, for the send thread's wait; a large value when nothing is timed.
    uint32_t msUntilDeadline(uint32_t now) const;
    // A status change since the last call, for the listener.
    bool takeStatusChange(Status& s);
    TerminalStats stats() const { return stats_; }

private:
    struct InFlight { uint16_t fkt, handle; uint32_t sentMs; };

    void onAnswer(const most::View& m, uint32_t now);
    void onNav(const most::View& m, uint32_t now);
    void fail(const char* why, uint32_t now);
    void refresh(const char* why, uint32_t now);
    void dropWork();
    void finishIfDone(uint32_t now);

    Config cfg_;
    bool linkUp_;
    uint16_t self_, host_;
    bool failed_, navActive_, frameOff_;
    uint32_t lastNavMs_;
    Status status_;
    bool changed_;

    Batch current_, pending_;
    bool haveCurrent_, havePending_;
    size_t nextCall_;
    uint32_t batchStartMs_;
    std::vector<InFlight> inFlight_;
    RecentCalls recent_;
    uint32_t tag_;
    TerminalStats stats_;
};

// A Core on its own thread. attach() before the client's start(). The listener runs on the terminal's thread with no
// lock held, so it may submit().
class HudTerminal {
public:
    typedef bool (*SendFn)(const most::Msg& m, void* user);
    typedef void (*StatusCb)(Status s, void* user);

    explicit HudTerminal(const Config& cfg);
    ~HudTerminal();

    bool attach(most::Client& c);                 // allowSend, a route for 82/00, link and registry listeners
    void setSender(SendFn fn, void* user);        // in place of the client's send (host tests)
    void setStatusListener(StatusCb cb, void* user);
    bool start();
    void stop();

    SubmitResult submit(const Batch& b);
    void reset();
    Status status() const;
    TerminalStats stats() const;
    void logHealth();                             // one line, from main's 60 s loop

    // What the client's callbacks feed in; public for the host tests.
    void onMessage(const most::View& m);
    void onLink(bool up, uint16_t self);
    void onRegistry(const std::vector<most::RegistryEntry>& reg);

private:
    static void* threadMain(void* self);
    static void msgTramp(const most::View& m, void* self);
    static void linkTramp(most::LinkState s, void* self);
    static void regTramp(const std::vector<most::RegistryEntry>& reg, void* self);
    static bool clientSend(const most::Msg& m, void* self);
    void run();
    void wake();

    Core core_;
    most::Client* client_;
    SendFn send_;
    void* sendUser_;
    StatusCb statusCb_;
    void* statusUser_;
    mutable pthread_mutex_t lock_;
    pthread_cond_t cond_;
    pthread_t thread_;
    bool running_, stop_;
    TerminalStats lastHealth_;
};

} // namespace hudterm
} // namespace nbt

#endif
