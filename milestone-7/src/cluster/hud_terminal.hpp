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
// whole state. It is told through the status listener, and epoch() changes whenever work was dropped.
//
// Batches: one is sent at a time, back to back. Each belongs to a slot, one per thing the driver draws on its own (the
// main picture with its texts, a route overlay, lanes), and one batch per slot waits behind the one in progress, in
// the order they came. A "full" batch redraws everything of its slot (a new maneuver, a new route), so a full batch
// submitted while one of its slot waits takes its place, at the back of the queue: what it draws on comes first. A
// partial batch (a new distance) is merged into its slot's waiting batch instead: a text call for a handle the waiting
// batch already sets replaces that call's payload in place, anything else is appended. So a distance update never
// wipes out a new maneuver that has not gone out yet, and a new route never wipes out the roads it is drawn on. A
// batch for every slot (a clear) takes the place of everything waiting.
//
// The factory navigation, as the 6WA showed it (car, S19):
//   - what we see of it is the cluster's answers to it (host -> the navigation's address, each with its call's
//     payload); its calls to the host are not routed to us. A call to the host would count the same way.
//   - any FB 82 message to or from the host that is neither ours, nor an answer to us, nor the cluster's copy of an answer
//     to us (RecentCalls) is the navigation: we yield from that message on.
//   - a route is on screen once the navigation draws: SetVisibility(frame, 1), SwitchVisibility, an item added, or a
//     non-empty text. A route ends with SetVisibility(frame, 0), then the navigation clearing its instances (no
//     drawing), about 3 s later. Once the frame is off, nothing but SetVisibility(frame, 1) puts a route back on
//     screen until we are released: the clean-up may swap buffers back or set a status text, and that must not keep
//     us yielded for good.
//   - quiet alone does not end a route: the navigation only sends on a change, so a parked car on a route is quiet too.
//     So the navigation is over once no route is on screen and the terminal has been quiet for navQuietMs. Traffic that
//     never draws (a set-up, a status) ends that way too.
//   - at start-up the navigation is taken to be idle: whether a route that is already running can be seen without
//     traffic is a car test still to do. Its state is kept when the link drops (a route that ended meanwhile leaves us
//     yielded until the next one ends, the safe side) and reset when the host changes.
//
// Answers to calls the terminal dropped (leaving kReady drops the calls in flight) can still arrive. Within replyMs of
// the drop, a success is a stray and an error is counted (lateErrors) and does not fail the terminal: a navigation
// taking over the instances can make a call of ours fail that was fine when it was sent.

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

// What a batch draws: its own slot's waiting batch is the only one it may take the place of or merge into.
enum Slot { kSlotMain = 0, kSlotRoute, kSlotLanes, kSlotCount, kSlotAll = 255 };

struct Batch {
    std::string label;       // for the log
    std::vector<Call> calls;
    bool full;               // redraws everything of its slot: may replace that slot's waiting batch
    uint8_t slot;            // Slot; kSlotAll (a clear) replaces every waiting batch
    Batch() : full(false), slot(kSlotMain) {}
    explicit Batch(const char* l, bool f = false, uint8_t s = kSlotMain) : label(l), full(f), slot(s) {}
    void add(uint16_t fkt, const std::vector<uint8_t>& p) { calls.push_back(Call(fkt, p)); }
};

enum SubmitResult {
    kQueued,      // will be sent after the batch in progress, if any
    kReplaced,    // a full batch took the place of a queued one of its slot that had not started: the newest wins
    kMerged,      // a partial batch was merged into its slot's queued one
    kNotReady,    // status is not kReady: dropped, the driver redraws when kReady comes back
    kRefused      // empty, too long, or a call breaks a limit (cluster/hudterm.hpp): nothing of it is sent
};
const char* submitResultName(SubmitResult r);

struct TerminalStats {
    uint32_t batches, batchesDone, batchesDropped, replaced, merged, refused;
    uint32_t calls, answers, copies, strays, navMsgs, timeouts, errors, lateErrors;
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
    bool busy() const { return haveCurrent_ || !pending_.empty(); }
    // A batch of this slot (or a clear) waits behind the one in progress.
    bool waiting(int slot) const;
    uint32_t epoch() const { return epoch_; }      // changes whenever submitted work was dropped
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
    bool merge(Batch& into, const Batch& b);
    void fail(const char* why, uint32_t now);
    void refresh(const char* why, uint32_t now);
    void dropWork(uint32_t now);
    bool takeDropped(uint16_t fkt, bool byHandle, uint16_t handle, uint32_t now);
    void finishIfDone(uint32_t now);

    Config cfg_;
    bool linkUp_;
    uint16_t self_, host_;
    bool failed_, navActive_, routeOn_;
    bool frameOff_;          // the navigation switched its frame off: only frame-on draws again until the release
    uint32_t lastNavMs_;
    Status status_;
    bool changed_;

    Batch current_;
    std::vector<Batch> pending_;      // at most one per slot, in the order they came
    bool haveCurrent_;
    size_t nextCall_;
    uint32_t batchStartMs_;
    std::vector<InFlight> inFlight_;
    std::vector<InFlight> dropped_;   // calls in flight when work was dropped, sentMs = the drop time
    RecentCalls recent_;
    uint32_t tag_;
    uint32_t epoch_;
    TerminalStats stats_;
};

// What a HUDTerminal cluster driver needs from the transport: HudTerminal, or a test's fake.
class Port {
public:
    virtual ~Port() {}
    virtual SubmitResult submit(const Batch& b) = 0;
    virtual Status status() const = 0;
    virtual uint32_t epoch() const = 0;
    // A batch of this slot (or a clear) waits behind the one in progress: a full batch of the slot submitted now would
    // take its place.
    virtual bool waiting(int slot) const = 0;
    virtual void reset() = 0;                     // leave kFailed
    // Until everything submitted is answered: true then; false on the timeout or once the status leaves kReady.
    virtual bool waitIdle(uint32_t ms) = 0;
};

// A Core on its own thread. attach() before the client's start(). The listener runs on the terminal's thread with no
// lock held, so it may submit().
class HudTerminal : public Port {
public:
    typedef bool (*SendFn)(const most::Msg& m, void* user);
    typedef void (*StatusCb)(Status s, void* user);

    explicit HudTerminal(const Config& cfg);
    ~HudTerminal();

    // A route for 82/00, link and registry listeners and, when sending, allowSend. Without allowSend the client refuses
    // every call, which is what the shadow mode wants: the terminal only watches.
    bool attach(most::Client& c, bool sending = true);
    void setSender(SendFn fn, void* user);        // in place of the client's send (host tests)
    void setStatusListener(StatusCb cb, void* user);
    bool start();
    void stop();

    SubmitResult submit(const Batch& b);
    void reset();
    Status status() const;
    uint32_t epoch() const;
    bool waiting(int slot) const;
    bool waitIdle(uint32_t ms);
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
