// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "dsi_client.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <pthread.h>

namespace nbt {

namespace {

typedef void (*FnConnCtor)(void*);
typedef int  (*FnConnAdd)(void*, void*);
typedef void (*FnConnStart)(void*);
typedef void (*FnCliCtor)(void*, const char*, const char*, int, int);
typedef void (*FnSetNotif)(void*, unsigned);
typedef void (*FnStreamCtor)(void*, void*, void*, int, unsigned, unsigned, unsigned,
                             const void*, const void*, int);
typedef void (*FnStreamWrite)(void*, const void*, unsigned);
typedef int  (*FnStreamSend)(void*, int);
typedef void (*FnStreamDtor)(void*);

struct Syms {
    FnConnCtor connCtor;
    FnConnAdd connAdd;
    FnConnStart connStart;
    FnCliCtor cliCtor;
    FnSetNotif setNotif;
    FnStreamCtor streamCtor;
    FnStreamWrite streamWrite;
    FnStreamSend streamSend;
    FnStreamDtor streamDtor;
    void* vtable;
};

Syms g_s;
pthread_mutex_t g_symMutex = PTHREAD_MUTEX_INITIALIZER;

// Several clients per process (e.g. Bluetooth GAP + MOST server): the library calls the replaced
// virtuals with the CDSIClient object as `this`, so the trampolines map that pointer back to us.
enum { kMaxClients = 6 };   // GAP, MOST server, handsfreed, + NBT_BT_PLAY (DMMAudioSource, SGMedia)
struct Slot { void* cli; DsiClient* self; };
Slot g_slots[kMaxClients];
int g_nslots = 0;               // only grows; entries are written before CDSIConnection::add

DsiClient* lookup(void* cli) {
    for (int i = 0; i < g_nslots; ++i)
        if (g_slots[i].cli == cli) return g_slots[i].self;
    static bool warned = false;
    if (!warned) { warned = true; printf("[DsiClient] WARNING: callback for unknown client %p (%d registered)\n", cli, g_nslots); }
    return g_nslots == 1 ? g_slots[0].self : NULL;   // single-client behaviour of earlier builds
}

void* need(void* lib, const char* name) {
    void* p = dlsym(lib, name);
    printf("[DsiClient]   dlsym %-70s %s\n", name, p ? "ok" : "MISSING");
    return p;
}

} // namespace

DsiClient::DsiClient()
    : lib_(NULL), conn_(NULL), cli_(NULL), attached_(false), quiet_(false),
      attachedCb_(NULL), dataCb_(NULL), user_(NULL) {}

void DsiClient::setCallbacks(AttachedCb a, DataCb d, void* user) {
    attachedCb_ = a; dataCb_ = d; user_ = user;
}

void DsiClient::putU32(std::vector<uint8_t>& v, uint32_t x) {
    while (v.size() % 4) v.push_back(0);
    v.push_back((uint8_t)(x & 0xff));
    v.push_back((uint8_t)((x >> 8) & 0xff));
    v.push_back((uint8_t)((x >> 16) & 0xff));
    v.push_back((uint8_t)((x >> 24) & 0xff));
}

void DsiClient::putString(std::vector<uint8_t>& v, const char* s) {
    uint32_t n = (uint32_t)strlen(s) + 1;   // length includes the NUL
    putU32(v, n);
    v.insert(v.end(), s, s + n);
    while (v.size() % 4) v.push_back(0);    // padding after string: INFERRED
}

bool DsiClient::open(const char* iface, const char* role, int major, int minor) {
    if (cli_) { printf("[DsiClient] open: this client is already open\n"); return false; }
    if (g_nslots >= kMaxClients) { printf("[DsiClient] open: too many clients (max %d)\n", (int)kMaxClients); return false; }

    const char* names[] = {
        "/opt/sys/lib/libsys_dsi_colibry.cpp-ne.so.8",
        "/fs/sda0/opt/sys/lib/libsys_dsi_colibry.cpp-ne.so.8",
        "libsys_dsi_colibry.cpp-ne.so.8",
        NULL
    };
    for (int i = 0; names[i] && !lib_; ++i) {
        lib_ = dlopen(names[i], RTLD_NOW | RTLD_GLOBAL);
        printf("[DsiClient] dlopen(%s) -> %s\n", names[i], lib_ ? "OK" : dlerror());
    }
    if (!lib_) return false;

    pthread_mutex_lock(&g_symMutex);
    if (!g_s.vtable) {   // resolve once per process; later clients reuse the table
        memset(&g_s, 0, sizeof(g_s));
        g_s.connCtor    = (FnConnCtor)   need(lib_, "_ZN14CDSIConnectionC1Ev");
        g_s.connAdd     = (FnConnAdd)    need(lib_, "_ZN14CDSIConnection3addER10CDSIClient");
        g_s.connStart   = (FnConnStart)  need(lib_, "_ZN14CDSIConnection5startEv");
        g_s.cliCtor     = (FnCliCtor)    need(lib_, "_ZN10CDSIClientC1EPKcS1_ii");
        g_s.setNotif    = (FnSetNotif)   need(lib_, "_ZN10CDSIClient15setNotificationEj");
        g_s.streamCtor  = (FnStreamCtor) need(lib_, "_ZN11CDSIOStreamC1ER11CDSIChannelP13CDSITCPSocketiN3DSI11RequestTypeENS4_7CommandEjRK8SPartyIDS9_i");
        g_s.streamWrite = (FnStreamWrite)need(lib_, "_ZN11CDSIOStream5writeEPKvj");
        g_s.streamSend  = (FnStreamSend) need(lib_, "_ZN11CDSIOStream4sendEi");
        g_s.streamDtor  = (FnStreamDtor) need(lib_, "_ZN11CDSIOStreamD1Ev");
        g_s.vtable      = need(lib_, "_ZTV10CDSIClient");
        if (!g_s.connCtor || !g_s.connAdd || !g_s.connStart || !g_s.cliCtor || !g_s.setNotif ||
            !g_s.streamCtor || !g_s.streamWrite || !g_s.streamSend || !g_s.streamDtor || !g_s.vtable) {
            printf("[DsiClient] required symbols missing, aborting\n");
            memset(&g_s, 0, sizeof(g_s));
            pthread_mutex_unlock(&g_symMutex);
            return false;
        }
    }
    pthread_mutex_unlock(&g_symMutex);

    conn_ = calloc(1, 0x8c + 64);
    cli_  = static_cast<char*>(calloc(1, 0x14c + 64));
    if (!conn_ || !cli_) return false;

    printf("[DsiClient] constructing connection + client %s.%s %d.%d\n", role, iface, major, minor);
    g_s.connCtor(conn_);
    g_s.cliCtor(cli_, iface, role, major, minor);

    // Replace the abstract vtable: copy 8 slots starting at vptr (= _ZTV + 8).
    // Copy generously (32 slots): the recipe only lists slots up to +0x1c, but if CDSIClient
    // has more virtuals a short copy would let the library call through heap garbage.
    void** vt = static_cast<void**>(calloc(32, sizeof(void*)));
    memcpy(vt, static_cast<char*>(g_s.vtable) + 8, 32 * sizeof(void*));
    vt[3] = reinterpret_cast<void*>(&DsiClient::onAttachedTramp);   // +0x0c attached  (label inferred)
    vt[4] = reinterpret_cast<void*>(&DsiClient::onDetachedTramp);   // +0x10 detached  (label inferred)
    vt[7] = reinterpret_cast<void*>(&DsiClient::onDataTramp);       // +0x1c handleDataResponse
    *reinterpret_cast<void***>(cli_) = vt;

    pthread_mutex_lock(&g_symMutex);
    g_slots[g_nslots].cli = cli_;
    g_slots[g_nslots].self = this;
    ++g_nslots;
    pthread_mutex_unlock(&g_symMutex);
    int ok = g_s.connAdd(conn_, cli_);
    printf("[DsiClient] CDSIConnection::add -> %d\n", ok);

    pthread_attr_t at;
    pthread_attr_init(&at);
    pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    pthread_t t;
    bool started = pthread_create(&t, &at, &DsiClient::threadMain, this) == 0;
    pthread_attr_destroy(&at);
    printf("[DsiClient] connection thread %s\n", started ? "started" : "FAILED");
    return started;
}

void* DsiClient::threadMain(void* arg) {
    DsiClient* c = static_cast<DsiClient*>(arg);
    printf("[DsiClient] CDSIConnection::start() entering sender loop (blocks)\n");
    fflush(stdout);
    g_s.connStart(c->conn_);
    printf("[DsiClient] CDSIConnection::start() returned\n");
    return NULL;
}

void DsiClient::onAttachedTramp(void* self) {
    DsiClient* c = lookup(self);
    if (!c) return;
    c->attached_ = true;
    printf("[DsiClient] ATTACHED to server\n");
    fflush(stdout);
    if (c->attachedCb_) c->attachedCb_(c, c->user_);
}

void DsiClient::onDetachedTramp(void* self) {
    DsiClient* c = lookup(self);
    if (!c) return;
    c->attached_ = false;
    printf("[DsiClient] DETACHED from server\n");
    fflush(stdout);
}

void DsiClient::onDataTramp(void* self, void* istream) {
    DsiClient* c = lookup(self);
    if (!c || !istream) return;
    const char* s = static_cast<const char*>(istream);
    uint32_t type = *reinterpret_cast<const uint32_t*>(s + 0x44);
    uint32_t id   = *reinterpret_cast<const uint32_t*>(s + 0x48);
    uint32_t seq  = *reinterpret_cast<const uint32_t*>(s + 0x4c);
    const uint8_t* pl = *reinterpret_cast<const uint8_t* const*>(s + 0x10);
    uint32_t len  = *reinterpret_cast<const uint32_t*>(s + 0x08);
    if (!c->quiet_) {
        printf("[DsiClient] RX type=0x%x id=0x%x seq=%u len=%u:", type, id, seq, len);
        for (uint32_t i = 0; pl && i < len && i < 48; ++i) printf(" %02x", pl[i]);
        printf("\n");
        fflush(stdout);
    }
    if (c->dataCb_) c->dataCb_(c, type, id, seq, pl, len, c->user_);
}

bool DsiClient::setNotification(uint32_t attr) {
    if (!cli_ || !attached_) return false;
    g_s.setNotif(cli_, attr);
    if (!quiet_) printf("[DsiClient] setNotification(0x%x)\n", attr);
    return true;
}

bool DsiClient::sendRequest(uint32_t requestId, const std::vector<uint8_t>& payload) {
    if (!cli_ || !attached_) { printf("[DsiClient] sendRequest: not attached\n"); return false; }
    // CDSIOStream may hold 64-bit members: keep the buffer 8-byte aligned (LDRD/STRD fault on ARM).
    uint64_t stream_storage[(0xBC + 64) / 8 + 1];
    char* stream = reinterpret_cast<char*>(stream_storage);
    memset(stream_storage, 0, sizeof(stream_storage));
    // client fields (recipe s.2/s.3): +0x110 client party, +0x118 server party,
    // +0x130 CDSIChannel, +0x138 CDSITCPSocket, +0x140 ipc fd.
    void* chan = cli_ + 0x130;
    void* tcp  = cli_ + 0x138;
    int ipcFd  = *reinterpret_cast<int*>(cli_ + 0x140);
    g_s.streamCtor(stream, chan, tcp, ipcFd, 0x100u, 7u, requestId, cli_ + 0x110, cli_ + 0x118, 0);
    if (!payload.empty()) g_s.streamWrite(stream, &payload[0], (unsigned)payload.size());
    int rc = g_s.streamSend(stream, -1);
    g_s.streamDtor(stream);
    printf("[DsiClient] TX request id=0x%x payload=%u bytes -> send rc=%d\n",
           requestId, (unsigned)payload.size(), rc);
    fflush(stdout);
    return true;
}

} // namespace nbt
