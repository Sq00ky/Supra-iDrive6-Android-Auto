// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "most/client.hpp"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <vector>

namespace nbt {
namespace most {

namespace {

// Headers logged per message class, and how many messages of a class other than normal are logged when a route wants
// them. Together they answer what each class is on the car, without a message rate reaching the log.
enum { kFirstHeaders = 4, kRoutedNonNormalHeaders = 16 };

uint32_t nowMs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)ts.tv_sec * 1000u + (uint32_t)(ts.tv_nsec / 1000000);
}

void printHeader(const char* what, const View& m) {
    printf("[MOST] %s: tx=%u var=%u/%u %04X->%04X %02X/%02X fkt=%03X %s(%X) tag=%u len=%u\n", what, m.txType, m.varTag,
           m.varVal, m.src, m.dst, m.fblock, m.inst, m.fkt, opName(m.op), m.op, m.hdrTag, (unsigned)m.len);
}

} // namespace

const char* linkStateName(LinkState s) {
    switch (s) {
        case kDetached: return "detached";
        case kRegistering: return "registering";
        case kRefused: return "refused";
        case kUp: return "up";
    }
    return "?";
}

Client::Client()
    : nKeys_(0), started_(false), rawLog_(false), stage_(0), routedNonNormalLogged_(0), state_(kDetached),
      clientId_(kClientRefused), deviceId_(0), haveComm_(false), comm_(0), longestWindowMs_(0), lastHealthMs_(0) {
    name_[0] = 0;
    memset(firstSeen_, 0, sizeof(firstSeen_));
    memset(&total_, 0, sizeof(total_));
    memset(&lastHealth_, 0, sizeof(lastHealth_));
    pthread_mutex_init(&lock_, NULL);
}

bool Client::addRoute(uint8_t fblock, int inst, int fkt, int op, Handler h, void* user) {
    if (started_) return false;
    return router_.add(fblock, inst, fkt, op, h, user);
}

bool Client::addDevCtrl(uint16_t key) {
    if (started_) return false;
    for (int i = 0; i < nKeys_; ++i)
        if (keys_[i] == key) return true;
    if (nKeys_ >= kMaxKeys) return false;
    keys_[nKeys_++] = key;
    return true;
}

void Client::setRawLog(bool on) { rawLog_ = on; }

bool Client::start(const char* clientName) {
    if (started_) return false;
    if (router_.size() == 0) {
        printf("[MOST] no consumer attached: client not started\n");
        return true;
    }
    snprintf(name_, sizeof(name_), "%s", clientName);
    started_ = true;
    lastHealthMs_ = nowMs();
    dsi_.setQuiet(!rawLog_);
    dsi_.setCallbacks(&Client::attachedTramp, &Client::dataTramp, this);
    if (!dsi_.open(kServerInterface, kServerRole, kInterfaceMajor, kInterfaceMinor)) {
        printf("[MOST] DSI open FAILED: no MOST messages for any consumer\n");
        return false;
    }
    return true;
}

void Client::attachedTramp(DsiClient* c, void* self) { static_cast<Client*>(self)->onAttached(c); }

void Client::dataTramp(DsiClient* c, uint32_t, uint32_t id, uint32_t, const uint8_t* p, uint32_t len, void* self) {
    static_cast<Client*>(self)->onData(c, id, p, len);
}

void Client::setState(LinkState s) {
    pthread_mutex_lock(&lock_);
    state_ = s;
    pthread_mutex_unlock(&lock_);
}

LinkState Client::state() const {
    pthread_mutex_lock(&lock_);
    LinkState s = state_;
    pthread_mutex_unlock(&lock_);
    return s;
}

ClientStats Client::stats() const {
    pthread_mutex_lock(&lock_);
    ClientStats s = total_;
    pthread_mutex_unlock(&lock_);
    return s;
}

void Client::onAttached(DsiClient* c) {
    printf("[MOST] attached to %s.%s: registering client %s\n", kServerRole, kServerInterface, name_);
    setState(kRegistering);
    std::vector<uint8_t> p;
    DsiClient::putString(p, name_);
    stage_ = 1;
    c->sendRequest(kReqRegisterClient, p);
    for (size_t i = 0; i < (size_t)kClientSubscriptionCount; ++i) c->setNotification(kClientSubscriptions[i]);
}

void Client::onData(DsiClient* c, uint32_t id, const uint8_t* p, uint32_t len) {
    if (id == kInfoMessage) {
        onMessage(p, len);
        return;
    }
    if (id == kInfoClientResponse && len >= 6) {
        uint32_t client = rdLe32(p);
        unsigned dev = rdLe16(p + 4);
        bool refused = client == kClientRefused;
        printf("[MOST] registered: clientId=%u deviceId=0x%04X%s\n", client, dev, refused ? "  (REGISTRATION REFUSED)" : "");
        pthread_mutex_lock(&lock_);
        clientId_ = client;
        deviceId_ = dev;
        state_ = refused ? kRefused : kUp;
        pthread_mutex_unlock(&lock_);
        if (refused || stage_ >= 2) return;
        c->setNotification(kAttrCommState);
        stage_ = 2;
        if (nKeys_ == 0) return;     // the vendor client skips it too, and the server logs an error for an empty list
        std::vector<uint16_t> keys(keys_, keys_ + nKeys_);
        c->sendRequest(kReqRegisterDevCtrls, buildRegisterDevCtrls(client, keys));
        printf("[MOST] registerDevCtrls sent (%d keys)\n", nKeys_);
        return;
    }
    if (id == kAttrCommState && len >= 4) {
        uint32_t v = rdLe32(p);
        pthread_mutex_lock(&lock_);
        bool changed = !haveComm_ || comm_ != v;
        haveComm_ = true;
        comm_ = v;
        pthread_mutex_unlock(&lock_);
        if (changed) printf("[MOST] comm state %u\n", v);
    }
}

void Client::noteFirst(const View& m, MsgClass c, bool wanted) {
    if (firstSeen_[c] < (unsigned)kFirstHeaders) {
        char what[32];
        snprintf(what, sizeof(what), "first %s #%u", className(c), ++firstSeen_[c]);
        printHeader(what, m);
    }
    if (wanted && c != kNormal && routedNonNormalLogged_ < (unsigned)kRoutedNonNormalHeaders) {
        ++routedNonNormalLogged_;
        char what[48];
        snprintf(what, sizeof(what), "routed function in class %s", className(c));
        printHeader(what, m);
    }
}

void Client::onMessage(const uint8_t* p, uint32_t len) {
    View m;
    if (!parseView(p, len, m)) {
        pthread_mutex_lock(&lock_);
        ++total_.bad;
        pthread_mutex_unlock(&lock_);
        return;
    }
    m.rxMs = nowMs();
    MsgClass c = classify(m);
    noteFirst(m, c, router_.wants(m));
    int ran = router_.dispatch(m);
    uint32_t took = nowMs() - m.rxMs;

    pthread_mutex_lock(&lock_);
    ++total_.msgs;
    switch (c) {
        case kNormal: ++total_.normal; break;
        case kSegFirst: case kSegMiddle: case kSegLast: ++total_.seg; break;
        case kTxStatus: ++total_.txStatus; break;
        case kNoInfo: ++total_.noInfo; break;
    }
    if (ran > 0) {
        ++total_.routed;
        if (c != kNormal) ++total_.routedNonNormal;
    }
    if (took > total_.longestHandlerMs) total_.longestHandlerMs = took;
    if (took > longestWindowMs_) longestWindowMs_ = took;
    pthread_mutex_unlock(&lock_);
}

void Client::logHealth() {
    if (!started_) return;
    uint32_t now = nowMs();
    pthread_mutex_lock(&lock_);
    ClientStats t = total_, l = lastHealth_;
    lastHealth_ = total_;
    uint32_t longest = longestWindowMs_;
    longestWindowMs_ = 0;
    LinkState st = state_;
    bool haveComm = haveComm_;
    uint32_t comm = comm_, client = clientId_;
    unsigned dev = deviceId_;
    uint32_t secs = (now - lastHealthMs_ + 500) / 1000;
    lastHealthMs_ = now;
    pthread_mutex_unlock(&lock_);

    char commText[16];
    if (haveComm) snprintf(commText, sizeof(commText), "%u", comm);
    else snprintf(commText, sizeof(commText), "?");
    printf("[MOST] %u s: %u msgs (normal %u, seg %u, txstatus %u, noinfo %u), routed %u (non-normal %u), bad %u, "
           "longest handler %u ms, link %s, comm %s, client %u dev 0x%04X\n",
           secs, t.msgs - l.msgs, t.normal - l.normal, t.seg - l.seg, t.txStatus - l.txStatus, t.noInfo - l.noInfo,
           t.routed - l.routed, t.routedNonNormal - l.routedNonNormal, t.bad - l.bad, longest, linkStateName(st), commText,
           client, dev);
}

} // namespace most
} // namespace nbt
