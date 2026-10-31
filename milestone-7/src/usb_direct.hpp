// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef NBT_USB_DIRECT_HPP
#define NBT_USB_DIRECT_HPP

// NBT_USB_DIRECT=1 (experimental, QNX only): talk to a phone in accessory mode through libusbdi instead of
// devc-serusb-aoap. That driver lets every client read /dev/oap*, and a transfer that completes while our read() is
// not waiting goes to whichever client reads next (the media app's A4A handler opens the node too), or is lost in the
// driver's own queue: TLS records went missing in every burst (car logs 2026-10-31). Here io-usb hands the bulk-in
// transfers to us only, with several reads queued so a burst always has somewhere to land. The driver has to go
// first (it keeps a read queued on the same endpoint): UsbAccessory stops it once the phone is in accessory mode.

#include <stddef.h>
#include <stdint.h>
#include <pthread.h>
#include <string>
#include <vector>

struct usbd_connection;
struct usbd_desc_node;
struct usbd_device;
struct usbd_pipe;
struct usbd_urb;

namespace nbt {

// Each completed bulk-in transfer, in the order the phone sent it. data == NULL: receiving ended (why() says why).
// Returns false to stop receiving. Called on io-usb's callback thread with the link's lock held: queue and return.
typedef bool (*UsbRxCallback)(const uint8_t* data, size_t n, void* user);

class UsbDirectLink;

class UsbDirect {
public:
    UsbDirect();
    ~UsbDirect();

    bool start();   // connects to io-usb; false (logged) when it refuses, and off QNX
    void stop();    // links must be closed first

    // Accessory devices (18d1:2d00-2d05) that io-usb reported at least min_age_s ago and that are still plugged in,
    // as stable keys.
    std::vector<std::string> accessories(double min_age_s = 0);

    // Attaches to accessory `key` and opens its bulk pipes. NULL on failure (logged).
    UsbDirectLink* open(const std::string& key);

    // Stops every devc-serusb-aoap process: SIGTERM, then SIGKILL if it is still there 2 s later (it does not exit
    // on SIGTERM while it holds a phone). Returns how many are still running. Call it about once a second.
    int stopAoapDriver();

    // libusbdi callbacks (no user pointer: one UsbDirect per process).
    void inserted(const void* instance);
    void removed(const void* instance);

private:
    struct Accessory {
        std::string key;
        std::vector<uint8_t> instance;   // usbd_device_instance_t
        double seen_s;                    // monotonic time of the insertion
        UsbDirectLink* link;              // attached, or NULL
    };
    struct Signalled {
        long pid;
        double term_s;
        bool killed;
    };
    UsbDirect(const UsbDirect&);
    UsbDirect& operator=(const UsbDirect&);
    friend class UsbDirectLink;
    void released(UsbDirectLink* link);   // link closed: the accessory can be opened again

    pthread_mutex_t mu_;
    struct usbd_connection* conn_;
    std::vector<Accessory> accs_;
    std::vector<Signalled> signalled_;   // watch thread only
};

class UsbDirectLink {
public:
    bool startRx(UsbRxCallback cb, void* user);
    bool write(const uint8_t* p, size_t n);   // one bulk-out transfer (+ a zero-length one when needed); blocks
    void abort();                             // ends receiving and a write in progress; callbacks stop
    const char* why();                        // why receiving ended
    void close();                             // after abort(): waits for the transfers, detaches; deletes this

private:
    friend class UsbDirect;
    struct RxSlot {
        UsbDirectLink* link;
        struct usbd_urb* urb;
        uint8_t* buf;
        uint64_t seq;
        bool busy;
    };
    struct Ready {
        uint64_t seq;
        std::vector<uint8_t> data;
        bool ok;
    };
    UsbDirectLink(UsbDirect* owner, const std::string& key, struct usbd_device* dev);
    ~UsbDirectLink();
    bool openPipes(unsigned config, unsigned iface, unsigned alternate);
    bool openFrom(struct usbd_desc_node* ifc, const char* how);   // the bulk in/out pair of one interface
    bool submit(RxSlot* s);                    // mu_ held
    void finish(const char* why);              // mu_ held: receiving ended
    void gone();                               // phone unplugged
    static void rxDone(struct usbd_urb* urb, struct usbd_pipe* pipe, void* handle);
    static void txDone(struct usbd_urb* urb, struct usbd_pipe* pipe, void* handle);
    bool transfer(const uint8_t* p, size_t n);

    UsbDirect* owner_;
    std::string key_;
    struct usbd_device* dev_;
    struct usbd_pipe* in_;
    struct usbd_pipe* out_;
    unsigned max_packet_out_;

    pthread_mutex_t mu_;
    pthread_cond_t cv_;
    UsbRxCallback cb_;
    void* user_;
    bool aborting_;
    bool ended_;                 // receiving ended (callback told)
    char why_[96];
    std::vector<RxSlot> slots_;
    unsigned rx_out_;            // bulk-in transfers queued in io-usb
    uint64_t next_submit_;
    uint64_t next_deliver_;
    std::vector<Ready> ready_;   // completed out of order (io-usb normally completes in order)

    struct usbd_urb* tx_urb_;
    uint8_t* tx_buf_;
    bool tx_busy_;
    bool tx_done_;
    bool tx_ok_;
    bool zlp_ok_;
};

} // namespace nbt

#endif // NBT_USB_DIRECT_HPP
