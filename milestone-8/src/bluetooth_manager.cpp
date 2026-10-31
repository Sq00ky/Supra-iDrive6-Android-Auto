// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "bluetooth_manager.hpp"
#include "dsi_client.hpp"
#include "gap_parse.hpp"
#include "spp_direction.hpp"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <string>
#include <vector>

namespace nbt {

namespace {
DsiClient g_gap;

// GapErrorCode values (libbssservice.so enum order): 0 NONE, 1 TIMEOUT, 2 BUSY,
// 3 STATE, 4 REFUSED, 5 DEFAULT, 6 INVALID_PARAM. DEFAULT is what the stack
// returns for "service name/uuid already exists", e.g. a registration left
// behind by an earlier nbt-aad run (we never deregister at shutdown).
const char* gapErrorName(uint32_t e) {
    switch (e) {
    case 0: return "OK";
    case 1: return "TIMEOUT";
    case 2: return "BUSY";
    case 3: return "STATE";
    case 4: return "REFUSED";
    case 5: return "DEFAULT (name/uuid already registered?)";
    case 6: return "INVALID_PARAM";
    default: return "FAILED";
    }
}

enum SppState { SPP_IDLE, SPP_REG_SENT, SPP_DEREG_SENT, SPP_REREG_SENT, SPP_DONE };
volatile SppState g_spp = SPP_IDLE;
volatile bool g_periodic = false;      // current deregister/register pair was started by reregister()
volatile bool g_txBusy = false;        // reregister() is inside sendRequest on the wireless thread
uint32_t g_stateSec = 0;               // when g_spp last changed (monotonic s)
pthread_mutex_t g_stateMx = PTHREAD_MUTEX_INITIALIZER;

uint32_t monoSec() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)ts.tv_sec;
}

void setState(SppState s) {
    pthread_mutex_lock(&g_stateMx);
    g_spp = s;
    g_stateSec = monoSec();
    pthread_mutex_unlock(&g_stateMx);
}

// Sends from the connection thread (response callbacks) wait for a send that reregister() started on the wireless
// thread and that is still returning, so two threads never write this client's stream at once. Bounded: 200 ms.
void waitTxIdle() {
    for (int i = 0; g_txBusy && i < 200; ++i) usleep(1000);
}

void sendRegister(DsiClient* c) {
    const char* uuidCon = getenv("NBT_SPP_UUID_CON");
    if (!uuidCon || !*uuidCon) uuidCon = AA_BT_UUID;   // connect UUID: same as service UUID (INFERRED)
    std::vector<uint8_t> p;
    DsiClient::putString(p, AA_SPP_NAME);
    DsiClient::putString(p, AA_BT_UUID);
    DsiClient::putString(p, uuidCon);
    DsiClient::putU32(p, 0);   // type 0 = DevA (inferred)
    printf("[BluetoothManager] SppUuidRegister(name='%s', uuid=%s, con=%s, type=0)\n",
           AA_SPP_NAME, AA_BT_UUID, uuidCon);
    waitTxIdle();
    c->sendRequest(DSI_REQ_SPP_UUID_REGISTER, p);
}

void sendDeregister(DsiClient* c, const char* why) {
    std::vector<uint8_t> p;
    DsiClient::putString(p, AA_SPP_NAME);   // libdsiservice: deregister takes the service name only
    printf("[BluetoothManager] SppUuidDeregister(name='%s'): %s\n", AA_SPP_NAME, why);
    c->sendRequest(DSI_REQ_SPP_UUID_DEREGISTER, p);
}

uint32_t readErr(const uint8_t* pl, uint32_t len) {
    return (pl && len >= 4) ? (uint32_t)(pl[0] | (pl[1] << 8) | (pl[2] << 16) | ((uint32_t)pl[3] << 24)) : 0xffffffffu;
}

// NBT_BT_TRACE=1 (read-only diagnostics, ported from Milestone-6): subscribe to the Bluetooth stack's device / service
// informs, as Connectivity does, and log when the phone connects or drops, with uptime. Meant for "after the car
// sleeps and wakes, the phone never opens the Android Auto Bluetooth link" (Supra 2026-10-29): did the phone come back
// over Bluetooth at all, and when, against the re-registrations. Never RegisterEvents (0x02): Connectivity's list.
const uint32_t kReqDeviceListGet = 0x13, kRspDeviceListGet = 0x80000028;
const uint32_t kInfDeviceDisconnected = 0x80000000, kInfDeviceListChanged = 0x80000001;
const uint32_t kInfServiceDisconnected = 0x80000007, kInfServiceListChanged = 0x80000008;
const uint32_t kReqServiceListGet = 0x18, kRspServiceListGet = 0x8000002d;
// Incoming link requests from the phone (libbssservice EvntServiceRequest / ...Complete / ...Error).
const uint32_t kInfServiceRequest = 0x80000009, kInfServiceRequestComplete = 0x8000000a,
               kInfServiceRequestError = 0x8000000b;
SppDirection g_dir;
int g_dirMode = 0;
pthread_mutex_t g_dirMx = PTHREAD_MUTEX_INITIALIZER;
bool btTrace() {
    static int t = -1;
    if (t < 0) { const char* e = getenv("NBT_BT_TRACE"); t = (e && e[0] == '1') ? 1 : 0; }
    return t == 1;
}
double upSecBt() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + ts.tv_nsec / 1e9;
}
std::string g_phoneAddr;
uint32_t g_phoneConn = 0;
bool g_listAsked = false;

const char* connName(uint32_t c) {
    static const char* const k[] = { "not in range", "in range", "connected", "service connected" };
    return c < 4 ? k[c] : "?";
}

void logServiceList(const uint8_t* pl, uint32_t len, bool withError) {
    GapServiceList L;
    uint32_t err = 0;
    if (!gapParseServiceList(pl, len, withError, L, &err)) {
        printf("[BluetoothManager] service list not parsed (%u bytes, error %u)\n", len, err);
        return;
    }
    std::string sv;
    for (size_t i = 0; i < L.services.size(); ++i) {
        char st[16];
        snprintf(st, sizeof(st), "%u", i < L.states.size() ? L.states[i] : 0u);
        sv += (i ? ", " : "") + L.services[i] + "(" + st + ")";
    }
    printf("[BluetoothManager] [%9.3f] phone %s services: %s%s%s\n", upSecBt(), L.address.c_str(), sv.c_str(),
           err ? " | error " : "", err ? gapErrorText(err) : "");
}

void updateDevices(DsiClient* c, const std::vector<GapDevice>& d, const char* from) {
    size_t best = d.size();
    for (size_t i = 0; i < d.size(); ++i)
        if (d[i].connection >= 2 && (best == d.size() || d[i].connection > d[best].connection)) best = i;
    std::string addr = best < d.size() ? d[best].address : std::string();
    uint32_t conn = best < d.size() ? d[best].connection : 0;
    if (addr == g_phoneAddr && conn == g_phoneConn) return;
    g_phoneAddr = addr;
    g_phoneConn = conn;
    if (addr.empty())
        printf("[BluetoothManager] [%9.3f] %s: no phone connected over Bluetooth (%u paired)\n", upSecBt(), from,
               (unsigned)d.size());
    else
        printf("[BluetoothManager] [%9.3f] %s: phone '%s' %s is %s over Bluetooth\n", upSecBt(), from,
               d[best].name.c_str(), addr.c_str(), connName(conn));
    if (!addr.empty() && conn >= 2) {
        std::vector<uint8_t> p;
        DsiClient::putString(p, addr.c_str());
        waitTxIdle();
        c->sendRequest(kReqServiceListGet, p);
    }
}

void onGapAttached(DsiClient* c, void*) {
    if (sppDirMode() != 0) {   // read-only: who opens each Bluetooth serial link (Android Auto = the phone)
        const uint32_t req[] = { kInfServiceRequest, kInfServiceRequestComplete, kInfServiceRequestError };
        for (size_t i = 0; i < sizeof(req) / sizeof(req[0]); ++i) c->setNotification(req[i]);
        printf("[BluetoothManager] watching the phone's incoming link requests (ServiceRequest): an Android Auto "
               "link is opened by the phone, a BMW Apps link by the car\n");
    }
    if (btTrace()) {
        const uint32_t informs[] = { kInfDeviceDisconnected, kInfDeviceListChanged, kInfServiceDisconnected,
                                     kInfServiceListChanged };
        for (size_t i = 0; i < sizeof(informs) / sizeof(informs[0]); ++i) c->setNotification(informs[i]);
        printf("[BluetoothManager] NBT_BT_TRACE=1: logging the phone's Bluetooth connection and services\n");
    }
    g_periodic = false;
    setState(SPP_REG_SENT);
    sendRegister(c);
}

void onGapData(DsiClient* c, uint32_t type, uint32_t id, uint32_t, const uint8_t* pl, uint32_t len, void*) {
    if (id == kInfServiceRequest || id == kInfServiceRequestComplete || id == kInfServiceRequestError) {
        GapReader r(pl, len);
        std::string name, addr, svc;
        if (id == kInfServiceRequest) name = r.str();
        addr = r.str();
        svc = r.str();
        const char* what = id == kInfServiceRequest ? "ServiceRequest"
                         : (id == kInfServiceRequestComplete ? "ServiceRequestComplete" : "ServiceRequestError");
        printf("[BluetoothManager] [%9.3f] %s from the phone: %s%s%s service '%s'%s\n", upSecBt(), what,
               name.empty() ? "" : "'", name.c_str(), name.empty() ? addr.c_str() : ("' " + addr).c_str(), svc.c_str(),
               r.ok() ? "" : " (payload not fully parsed)");
        if (id != kInfServiceRequestError) {
            pthread_mutex_lock(&g_dirMx);
            g_dir.onPhoneRequest(svc, monoSec());
            pthread_mutex_unlock(&g_dirMx);
        }
        fflush(stdout);
        return;
    }
    if (btTrace() && (id == kRspDeviceListGet || id == kInfDeviceListChanged)) {
        std::vector<GapDevice> d;
        uint32_t err = 0;
        if (gapParseDeviceList(pl, len, id == kRspDeviceListGet, d, &err))
            updateDevices(c, d, id == kRspDeviceListGet ? "DeviceList" : "DeviceListChanged");
        else
            printf("[BluetoothManager] 0x%x: device list not parsed (%u bytes, error %u)\n", id, len, err);
        fflush(stdout);
        return;
    }
    if (btTrace() && (id == kRspServiceListGet || id == kInfServiceListChanged)) {
        logServiceList(pl, len, id == kRspServiceListGet);
        fflush(stdout);
        return;
    }
    if (btTrace() && (id == kInfDeviceDisconnected || id == kInfServiceDisconnected)) {
        GapReader r(pl, len);
        if (id == kInfDeviceDisconnected) {
            std::string a = r.str();
            uint32_t why = r.u32();
            printf("[BluetoothManager] [%9.3f] DeviceDisconnected %s (reason %u)\n", upSecBt(), a.c_str(), why);
        } else {
            std::string dv = r.str(), in = r.str(), a = r.str(), sv = r.str();
            uint32_t why = r.u32();
            printf("[BluetoothManager] [%9.3f] ServiceDisconnected %s '%s' (reason %u)\n", upSecBt(), a.c_str(),
                   sv.c_str(), why);
        }
        fflush(stdout);
        return;
    }
    if (id == DSI_RSP_SPP_UUID_REGISTER) {
        uint32_t err = readErr(pl, len);
        printf("[BluetoothManager] SppUuidRegister response: GapErrorCode=%u (%s)%s\n", err, gapErrorName(err),
               g_spp == SPP_REREG_SENT ? (g_periodic ? " [re-registered]" : " [retry]") : "");
        if (err != 0 && g_spp == SPP_REG_SENT) {
            setState(SPP_DEREG_SENT);
            waitTxIdle();
            sendDeregister(c, "clearing a stale registration, then retrying");
            return;
        }
        if (err != 0 && g_periodic)
            printf("[BluetoothManager] re-registering the Android Auto Bluetooth service failed: use Restart in the "
                   "iDrive menu (the next automatic try follows)\n");
        else if (err != 0)
            printf("[BluetoothManager] SPP registration still failing: the phone will not find Android Auto over Bluetooth "
                   "(reboot the head unit and try again)\n");
        else if (g_periodic)
            printf("[BluetoothManager] Android Auto Bluetooth service registered again: a connected phone can now open "
                   "the Bluetooth setup link\n");
        setState(SPP_DONE);
        if (btTrace() && !g_listAsked) {   // who is connected right now (later changes come as informs)
            g_listAsked = true;
            waitTxIdle();
            c->sendRequest(kReqDeviceListGet, std::vector<uint8_t>());
        }
    } else if (g_spp == SPP_DEREG_SENT && type == 0x200 && id != kRspDeviceListGet && id != kRspServiceListGet) {
        // Deregister response (0x80000044 on the car); take the first plain response after the request.
        uint32_t err = readErr(pl, len);
        printf("[BluetoothManager] SppUuidDeregister response (id=0x%x): GapErrorCode=%u (%s); registering again\n",
               id, err, gapErrorName(err));
        setState(SPP_REREG_SENT);
        sendRegister(c);
    } else if (type == 0x204 || type == 0x202) {
        printf("[BluetoothManager] request rejected by stub/server (type=0x%x id=0x%x)\n", type, id);
        if (g_spp == SPP_DEREG_SENT) {   // no deregister support: still try once more
            setState(SPP_REREG_SENT);
            sendRegister(c);
        } else if (g_spp == SPP_REREG_SENT) {
            setState(SPP_DONE);
        }
    }
    fflush(stdout);
}
} // namespace

void sppDirConfigure(int mode, uint32_t windowS, int model) {
    pthread_mutex_lock(&g_dirMx);
    g_dirMode = mode;
    g_dir.setWindowSeconds(windowS);
    g_dir.setModel((SppDirection::Model)model);
    pthread_mutex_unlock(&g_dirMx);
}

int sppDirMode() {
    pthread_mutex_lock(&g_dirMx);
    int m = g_dirMode;
    pthread_mutex_unlock(&g_dirMx);
    return m;
}

bool sppDirTakePhoneOpened(std::string* service) {
    pthread_mutex_lock(&g_dirMx);
    bool r = g_dirMode != 0 && g_dir.takePhoneOpened(monoSec(), service);
    pthread_mutex_unlock(&g_dirMx);
    return r;
}

int sppDirLinkResult(bool phoneOpened, bool spoke) {
    pthread_mutex_lock(&g_dirMx);
    int r = (g_dirMode != 0 && g_dir.onLinkResult(phoneOpened, spoke)) ? (int)g_dir.model() : -1;
    pthread_mutex_unlock(&g_dirMx);
    return r;
}

bool sppDirGate() {
    pthread_mutex_lock(&g_dirMx);
    bool r = g_dirMode != 0 && g_dir.gate(g_dirMode == 1);
    pthread_mutex_unlock(&g_dirMx);
    return r;
}

int sppDirModel() {
    pthread_mutex_lock(&g_dirMx);
    int r = (int)g_dir.model();
    pthread_mutex_unlock(&g_dirMx);
    return r;
}

unsigned sppDirRequests() {
    pthread_mutex_lock(&g_dirMx);
    unsigned r = g_dir.requestsSeen();
    pthread_mutex_unlock(&g_dirMx);
    return r;
}

BluetoothManager::BluetoothManager() : is_registered_(false) {}

BluetoothManager::~BluetoothManager() { stop(); }

bool BluetoothManager::init() {
    printf("[BluetoothManager] Initializing (DSI client to BSS_GAP.SGBluetoothGap %d.%d)\n",
           DSI_GAP_MAJOR, DSI_GAP_MINOR);
    return true;
}

bool BluetoothManager::registerAndroidAutoSdp() {
    if (is_registered_) return true;
    g_gap.setCallbacks(&onGapAttached, &onGapData, NULL);
    if (!g_gap.open(DSI_SERVICE_BLUETOOTH_GAP, DSI_ROLE_BLUETOOTH_GAP, DSI_GAP_MAJOR, DSI_GAP_MINOR)) {
        printf("[BluetoothManager] DSI client could not be created; no SDP registration\n");
        return false;
    }
    is_registered_ = true;   // request goes out asynchronously when the server attaches
    return true;
}

bool BluetoothManager::reregister(const char* why) {
    if (!is_registered_ || !g_gap.attached()) {
        printf("[BluetoothManager] %s: Bluetooth service not attached, nothing to re-register\n", why);
        fflush(stdout);
        return false;
    }
    pthread_mutex_lock(&g_stateMx);
    SppState s = g_spp;
    uint32_t age = monoSec() - g_stateSec;
    bool inFlight = (s == SPP_REG_SENT || s == SPP_DEREG_SENT || s == SPP_REREG_SENT);
    if (s == SPP_IDLE || (inFlight && age < 15)) {
        pthread_mutex_unlock(&g_stateMx);
        printf("[BluetoothManager] %s: a registration is still in progress, not re-registering now\n", why);
        fflush(stdout);
        return false;
    }
    if (inFlight)
        printf("[BluetoothManager] previous registration request got no answer in %u s, starting over\n", (unsigned)age);
    g_spp = SPP_DEREG_SENT;
    g_stateSec = monoSec();
    g_periodic = true;
    g_txBusy = true;
    pthread_mutex_unlock(&g_stateMx);

    printf("[BluetoothManager] [%9.3f] %s: re-registering the Android Auto Bluetooth service (what Restart does)\n",
           upSecBt(), why);
    sendDeregister(&g_gap, "removing the registration, then registering again");
    g_txBusy = false;
    fflush(stdout);
    return true;
}

void BluetoothManager::requestSppBounce() {
    reregister("handshake failed before the phone accepted the Wi-Fi start");
}

void BluetoothManager::stop() {
    if (!is_registered_) return;
    // Deregistration (request 0x2f) is intentionally not sent at shutdown: the DSI
    // connection thread is detached and sending off-thread is unsafe.
    is_registered_ = false;
}

} // namespace nbt
