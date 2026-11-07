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
#include "usb_aoap.hpp"
#include <stddef.h>
#include <stdint.h>
#include <pthread.h>
#include <string>
#include <vector>

namespace nbt {

struct UsbBridge;   // one /dev/oap <-> socketpair bridge and its threads (usb_accessory.cpp)
class UsbDirect;    // NBT_USB_DIRECT: the phone through libusbdi instead of /dev/oap (usb_direct.hpp)

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

    // A phone on the cable that USB still means to connect: a session runs, or an aoap-switch or accessory node is
    // still to be switched or opened. A node USB gave up on, or left to another client, does not count, so it does
    // not keep wireless off until the phone is unplugged. The wireless thread leaves the Bluetooth links alone while
    // this is true. Updated every scan (500 ms).
    bool phonePresent() const { return flag(phone_present_); }

    // Any thread: a USB session ended without a frame from the phone being decrypted. stale: it sent frames we could
    // not decrypt, so it is still in an earlier session; else it stayed silent, which a phone that is still starting
    // Android Auto does too. The phone is reset (NBT_USB_DIRECT; through /dev/oap* it can only be tried again).
    void noteSessionFailed(bool stale);

private:
    struct SwitchNode {         // a /dev/android* node
        std::string name;
        int tries;
        double next_try_s;
        bool done;              // switched, left to another client, or given up
        bool switched;          // done by our switch: the phone comes back as an accessory
        double done_s;
    };
    struct AccessoryNode {      // a /dev/oap* node
        std::string name;
        int sessions;           // sessions actually started on this plug-in
        int open_fails;         // in a row
        double next_try_s;
        int silent_sessions;    // sessions the phone did not answer, on this plug-in
    };
    enum TriggerResult { kTriggerSwitched, kTriggerOtherClient, kTriggerFailed };
    enum BridgeResult { kBridgeStarted, kBridgeOpenFailed, kBridgeFailed, kBridgeLeaked };

    static void* watchThreadStub(void* arg);
    void watchLoop();
    TriggerResult trigger(const char* path);
    BridgeResult startBridge(const std::string& node);   // node: an oap* name, or a UsbDirect key
    bool reapBridge(bool force);   // false: a stuck bridge was abandoned (force only)
    AccessoryNode* findNode(const std::string& name);
    bool usbWantsPhone(const std::vector<std::string>& androids, const std::vector<std::string>& accs,
                       bool accessory_waiting, double now);
    void handleFailedSession();
    bool resetPhone(const std::string& key, const char* why);   // false: not reset (logged), carry on without it

    bool flag(const bool& f) const;
    void setFlag(bool& f, bool v);

    std::string dev_dir_;
    ClientConnectedCallback client_callback_;
    void* client_user_data_;
    OwnerCallback owner_;
    void* owner_data_;

    pthread_mutex_t flag_mutex_;   // guards is_running_, phone_present_, session_failed_ and session_stale_
    pthread_t watch_thread_;
    bool is_running_;
    bool phone_present_;
    bool session_failed_;          // noteSessionFailed(): for the watch thread
    bool session_stale_;

    // Owned by the watch thread (stop() takes them over after joining it).
    UsbBridge* bridge_;            // NULL while no bridge runs
    std::string last_node_;        // the node of the last bridge: the session that failed ran on it
    aoap::ResetBudget resets_;
    int not_ours_ticks_;           // scans in a row the session owner was not USB while the bridge was up
    UsbDirect* direct_;            // set from start() to stop() when NBT_USB_DIRECT is on and io-usb answered
    double next_driver_stop_s_;    // NBT_USB_DIRECT: devc-serusb-aoap is signalled at most once a second
    int direct_drivers_;           // devc-serusb-aoap processes still running at the last check
    bool direct_leaked_;           // an abandoned bridge kept its UsbDirectLink: direct_ stays alive
    std::vector<SwitchNode> switches_;
    std::vector<AccessoryNode> nodes_;
};

} // namespace nbt

#endif // NBT_USB_ACCESSORY_HPP
