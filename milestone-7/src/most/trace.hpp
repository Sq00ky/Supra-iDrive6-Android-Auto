// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef MOST_TRACE_HPP
#define MOST_TRACE_HPP

// An opt-in log of everything the MOST client sees, for working out what the car's signals are from a drive. It is a user
// of the client's taps and adds no keys and no requests. Lines are tagged [MOSTLOG].
//   - the registry, when it arrives or changes, and any instance change (kInfoChangeInstId)
//   - for each message type (fblock, inst, fkt, op): the first few, then only changes of payload
//   - a cap on the lines for each type. "Changes only" is not a rate limit: a position changes every second and a status
//     with a message counter differs every time.
// It runs on the DSI callback thread, and with USB logging on each line is a write to the stick from there. The cap is what
// keeps that bounded: one type cannot use up the log, and the client's longest-handler figure shows the cost.

#include "most/msg.hpp"
#include "most/registry.hpp"
#include <stdint.h>
#include <vector>

namespace nbt {
namespace most {

class Client;

// The decision for each message: pure, host-tested in tests/test_most_trace.cpp.
class TraceFilter {
public:
    enum { kMaxTypes = 512, kFirstLines = 3, kLinesPerType = 200 };
    enum Decision {
        kSkip,        // nothing to log
        kLog,
        kLogFinal,    // log it, and say that this type is now at its cap
        kTableFull    // this type does not fit in the table: nothing to log
    };

    TraceFilter();
    Decision feed(const View& m);

private:
    struct Type {
        bool used;
        uint8_t fblock, inst, op;
        uint16_t fkt;
        uint16_t lines;     // logged so far
        uint32_t seen;
        uint32_t hash;      // of the last payload
        uint32_t len;
    };
    Type* lookup(const View& m);

    Type types_[kMaxTypes];
};

class Trace {
public:
    Trace();

    // Before the client's start(). False: the client already has a tap, or has started.
    bool attach(Client& client);

private:
    static void tapTramp(const View& m, MsgClass c, void* self);
    static void infoTramp(uint32_t id, const uint8_t* p, uint32_t len, void* self);
    void onMessage(const View& m, MsgClass c);
    void onInfo(uint32_t id, const uint8_t* p, uint32_t len);

    TraceFilter filter_;
    bool tableFullLogged_;
    std::vector<RegistryEntry> registry_;
    unsigned badRegistryLogged_;
};

} // namespace most
} // namespace nbt

#endif
