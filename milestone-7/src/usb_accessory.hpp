// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef NBT_USB_ACCESSORY_HPP
#define NBT_USB_ACCESSORY_HPP

// Android Auto over USB (NBT_USB=1, experimental, not car-verified). Uses the head unit's own AOA drivers
// (see usb_aoap.hpp): a watcher thread triggers aoap-switch on /dev/android<bus><dev> with the Android Auto
// identity, then opens the /dev/oap<bus><dev> node the phone comes back on and bridges it to one end of a
// socketpair. The other end goes to the same ClientConnectedCallback as a TCP phone, so AASession runs unchanged.

#include "tcp_server.hpp"
#include <stddef.h>
#include <stdint.h>
#include <pthread.h>
#include <string>
#include <vector>

namespace nbt {

class UsbAccessory {
public:
    typedef bool (*BusyCallback)(void* user_data);   // true while any Android Auto session runs (TCP or USB)

    // dev_dir: where the drivers' nodes appear ("/dev"; host tests pass their own directory).
    UsbAccessory(ClientConnectedCallback callback, void* user_data, BusyCallback busy, void* busy_data,
                 const char* dev_dir = "/dev");
    ~UsbAccessory();

    bool start();
    void stop();   // ends the bridge too, which ends a USB session

private:
    static void* watchThreadStub(void* arg);
    void watchLoop();
    bool trigger(const char* path);
    bool startBridge(const std::string& node);
    void reapBridge(bool force);

    static void* readerThreadStub(void* arg);
    static void* writerThreadStub(void* arg);
    bool flag(const bool& f) const;
    void setFlag(bool& f, bool v);
    void readerLoop();   // /dev/oap -> socket
    void writerLoop();   // socket -> /dev/oap, one AA frame per bulk transfer

    std::string dev_dir_;
    ClientConnectedCallback client_callback_;
    void* client_user_data_;
    BusyCallback busy_;
    void* busy_data_;

    pthread_mutex_t flag_mutex_;   // guards is_running_, bridge_stop_, reader_done_, writer_done_
    pthread_t watch_thread_;
    bool is_running_;

    // One bridge at a time, owned by the watch thread (stop() takes it over after joining that thread).
    bool bridge_up_;
    std::string bridge_node_;
    int usb_fd_;
    int sock_fd_;
    pthread_t reader_thread_;
    pthread_t writer_thread_;
    bool bridge_stop_;
    bool reader_done_;
    bool writer_done_;
    uint64_t bytes_in_;    // read by the watch thread only after joining the bridge threads
    uint64_t bytes_out_;
    double bridge_start_s_;
    double bridge_end_s_;

    std::vector<std::string> triggered_;                    // android nodes we already sent the switch to
    std::vector<std::pair<std::string, int> > sessions_;    // oap node -> sessions started on it
};

} // namespace nbt

#endif // NBT_USB_ACCESSORY_HPP
