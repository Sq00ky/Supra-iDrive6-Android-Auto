// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef MOST_CLIENT_HPP
#define MOST_CLIENT_HPP

// The daemon's one client on MOSTSubSystem.DSYSMOSTServer 4.5. It observes: consumers add routes for the messages they
// want, and nothing is ever sent to a function block. There is no public send; the private DsiClient only registers.
// This folder is the library: no Android Auto code, no environment reads, no consumer names.
//
// What reaches a route depends on the message class (most/msg.hpp): normal and noinfo messages as they are, segments only
// once joined, and transmit status not at all. All of them are counted.
//
// Handlers run on the DSI callback thread, one after the other, and everything behind them waits. The DSI library
// queues between its receiver thread and this one with no bound seen, so a slow handler costs late events and memory in
// our own process. If the client's IPC buffer ever fills, the server's write fails at once and the message is lost with
// no notice. So, in a handler:
//   - never block: post to a queue, as AASession::postKeyEvent does
//   - keep logging bounded: with USB logging on, each line is a write to the stick from this thread
//   - accept copies: the head unit's messages arrive once per receiver, and the router does not dedupe
//   - accept loss: full-state messages correct themselves with the next status, single-event messages do not
//   - treat View::rxMs as callback time: after a stall it is later than arrival
//   - do not rely on __sync: __sync_synchronize() is a no-op on GCC 4.4.2. State handed between threads takes a mutex.

#include "dsi_client.hpp"
#include "most/ids.hpp"
#include "most/msg.hpp"
#include "most/reassembler.hpp"
#include "most/router.hpp"
#include <pthread.h>
#include <stdint.h>

namespace nbt {
namespace most {

// Routing never depends on this: a wrong reading must not stop a consumer. Link state only tells consumers when the
// messages they were following may have stopped.
//   kDetached     no connection to the server, or it went away
//   kRegistering  attached, no answer to the registration yet
//   kRefused      the server refused the registration
//   kCommNotOk    comm state was OK (kCommStateOk) and has changed to something else
//   kUp           registered, and comm state has not gone bad. The car reports comm state 1 while messages flow, so a
//                 value other than kCommStateOk does not count against the link until OK has been seen once.
enum LinkState { kDetached, kRegistering, kRefused, kCommNotOk, kUp };
const char* linkStateName(LinkState s);
typedef void (*LinkCb)(LinkState s, void* user);    // on the DSI callback thread, for every change

// Totals since start().
struct ClientStats {
    uint32_t msgs;            // parsed messages
    uint32_t normal, seg, txStatus, noInfo;
    uint32_t routed;          // messages that reached at least one handler, a joined message counting once
    uint32_t routedNonNormal; // messages a route wants, in any class other than normal
    uint32_t joined;          // segmented messages joined and handed on
    uint32_t segLost;         // segments a route wants that could not be joined: no first segment, or over the cap
    uint32_t bad;             // frames that did not parse
    uint32_t longestHandlerMs;
};

class Client {
public:
    enum { kMaxKeys = 16, kMaxLinkListeners = 4 };

    Client();

    // Before start() only. All return false when full or already started.
    bool addRoute(uint8_t fblock, int inst, int fkt, int op, Handler h, void* user);
    bool addDevCtrl(uint16_t key);            // a key added twice is sent once; request 1 is skipped with no keys
    bool addLinkListener(LinkCb cb, void* user);
    void setRawLog(bool on);                  // log every raw DSI event (very noisy)

    // Opens the DSI connection. With no consumer attached it does nothing and returns true. False: DSI open failed.
    bool start(const char* clientName);

    LinkState state() const;
    ClientStats stats() const;

    // One line with the counters since the last call, so a silent client still shows. From main's loop, every 60 s.
    void logHealth();

private:
    static void attachedTramp(DsiClient* c, void* self);
    static void detachedTramp(DsiClient* c, void* self);
    static void dataTramp(DsiClient* c, uint32_t type, uint32_t id, uint32_t seq, const uint8_t* p, uint32_t len, void* self);
    void onAttached(DsiClient* c);
    void onDetached();
    void onData(DsiClient* c, uint32_t id, const uint8_t* p, uint32_t len);
    void onMessage(const uint8_t* p, uint32_t len);
    void noteFirst(const View& m, MsgClass c, bool wanted);
    LinkState deriveStateLocked() const;
    void updateStateLocked();         // call with lock_ held: unlocks it, then tells the listeners about a change

    DsiClient dsi_;
    Router router_;
    uint16_t keys_[kMaxKeys];
    int nKeys_;
    struct LinkListener { LinkCb cb; void* user; };
    LinkListener listeners_[kMaxLinkListeners];
    int nListeners_;
    bool started_;
    bool rawLog_;
    char name_[32];

    // Callback thread only.
    int stage_;                       // 0 init, 1 name sent, 2 devctrls sent
    Reassembler reassembler_;
    unsigned firstSeen_[kNoInfo + 1];
    unsigned routedNonNormalLogged_;

    // Under lock_: the callback thread writes, logHealth and the getters read.
    mutable pthread_mutex_t lock_;
    LinkState state_;
    bool attached_;
    bool answered_;                   // the registration has been answered
    uint32_t clientId_;
    unsigned deviceId_;
    bool haveComm_;
    uint32_t comm_;
    bool commOkSeen_;
    ClientStats total_;
    ClientStats lastHealth_;
    uint32_t longestWindowMs_;
    uint32_t lastHealthMs_;
};

} // namespace most
} // namespace nbt

#endif
