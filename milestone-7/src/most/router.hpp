// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef MOST_ROUTER_HPP
#define MOST_ROUTER_HPP

// Sends a received message to whoever asked for it: (fblock, inst | any, fkt | any, op | any) -> handlers. Every match
// runs, in the order added, so a logger and a consumer can share a message. Nothing is deduplicated: the head unit's
// messages arrive once per receiver, and the decoders compare against their last state. Pure, no allocation.

#include "most/msg.hpp"
#include "most/latency_diag.hpp"

namespace nbt {
namespace most {

enum { kAny = -1 };
typedef void (*Handler)(const View& m, void* user);

class Router {
public:
    enum { kMaxRoutes = 16 };

    Router() : n_(0) {}

    // False when full (or with no handler): the caller logs it, because that consumer would stay silent otherwise.
    bool add(uint8_t fblock, int inst, int fkt, int op, Handler h, void* user) {
        if (n_ >= kMaxRoutes || !h) return false;
        Route& r = routes_[n_++];
        r.fblock = fblock; r.inst = inst; r.fkt = fkt; r.op = op; r.h = h; r.user = user;
        return true;
    }

    bool wants(const View& m) const {
        for (int i = 0; i < n_; ++i)
            if (matches(routes_[i], m)) return true;
        return false;
    }

    // Runs every match and returns how many ran.
    int dispatch(const View& m) const {
        int ran = 0;
        for (int i = 0; i < n_; ++i) {
            if (!matches(routes_[i], m)) continue;
            const uint64_t diagnosticEntry=latencydiag::stamp();
            routes_[i].h(m, routes_[i].user);
            if(latencydiag::enabled())latencydiag::record(latencydiag::MostRouteSpan,
                latencydiag::metadata(m),diagnosticEntry,0,routes_[i].user);
            ++ran;
        }
        return ran;
    }

    int size() const { return n_; }

private:
    struct Route {
        uint8_t fblock;
        int inst, fkt, op;
        Handler h;
        void* user;
    };

    static bool matches(const Route& r, const View& m) {
        return r.fblock == m.fblock && (r.inst == kAny || r.inst == m.inst) &&
               (r.fkt == kAny || r.fkt == m.fkt) && (r.op == kAny || r.op == m.op);
    }

    Route routes_[kMaxRoutes];
    int n_;
};

} // namespace most
} // namespace nbt

#endif
