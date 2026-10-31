// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef BLUETOOTH_MANAGER_HPP
#define BLUETOOTH_MANAGER_HPP

#include <stdint.h>
#include <stddef.h>
#include <string>

namespace nbt {

// Wireless Android Auto service UUID the phone searches for in SDP.
static const char* const AA_SPP_NAME = "AndroidAuto";
static const char* const AA_BT_UUID = "4DE17A00-52CB-11E6-BDF4-0800200C9A66";

// Connectivity's DSI server for Bluetooth: broker name "BSS_GAP.SGBluetoothGap".
// Client side (Connectivity's own proxy) uses version 35.0.
// Request ids (type 0x100): SppUuidRegister = 46 (0x2e), SppUuidDeregister = 47 (0x2f).
// NOTE: 35 is EnabledServicesSet, NOT a UUID registration (old fork constant was wrong).
static const char* const DSI_SERVICE_BLUETOOTH_GAP = "SGBluetoothGap";
static const char* const DSI_ROLE_BLUETOOTH_GAP    = "BSS_GAP";
static const int         DSI_GAP_MAJOR = 35;
static const int         DSI_GAP_MINOR = 0;
static const uint32_t    DSI_REQ_SPP_UUID_REGISTER   = 0x2e;
static const uint32_t    DSI_REQ_SPP_UUID_DEREGISTER = 0x2f;
static const uint32_t    DSI_RSP_SPP_UUID_REGISTER   = 0x80000043; // payload: u32 GapErrorCode (0 = OK)

class BluetoothManager {
public:
    BluetoothManager();
    ~BluetoothManager();

    bool init();
    // Registers the AA UUID with Connectivity's Bluetooth service (non-blocking:
    // the request is sent from the DSI connection thread once attached).
    bool registerAndroidAutoSdp();
    // SppUuidDeregister + SppUuidRegister in place (what a daemon Restart does on the Bluetooth side), for a phone
    // that is connected over Bluetooth but never opens the Android Auto link. Call from the wireless thread while
    // no link or session is up; skipped (false) while a registration is still in flight. `why` is only logged.
    bool reregister(const char* why);
    // After a handshake that already had /dev/btspp open and the phone closed without accepting
    // the Wi-Fi start. Uses the same deregister-then-register path as reregister(), on the caller
    // (the wireless thread), and is skipped while a registration is already in flight.
    void requestSppBounce();
    void stop();

private:
    bool is_registered_;
};

// Phone-opened vs car-opened Bluetooth serial links (src/spp_direction.hpp). The Bluetooth thread feeds the GAP
// ServiceRequest informs in, the wireless thread asks when a /dev/btsppN node appears. Thread-safe.
// mode: 0 off (no informs watched), 1 always leave car-opened links alone, 2 learn first (BMW/MINI default).
void sppDirConfigure(int mode, uint32_t windowS, int model);
int sppDirMode();
bool sppDirTakePhoneOpened(std::string* service);   // true = a phone ServiceRequest came in the last windowS seconds
int sppDirLinkResult(bool phoneOpened, bool spoke);  // new model (SppDirection::Model) when it changed, else -1
bool sppDirGate();                                   // leave car-opened links alone now?
int sppDirModel();
unsigned sppDirRequests();

} // namespace nbt

#endif // BLUETOOTH_MANAGER_HPP
