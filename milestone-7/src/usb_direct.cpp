// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "usb_direct.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#ifdef __QNX__
#include <sys/usbdi.h>
#include <sys/procfs.h>
#include <devctl.h>
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <unistd.h>
#endif

namespace nbt {

#ifdef __QNX__

static const uint32_t kGoogleVendor = 0x18d1;
static const uint32_t kFirstAccessoryPid = 0x2d00;   // accessory, + adb, + audio, ... (AOA 1 and 2)
static const uint32_t kLastAccessoryPid = 0x2d05;
static const unsigned kRxSlots = 8;                   // bulk-in transfers kept queued in io-usb
static const size_t kRxBuf = 16384;                   // like devc-serusb-aoap -l 16384; a longer transfer continues
                                                      // in the next one (a stream to us)
static const size_t kTxBuf = 65536;

static UsbDirect* g_direct = NULL;   // libusbdi's insertion/removal callbacks carry no user pointer

static void onInsert(struct usbd_connection*, usbd_device_instance_t* inst) {
    if (g_direct) g_direct->inserted(inst);
}

static void onRemove(struct usbd_connection*, usbd_device_instance_t* inst) {
    if (g_direct) g_direct->removed(inst);
}

static std::string keyOf(const usbd_device_instance_t* inst) {
    char k[32];
    snprintf(k, sizeof(k), "usb%u.%u.%u", (unsigned)inst->path, (unsigned)inst->devno, (unsigned)inst->generation);
    return k;
}

static double monoS() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void deadline(struct timespec& ts, unsigned ms) {
    clock_gettime(CLOCK_REALTIME, &ts);   // pthread_cond_timedwait's default clock
    ts.tv_sec += ms / 1000;
    ts.tv_nsec += (long)(ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec += 1;
        ts.tv_nsec -= 1000000000L;
    }
}

UsbDirect::UsbDirect() : conn_(NULL) {
    pthread_mutex_init(&mu_, NULL);
}

UsbDirect::~UsbDirect() {
    stop();
    pthread_mutex_destroy(&mu_);
}

bool UsbDirect::start() {
    if (conn_) return true;
    if (g_direct && g_direct != this) return false;
    g_direct = this;
    static usbd_device_ident_t ident = { kGoogleVendor, USBD_CONNECT_WILDCARD, USBD_CONNECT_WILDCARD,
                                         USBD_CONNECT_WILDCARD, USBD_CONNECT_WILDCARD };
    static usbd_funcs_t funcs = { _USBDI_NFUNCS, onInsert, onRemove, NULL };
    usbd_connect_parm_t parm;
    memset(&parm, 0, sizeof(parm));
    parm.vusb = USB_VERSION;
    parm.vusbd = USBD_VERSION;
    parm.ident = &ident;
    parm.funcs = &funcs;
    parm.connect_wait = USBD_CONNECT_WAIT;
    int r = usbd_connect(&parm, &conn_);
    if (r != EOK) {
        printf("[UsbDirect] usbd_connect failed: %s\n", strerror(r));
        conn_ = NULL;
        g_direct = NULL;
        return false;
    }
    printf("[UsbDirect] connected to io-usb: phones in accessory mode are read through libusbdi (NBT_USB_DIRECT)\n");
    return true;
}

void UsbDirect::stop() {
    if (!conn_) return;
    usbd_disconnect(conn_);
    conn_ = NULL;
    pthread_mutex_lock(&mu_);
    accs_.clear();
    pthread_mutex_unlock(&mu_);
    if (g_direct == this) g_direct = NULL;
}

void UsbDirect::inserted(const void* p) {
    const usbd_device_instance_t* inst = reinterpret_cast<const usbd_device_instance_t*>(p);
    if (inst->ident.vendor != kGoogleVendor || inst->ident.device < kFirstAccessoryPid ||
        inst->ident.device > kLastAccessoryPid)
        return;
    std::string key = keyOf(inst);
    pthread_mutex_lock(&mu_);
    bool known = false;
    for (size_t i = 0; i < accs_.size(); ++i) known = known || accs_[i].key == key;   // one entry per device
    if (!known) {
        Accessory a;
        a.key = key;
        a.instance.assign(reinterpret_cast<const uint8_t*>(inst),
                          reinterpret_cast<const uint8_t*>(inst) + sizeof(*inst));
        a.seen_s = monoS();
        a.link = NULL;
        accs_.push_back(a);
    }
    pthread_mutex_unlock(&mu_);
    if (!known)
        printf("[UsbDirect] accessory %04x:%04x at %s (interface %u)\n", (unsigned)inst->ident.vendor,
               (unsigned)inst->ident.device, key.c_str(), (unsigned)inst->iface);
}

void UsbDirect::removed(const void* p) {
    const usbd_device_instance_t* inst = reinterpret_cast<const usbd_device_instance_t*>(p);
    std::string key = keyOf(inst);
    pthread_mutex_lock(&mu_);
    for (size_t i = accs_.size(); i-- > 0;) {
        if (accs_[i].key != key) continue;
        printf("[UsbDirect] accessory %s removed\n", key.c_str());
        if (accs_[i].link) accs_[i].link->gone();   // lock order: UsbDirect::mu_, then the link's
        accs_.erase(accs_.begin() + i);
    }
    pthread_mutex_unlock(&mu_);
}

std::vector<std::string> UsbDirect::accessories(double min_age_s) {
    std::vector<std::string> keys;
    const double now = monoS();
    pthread_mutex_lock(&mu_);
    for (size_t i = 0; i < accs_.size(); ++i) {
        if (accs_[i].link || now - accs_[i].seen_s >= min_age_s) keys.push_back(accs_[i].key);
    }
    pthread_mutex_unlock(&mu_);
    return keys;
}

UsbDirectLink* UsbDirect::open(const std::string& key) {
    if (!conn_) return NULL;
    usbd_device_instance_t inst;
    bool found = false;
    pthread_mutex_lock(&mu_);
    for (size_t i = 0; i < accs_.size() && !found; ++i) {
        if (accs_[i].key == key && !accs_[i].link) {
            memcpy(&inst, &accs_[i].instance[0], sizeof(inst));
            found = true;
        }
    }
    pthread_mutex_unlock(&mu_);
    if (!found) return NULL;

    // Not under mu_: io-usb's callback thread may need it meanwhile.
    struct usbd_device* dev = NULL;
    int r = usbd_attach(conn_, &inst, 0, &dev);
    if (r != EOK) {
        printf("[UsbDirect] %s: usbd_attach failed: %s\n", key.c_str(), strerror(r));
        return NULL;
    }
    UsbDirectLink* link = new UsbDirectLink(this, key, dev);
    if (!link->openPipes(inst.config, inst.iface, inst.alternate)) {
        delete link;   // detaches
        return NULL;
    }
    bool still = false;
    pthread_mutex_lock(&mu_);
    for (size_t i = 0; i < accs_.size() && !still; ++i) {
        if (accs_[i].key == key && !accs_[i].link) {
            accs_[i].link = link;
            still = true;
        }
    }
    pthread_mutex_unlock(&mu_);
    if (!still) {
        printf("[UsbDirect] %s: unplugged while it was being opened\n", key.c_str());
        delete link;
        return NULL;
    }
    return link;
}

void UsbDirect::released(UsbDirectLink* link) {
    pthread_mutex_lock(&mu_);
    for (size_t i = 0; i < accs_.size(); ++i) {
        if (accs_[i].link == link) accs_[i].link = NULL;
    }
    pthread_mutex_unlock(&mu_);
}

int UsbDirect::stopAoapDriver() {
    std::vector<long> pids;
    DIR* d = opendir("/proc");
    if (!d) return 0;
    struct dirent* e;
    while ((e = readdir(d)) != NULL) {
        char* end = NULL;
        long pid = strtol(e->d_name, &end, 10);
        if (pid <= 1 || !end || *end) continue;
        char path[64];
        snprintf(path, sizeof(path), "/proc/%ld/as", pid);
        int fd = ::open(path, O_RDONLY);
        if (fd < 0) continue;
        struct {
            procfs_debuginfo info;
            char buf[PATH_MAX];
        } name;
        memset(&name, 0, sizeof(name));
        int r = devctl(fd, DCMD_PROC_MAPDEBUG_BASE, &name, sizeof(name), NULL);
        ::close(fd);
        if (r != EOK) continue;
        const char* base = strrchr(name.info.path, '/');
        base = base ? base + 1 : name.info.path;
        if (strcmp(base, "devc-serusb-aoap") == 0) pids.push_back(pid);
    }
    closedir(d);

    const double now = monoS();
    for (size_t i = signalled_.size(); i-- > 0;) {   // forget the ones that are gone
        bool alive = false;
        for (size_t j = 0; j < pids.size(); ++j) alive = alive || pids[j] == signalled_[i].pid;
        if (!alive) {
            printf("[UsbDirect] devc-serusb-aoap (pid %ld) has exited: only nbt-aad reads the phone now\n",
                   signalled_[i].pid);
            signalled_.erase(signalled_.begin() + i);
        }
    }
    for (size_t j = 0; j < pids.size(); ++j) {
        Signalled* s = NULL;
        for (size_t i = 0; i < signalled_.size(); ++i) {
            if (signalled_[i].pid == pids[j]) s = &signalled_[i];
        }
        if (!s) {
            kill((pid_t)pids[j], SIGTERM);
            printf("[UsbDirect] stopping devc-serusb-aoap (pid %ld) so it does not read the phone alongside us\n",
                   pids[j]);
            Signalled n = { pids[j], now, false };
            signalled_.push_back(n);
        } else if (!s->killed && now - s->term_s >= 2.0) {
            kill((pid_t)pids[j], SIGKILL);
            printf("[UsbDirect] devc-serusb-aoap (pid %ld) is still there 2 s after SIGTERM: SIGKILL\n", pids[j]);
            s->killed = true;
        }
    }
    return (int)pids.size();
}

UsbDirectLink::UsbDirectLink(UsbDirect* owner, const std::string& key, struct usbd_device* dev)
    : owner_(owner), key_(key), dev_(dev), in_(NULL), out_(NULL), max_packet_out_(512), cb_(NULL), user_(NULL),
      aborting_(false), ended_(false), rx_out_(0), next_submit_(0), next_deliver_(0), tx_urb_(NULL), tx_buf_(NULL),
      tx_busy_(false), tx_done_(false), tx_ok_(false), zlp_ok_(true) {
    why_[0] = 0;
    pthread_mutex_init(&mu_, NULL);
    pthread_cond_init(&cv_, NULL);
}

// Only reached with nothing queued in io-usb (close() leaks the link otherwise).
UsbDirectLink::~UsbDirectLink() {
    for (size_t i = 0; i < slots_.size(); ++i) {
        if (slots_[i].urb) usbd_free_urb(slots_[i].urb);
        if (slots_[i].buf) usbd_free(slots_[i].buf);
    }
    if (tx_urb_) usbd_free_urb(tx_urb_);
    if (tx_buf_) usbd_free(tx_buf_);
    if (in_) usbd_close_pipe(in_);
    if (out_) usbd_close_pipe(out_);
    if (dev_) usbd_detach(dev_);
    pthread_cond_destroy(&cv_);
    pthread_mutex_destroy(&mu_);
}

bool UsbDirectLink::openFrom(struct usbd_desc_node* ifc, const char* how) {
    usbd_descriptors_t* bin = NULL;
    usbd_descriptors_t* bout = NULL;
    usbd_descriptors_t* ed;
    struct usbd_desc_node* ep = NULL;
    int eps = 0;
    for (int k = 0; (ed = usbd_parse_descriptors(dev_, ifc, USB_DESC_ENDPOINT, k, &ep)); ++k) {
        ++eps;
        if ((ed->endpoint.bmAttributes & 0x03) != USB_ATTRIB_BULK) continue;
        if (ed->endpoint.bEndpointAddress & USB_ENDPOINT_IN) bin = ed;
        else bout = ed;
    }
    if (!bin || !bout) {
        printf("[UsbDirect] %s: %s: %d endpoints, no bulk in/out pair\n", key_.c_str(), how, eps);
        return false;
    }
    int ri = usbd_open_pipe(dev_, bin, &in_);
    int ro = usbd_open_pipe(dev_, bout, &out_);
    if (ri != EOK || ro != EOK) {
        printf("[UsbDirect] %s: opening the bulk pipes failed: in %s, out %s\n", key_.c_str(), strerror(ri),
               strerror(ro));
        if (ri == EOK) usbd_close_pipe(in_);
        if (ro == EOK) usbd_close_pipe(out_);
        in_ = out_ = NULL;
        return false;
    }
    max_packet_out_ = bout->endpoint.wMaxPacketSize ? bout->endpoint.wMaxPacketSize : 512;
    printf("[UsbDirect] %s: %s: bulk in 0x%02x / out 0x%02x (max packet %u / %u)\n", key_.c_str(), how,
           (unsigned)bin->endpoint.bEndpointAddress, (unsigned)bout->endpoint.bEndpointAddress,
           (unsigned)bin->endpoint.wMaxPacketSize, max_packet_out_);
    return true;
}

// The interface io-usb reported first (what devc-serusb-aoap uses), then any non-adb interface of the device.
bool UsbDirectLink::openPipes(unsigned config, unsigned iface, unsigned alternate) {
    struct usbd_desc_node* ifc = NULL;
    char how[64];
    snprintf(how, sizeof(how), "config %u interface %u", config, iface);
    if (usbd_interface_descriptor(dev_, config, iface, alternate, &ifc) && openFrom(ifc, how)) {
        // done
    } else {
        struct usbd_desc_node* dn = NULL;
        struct usbd_desc_node* cn = NULL;
        usbd_descriptors_t* ifd;
        if (!usbd_parse_descriptors(dev_, NULL, USB_DESC_DEVICE, 0, &dn) ||
            !usbd_parse_descriptors(dev_, dn, USB_DESC_CONFIGURATION, 0, &cn)) {
            printf("[UsbDirect] %s: no device / configuration descriptor\n", key_.c_str());
            return false;
        }
        for (int i = 0; !in_ && (ifd = usbd_parse_descriptors(dev_, cn, USB_DESC_INTERFACE, i, &ifc)); ++i) {
            snprintf(how, sizeof(how), "interface %u (class %02x/%02x)", (unsigned)ifd->interface.bInterfaceNumber,
                     (unsigned)ifd->interface.bInterfaceClass, (unsigned)ifd->interface.bInterfaceSubClass);
            if (ifd->interface.bInterfaceSubClass == 0x42) continue;   // the adb interface of 2d01 / 2d03 / 2d05
            openFrom(ifc, how);
        }
    }
    if (!(in_ && out_)) {
        printf("[UsbDirect] %s: no bulk in/out endpoint pair found\n", key_.c_str());
        return false;
    }
    slots_.resize(kRxSlots);
    for (size_t i = 0; i < slots_.size(); ++i) {
        RxSlot& s = slots_[i];
        s.link = this;
        s.urb = usbd_alloc_urb(NULL);
        s.buf = reinterpret_cast<uint8_t*>(usbd_alloc(kRxBuf));
        s.seq = 0;
        s.busy = false;
        if (!s.urb || !s.buf) {
            printf("[UsbDirect] %s: out of USB transfer memory\n", key_.c_str());
            return false;
        }
    }
    tx_urb_ = usbd_alloc_urb(NULL);
    tx_buf_ = reinterpret_cast<uint8_t*>(usbd_alloc(kTxBuf));
    if (!tx_urb_ || !tx_buf_) {
        printf("[UsbDirect] %s: out of USB transfer memory\n", key_.c_str());
        return false;
    }
    return true;
}

bool UsbDirectLink::submit(RxSlot* s) {
    s->seq = next_submit_++;
    usbd_setup_bulk(s->urb, URB_DIR_IN | URB_SHORT_XFER_OK, s->buf, kRxBuf);
    int r = usbd_io(s->urb, in_, &UsbDirectLink::rxDone, s, USBD_TIME_INFINITY);
    if (r != EOK) {
        --next_submit_;
        snprintf(why_, sizeof(why_), "queueing a USB read failed: %s", strerror(r));
        return false;
    }
    s->busy = true;
    ++rx_out_;
    return true;
}

bool UsbDirectLink::startRx(UsbRxCallback cb, void* user) {
    pthread_mutex_lock(&mu_);
    cb_ = cb;
    user_ = user;
    bool ok = true;
    for (size_t i = 0; i < slots_.size() && ok; ++i) ok = submit(&slots_[i]);
    pthread_mutex_unlock(&mu_);
    if (!ok) printf("[UsbDirect] %s: %s\n", key_.c_str(), why_);
    return ok;
}

void UsbDirectLink::finish(const char* why) {
    if (ended_) return;
    ended_ = true;
    if (!why_[0]) snprintf(why_, sizeof(why_), "%s", why);
    if (cb_) cb_(NULL, 0, user_);
}

void UsbDirectLink::rxDone(struct usbd_urb* urb, struct usbd_pipe*, void* handle) {
    RxSlot* s = reinterpret_cast<RxSlot*>(handle);
    UsbDirectLink* l = s->link;
    _Uint32t status = 0, len = 0;
    usbd_urb_status(urb, &status, &len);
    pthread_mutex_lock(&l->mu_);
    s->busy = false;
    --l->rx_out_;
    if (!l->aborting_ && !l->ended_) {
        Ready r;
        r.seq = s->seq;
        r.ok = (status & USBD_URB_STATUS_MASK) == USBD_STATUS_CMP;
        if (r.ok) {
            r.data.assign(s->buf, s->buf + len);
        } else {
            unsigned us = status & USBD_USB_STATUS_MASK;
            snprintf(l->why_, sizeof(l->why_), "USB read ended: status 0x%08x%s", (unsigned)status,
                     us == USBD_STATUS_DEV_NOANSWER ? " (phone gone)" : us == USBD_STATUS_STALL ? " (stall)" : "");
        }
        l->ready_.push_back(r);
        // Hand transfers over in the order they were queued (io-usb completes them in order on one pipe).
        bool progress = true;
        while (progress && !l->ended_) {
            progress = false;
            for (size_t i = 0; i < l->ready_.size(); ++i) {
                if (l->ready_[i].seq != l->next_deliver_) continue;
                Ready t;
                t.data.swap(l->ready_[i].data);
                t.ok = l->ready_[i].ok;
                l->ready_.erase(l->ready_.begin() + i);
                ++l->next_deliver_;
                progress = true;
                if (!t.ok) l->finish("USB read failed");
                else if (!t.data.empty() && !l->cb_(&t.data[0], t.data.size(), l->user_))
                    l->finish("session not reading");
                break;
            }
        }
        if (r.ok && !l->ended_ && !l->submit(s)) l->finish("queueing a USB read failed");
    }
    pthread_cond_broadcast(&l->cv_);
    pthread_mutex_unlock(&l->mu_);
}

void UsbDirectLink::txDone(struct usbd_urb* urb, struct usbd_pipe*, void* handle) {
    UsbDirectLink* l = reinterpret_cast<UsbDirectLink*>(handle);
    _Uint32t status = 0, len = 0;
    usbd_urb_status(urb, &status, &len);
    pthread_mutex_lock(&l->mu_);
    l->tx_ok_ = (status & USBD_URB_STATUS_MASK) == USBD_STATUS_CMP;
    l->tx_done_ = true;
    pthread_cond_broadcast(&l->cv_);
    pthread_mutex_unlock(&l->mu_);
}

bool UsbDirectLink::transfer(const uint8_t* p, size_t n) {
    pthread_mutex_lock(&mu_);
    if (aborting_ || ended_ || tx_busy_) {
        pthread_mutex_unlock(&mu_);
        return false;
    }
    tx_busy_ = true;
    tx_done_ = false;
    pthread_mutex_unlock(&mu_);
    if (n) memcpy(tx_buf_, p, n);
    usbd_setup_bulk(tx_urb_, URB_DIR_OUT, tx_buf_, n);
    int r = usbd_io(tx_urb_, out_, &UsbDirectLink::txDone, this, USBD_TIME_INFINITY);
    pthread_mutex_lock(&mu_);
    if (r != EOK) {
        tx_busy_ = false;
        pthread_mutex_unlock(&mu_);
        printf("[UsbDirect] %s: USB write of %u bytes failed: %s\n", key_.c_str(), (unsigned)n, strerror(r));
        return false;
    }
    // abort() makes io-usb complete it; the wait is bounded anyway so a lost completion cannot hang the writer.
    unsigned waited_ms = 0;
    while (!tx_done_ && !(aborting_ && waited_ms >= 2000)) {
        struct timespec ts;
        deadline(ts, 100);
        pthread_cond_timedwait(&cv_, &mu_, &ts);
        if (aborting_) waited_ms += 100;
    }
    bool ok = tx_done_ && tx_ok_;
    if (tx_done_) tx_busy_ = false;   // else the URB stays io-usb's: close() leaks the link rather than free it
    pthread_mutex_unlock(&mu_);
    return ok;
}

bool UsbDirectLink::write(const uint8_t* p, size_t n) {
    while (n > 0) {
        size_t c = n < kTxBuf ? n : kTxBuf;
        if (!transfer(p, c)) return false;
        p += c;
        n -= c;
        // A transfer that is a whole number of packets needs a zero-length one after it, or the phone's read waits
        // for more.
        if (n == 0 && c % max_packet_out_ == 0 && zlp_ok_ && !transfer(p, 0)) {
            pthread_mutex_lock(&mu_);
            bool stopping = aborting_ || ended_;
            pthread_mutex_unlock(&mu_);
            if (stopping) return false;
            zlp_ok_ = false;
            printf("[UsbDirect] %s: zero-length USB write refused: not sending them any more\n", key_.c_str());
        }
    }
    return true;
}

void UsbDirectLink::abort() {
    pthread_mutex_lock(&mu_);
    bool first = !aborting_;
    aborting_ = true;
    pthread_mutex_unlock(&mu_);
    if (!first) return;
    // Not under mu_: io-usb may complete the transfers (and call rxDone / txDone) right here.
    usbd_abort_pipe(in_);
    usbd_abort_pipe(out_);
    pthread_mutex_lock(&mu_);
    pthread_cond_broadcast(&cv_);
    pthread_mutex_unlock(&mu_);
}

void UsbDirectLink::gone() {
    pthread_mutex_lock(&mu_);
    snprintf(why_, sizeof(why_), "phone unplugged");
    finish("phone unplugged");
    pthread_mutex_unlock(&mu_);
}

const char* UsbDirectLink::why() {
    return why_[0] ? why_ : "stopped";
}

void UsbDirectLink::close() {
    abort();
    pthread_mutex_lock(&mu_);
    for (unsigned waited = 0; (rx_out_ || tx_busy_) && waited < 2000; waited += 100) {
        struct timespec ts;
        deadline(ts, 100);
        pthread_cond_timedwait(&cv_, &mu_, &ts);
    }
    bool drained = !rx_out_ && !tx_busy_;
    unsigned left = rx_out_;
    pthread_mutex_unlock(&mu_);
    if (!drained) {
        // io-usb still owns some transfers: freeing them could crash in a late callback. Keep it all, and keep the
        // accessory marked as in use until the phone is unplugged.
        printf("[UsbDirect] %s: %u USB transfers did not complete after the abort: leaving them until the phone is "
               "replugged\n", key_.c_str(), left);
        return;
    }
    owner_->released(this);
    delete this;
}

#else   // host builds (tests): there is no io-usb

UsbDirect::UsbDirect() : conn_(NULL) { pthread_mutex_init(&mu_, NULL); }
UsbDirect::~UsbDirect() { pthread_mutex_destroy(&mu_); }
bool UsbDirect::start() {
    printf("[UsbDirect] needs QNX io-usb: not available here\n");
    return false;
}
void UsbDirect::stop() {}
std::vector<std::string> UsbDirect::accessories(double) { return std::vector<std::string>(); }
UsbDirectLink* UsbDirect::open(const std::string&) { return NULL; }
int UsbDirect::stopAoapDriver() { return 0; }
void UsbDirect::inserted(const void*) {}
void UsbDirect::removed(const void*) {}
void UsbDirect::released(UsbDirectLink*) {}

bool UsbDirectLink::startRx(UsbRxCallback, void*) { return false; }
bool UsbDirectLink::write(const uint8_t*, size_t) { return false; }
void UsbDirectLink::abort() {}
const char* UsbDirectLink::why() { return "no io-usb"; }
void UsbDirectLink::close() {}

#endif

} // namespace nbt
