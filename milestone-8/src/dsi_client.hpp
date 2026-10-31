// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef DSI_CLIENT_HPP
#define DSI_CLIENT_HPP

#include <stdint.h>
#include <stddef.h>
#include <vector>

namespace nbt {

// Minimal DSI client built on libsys_dsi_colibry via dlopen (no vendor headers).
// Recipe: scratch/findings/re2_colibry.md section 6. All DSI callbacks, and every
// send(), must happen on the connection thread: i.e. inside the attached / data
// callbacks (send() is only valid there). Nothing here is verified on the car yet:
// every step logs, and any failure just returns false (daemon keeps running).
class DsiClient {
public:
    typedef void (*AttachedCb)(DsiClient* c, void* user);
    typedef void (*DataCb)(DsiClient* c, uint32_t type, uint32_t id, uint32_t seq,
                           const uint8_t* payload, uint32_t len, void* user);

    DsiClient();

    // interface/role map to "Role.Interface" (e.g. "SGBluetoothGap","BSS_GAP",35,0).
    bool open(const char* iface, const char* role, int major, int minor);
    void setCallbacks(AttachedCb a, DataCb d, void* user);

    // Valid only from within callbacks (connection thread).
    bool sendRequest(uint32_t requestId, const std::vector<uint8_t>& payload);
    bool setNotification(uint32_t attr);

    // payload helpers (DSI stream encodings: 4-byte aligned from payload start)
    static void putU32(std::vector<uint8_t>& v, uint32_t x);
    static void putString(std::vector<uint8_t>& v, const char* s);

    // trampolines (public: used from C callbacks)
    static void onAttachedTramp(void* self);
    static void onDetachedTramp(void* self);
    static void onDataTramp(void* self, void* istream);

    bool attached() const { return attached_; }
    // Quiet: do not hex-dump every received event (busy interfaces such as the MOST server).
    void setQuiet(bool q) { quiet_ = q; }

private:
    static void* threadMain(void* arg);

    void* lib_;
    void* conn_;
    char* cli_;
    volatile bool attached_;
    bool quiet_;
    AttachedCb attachedCb_;
    DataCb dataCb_;
    void* user_;
};

} // namespace nbt

#endif
