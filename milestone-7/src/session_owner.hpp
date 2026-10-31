// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef NBT_SESSION_OWNER_HPP
#define NBT_SESSION_OWNER_HPP

// Which transport owns the daemon's single AASession (header-only, host-tested). Wireless (TCP) and USB both go
// through startSession() in main.cpp, which asks mayStart() first:
// - a USB session is never replaced by a wireless connection (the phone on the cable wins; the wireless thread also
//   leaves the Bluetooth links alone while a USB phone is plugged in, so it is not offered the Wi-Fi either);
// - a wireless session is not interrupted by a phone being plugged in: USB waits until it ends;
// - a wireless connection may replace a wireless session (the phone reconnected before we noticed it left).

#include <pthread.h>

namespace nbt {

enum Transport { kTransportNone = 0, kTransportTcp = 1, kTransportUsb = 2 };

inline const char* transportName(Transport t) {
    return t == kTransportTcp ? "wireless" : t == kTransportUsb ? "USB" : "none";
}

inline bool mayStart(Transport current, Transport incoming) {
    if (incoming == kTransportNone) return false;
    if (current == kTransportNone) return true;
    return current == kTransportTcp && incoming == kTransportTcp;
}

// The current owner. Set by startSession() (serialized by its own mutex), cleared by the session-ended callback
// on the session thread, so it has a lock of its own (QNX GCC 4.4: no atomics).
class SessionOwner {
public:
    SessionOwner() : owner_(kTransportNone) { pthread_mutex_init(&mutex_, NULL); }
    ~SessionOwner() { pthread_mutex_destroy(&mutex_); }

    Transport get() const {
        pthread_mutex_t* m = const_cast<pthread_mutex_t*>(&mutex_);
        pthread_mutex_lock(m);
        Transport t = owner_;
        pthread_mutex_unlock(m);
        return t;
    }

    void set(Transport t) {
        pthread_mutex_lock(&mutex_);
        owner_ = t;
        pthread_mutex_unlock(&mutex_);
    }

private:
    SessionOwner(const SessionOwner&);
    SessionOwner& operator=(const SessionOwner&);

    pthread_mutex_t mutex_;
    Transport owner_;
};

} // namespace nbt

#endif // NBT_SESSION_OWNER_HPP
