// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef NBT_USB_ACCESSORY_HPP
#define NBT_USB_ACCESSORY_HPP

// Android Auto over USB (NBT_USB=1, experimental, not car-verified). Uses the head unit's own AOA drivers
// (see usb_aoap.hpp): a watcher thread triggers aoap-switch on /dev/android<bus><dev> with the Android Auto
// identity, then opens the /dev/oap<bus><dev> node the phone comes back on and bridges it to one end of a
// socketpair. The other end goes to the ClientConnectedCallback (startSession() in main.cpp), so AASession runs
// unchanged.

#include "session_owner.hpp"
#include "tcp_server.hpp"
#include <stddef.h>
#include <stdint.h>
#include <pthread.h>
#include <string>
#include <vector>

namespace nbt {

struct UsbBridge;   // one /dev/oap <-> socketpair bridge and its two threads (usb_accessory.cpp)

class UsbAccessory {
public:
    // Who owns the AA session right now. kTransportNone: a USB session may start. Anything but kTransportUsb while
    // a bridge is up: that bridge's session is over (also when its threads are stuck on a silent phone).
    typedef Transport (*OwnerCallback)(void* user_data);

    // dev_dir: where the drivers' nodes appear ("/dev"; host tests pass their own directory).
    UsbAccessory(ClientConnectedCallback callback, void* user_data, OwnerCallback owner, void* owner_data,
                 const char* dev_dir = "/dev");
    ~UsbAccessory();

    bool start();
    void stop();   // ends the bridge too, which ends a USB session

    // A phone is on the cable: an aoap-switch node (AOA phone, switched or not yet) or an accessory node exists.
    // The wireless thread leaves the Bluetooth links alone meanwhile. Updated every scan (500 ms).
    bool phonePresent() const { return flag(phone_present_); }

private:
    struct SwitchNode {         // a /dev/android* node
        std::string name;
        int tries;
        double next_try_s;
        bool done;              // switched, left to another client, or given up
    };
    struct AccessoryNode {      // a /dev/oap* node
        std::string name;
        int sessions;           // sessions actually started on this plug-in
        int open_fails;         // in a row
        double next_try_s;
    };
    enum TriggerResult { kTriggerSwitched, kTriggerOtherClient, kTriggerFailed };
    enum BridgeResult { kBridgeStarted, kBridgeOpenFailed, kBridgeFailed, kBridgeLeaked };

    static void* watchThreadStub(void* arg);
    void watchLoop();
    TriggerResult trigger(const char* path);
    BridgeResult startBridge(const std::string& node);
    bool reapBridge(bool force);   // false: a stuck bridge was abandoned (force only)
    AccessoryNode* findNode(const std::string& name);

    bool flag(const bool& f) const;
    void setFlag(bool& f, bool v);

    std::string dev_dir_;
    ClientConnectedCallback client_callback_;
    void* client_user_data_;
    OwnerCallback owner_;
    void* owner_data_;

    pthread_mutex_t flag_mutex_;   // guards is_running_ and phone_present_
    pthread_t watch_thread_;
    bool is_running_;
    bool phone_present_;

    // Owned by the watch thread (stop() takes them over after joining it).
    UsbBridge* bridge_;            // NULL while no bridge runs
    int not_ours_ticks_;           // scans in a row the session owner was not USB while the bridge was up
    std::vector<SwitchNode> switches_;
    std::vector<AccessoryNode> nodes_;
};

} // namespace nbt

#endif // NBT_USB_ACCESSORY_HPP
