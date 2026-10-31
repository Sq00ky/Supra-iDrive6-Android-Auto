// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test for src/session_owner.hpp: which transport may take the AASession from which.
// g++ -std=gnu++98 -Wall -Wextra -pthread -Isrc -o /tmp/test_session_owner tests/test_session_owner.cpp
#include "session_owner.hpp"
#include <assert.h>
#include <stdio.h>
#include <string.h>

using namespace nbt;

static SessionOwner g_owner;

// The session-ended callback clears the owner from another thread while startSession() reads and sets it.
static void* churn(void*) {
    for (int i = 0; i < 100000; ++i) {
        g_owner.set(kTransportNone);
        (void)g_owner.get();
    }
    return NULL;
}

int main() {
    // Nothing running: either transport may start.
    assert(mayStart(kTransportNone, kTransportTcp));
    assert(mayStart(kTransportNone, kTransportUsb));
    // A wireless phone that reconnected replaces its own dying session (the behaviour before USB existed).
    assert(mayStart(kTransportTcp, kTransportTcp));
    // A wireless connection never takes the session from the phone on the cable (review finding 1).
    assert(!mayStart(kTransportUsb, kTransportTcp));
    // Plugging a phone in does not interrupt a wireless session: USB waits until it ends.
    assert(!mayStart(kTransportTcp, kTransportUsb));
    // One USB bridge at a time.
    assert(!mayStart(kTransportUsb, kTransportUsb));
    assert(!mayStart(kTransportNone, kTransportNone));

    assert(strcmp(transportName(kTransportTcp), "wireless") == 0);
    assert(strcmp(transportName(kTransportUsb), "USB") == 0);
    assert(strcmp(transportName(kTransportNone), "none") == 0);

    assert(g_owner.get() == kTransportNone);
    g_owner.set(kTransportUsb);
    assert(g_owner.get() == kTransportUsb);
    pthread_t t;
    assert(pthread_create(&t, NULL, churn, NULL) == 0);
    for (int i = 0; i < 100000; ++i) {
        g_owner.set(kTransportTcp);
        Transport v = g_owner.get();
        assert(v == kTransportTcp || v == kTransportNone);
    }
    pthread_join(t, NULL);
    printf("test_session_owner: all passed\n");
    return 0;
}
