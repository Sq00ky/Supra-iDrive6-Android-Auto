// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef MOST_ROUTER_HPP
#define MOST_ROUTER_HPP

// Sends a received message to whoever asked for it: (fblock, inst | any, fkt | any, op | any) -> handlers. Every match
// runs, in the order added, so a logger and a consumer can share a message. Nothing is deduplicated: the head unit's
// messages arrive once per receiver, and the decoders compare against their last state. Registration may allocate; matching and dispatch do not.

#include "most/msg.hpp"
#include <new>
#include <vector>

namespace nbt {
namespace most {

enum { kAny = -1 };
typedef void (*Handler)(const View& m, void* user);

class Router {
public:
    // False on allocation failure or with no handler; an unsuccessful append leaves existing routes intact.
    bool add(uint8_t fblock, int inst, int fkt, int op, Handler h, void* user) {
        if (!h || routes_.size() == routes_.max_size()) return false;
        Route r;
        r.fblock = fblock; r.inst = inst; r.fkt = fkt; r.op = op; r.h = h; r.user = user;
        try { routes_.push_back(r); }
        catch (const std::bad_alloc&) { return false; }
        return true;
    }

    bool wants(const View& m) const {
        for (size_t i = 0; i < routes_.size(); ++i)
            if (matches(routes_[i], m)) return true;
        return false;
    }

    // Runs every match and returns how many ran.
    int dispatch(const View& m) const {
        int ran = 0;
        for (size_t i = 0; i < routes_.size(); ++i) {
            if (!matches(routes_[i], m)) continue;
            routes_[i].h(m, routes_[i].user);
            ++ran;
        }
        return ran;
    }

    size_t size() const { return routes_.size(); }

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

    std::vector<Route> routes_;
};

} // namespace most
} // namespace nbt

#endif
