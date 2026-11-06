// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test: most::Router. Matching, wildcards, every match running in the order added, and the route limit.
#include "most/router.hpp"
#include <assert.h>
#include <stdio.h>
#include <string>

using namespace nbt::most;

static std::string g_ran;
static void mark(const View&, void* user) { g_ran += static_cast<const char*>(user); }

static View header(uint8_t fblock, uint8_t inst, uint16_t fkt, uint8_t op) {
    View v;
    v.txType = 0; v.varTag = 2; v.varVal = 0; v.dst = 0; v.src = 0; v.hdrTag = 0; v.data = NULL; v.len = 0; v.rxMs = 0;
    v.fblock = fblock; v.inst = inst; v.fkt = fkt; v.op = op;
    return v;
}

int main() {
    Router r;
    assert(!r.wants(header(0xE5, 3, 0xC46, 0xC)));
    assert(r.dispatch(header(0xE5, 3, 0xC46, 0xC)) == 0);

    char a[] = "a", b[] = "b", c[] = "c", d[] = "d";
    assert(r.add(0xE5, kAny, kAny, kAny, &mark, a));           // the controller, on any instance
    assert(r.add(0x15, kAny, 0x512, 0xC, &mark, b));           // the voice button, Status only
    assert(r.add(0x52, 0, 0x440, kAny, &mark, c));             // pinned to an instance
    assert(r.add(0xE5, 3, 0xC46, kAny, &mark, d));             // shares a message with the first route

    g_ran.clear();
    assert(r.wants(header(0xE5, 3, 0xC46, 0xC)));
    assert(r.dispatch(header(0xE5, 3, 0xC46, 0xC)) == 2 && g_ran == "ad");   // all matches, in the order added

    g_ran.clear();
    assert(r.dispatch(header(0xE5, 7, 0x123, 0x1)) == 1 && g_ran == "a");

    g_ran.clear();
    assert(r.dispatch(header(0x15, 2, 0x512, 0xC)) == 1 && g_ran == "b");
    assert(r.dispatch(header(0x15, 2, 0x512, 0x1)) == 0);                    // wrong op
    assert(r.dispatch(header(0x15, 2, 0x401, 0xC)) == 0);                    // wrong function
    assert(!r.wants(header(0x15, 2, 0x401, 0xC)));

    g_ran.clear();
    assert(r.dispatch(header(0x52, 0, 0x440, 0xC)) == 1 && g_ran == "c");
    assert(r.dispatch(header(0x52, 1, 0x440, 0xC)) == 0);                    // wrong instance

    assert(!r.add(0x05, 0, 0, 0, NULL, NULL));                               // no handler
    while (r.size() < Router::kMaxRoutes) assert(r.add(0x05, 0, 0, 0, &mark, a));
    assert(!r.add(0x05, 0, 0, 0, &mark, a));                                 // full

    printf("test_most_router OK\n");
    return 0;
}
