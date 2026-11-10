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
        case kCommNotOk: return "comm-not-ok";
        case kUp: return "up";
    }
    return "?";
}

Client::Client()
    : nKeys_(0), nListeners_(0), nRegListeners_(0), nSendKeys_(0), tap_(NULL), tapUser_(NULL), infoTap_(NULL), infoTapUser_(NULL), started_(false), rawLog_(false), stage_(0), routedNonNormalLogged_(0), state_(kDetached),
      attached_(false), answered_(false), clientId_(kClientRefused), deviceId_(0), haveComm_(false), comm_(0),
      commOkSeen_(false), longestWindowMs_(0), lastHealthMs_(0) {
    name_[0] = 0;
    memset(firstSeen_, 0, sizeof(firstSeen_));
    memset(&total_, 0, sizeof(total_));
    memset(&lastHealth_, 0, sizeof(lastHealth_));
    pthread_mutex_init(&lock_, NULL);
    pthread_mutex_init(&sendLock_, NULL);
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

bool Client::addLinkListener(LinkCb cb, void* user) {
    if (started_ || !cb || nListeners_ >= kMaxLinkListeners) return false;
    listeners_[nListeners_].cb = cb;
    listeners_[nListeners_].user = user;
    ++nListeners_;
    return true;
}

bool Client::addRegistryListener(RegistryCb cb, void* user) {
    if (started_ || !cb || nRegListeners_ >= kMaxRegistryListeners) return false;
    regListeners_[nRegListeners_].cb = cb;
    regListeners_[nRegListeners_].user = user;
    ++nRegListeners_;
    return true;
}

bool Client::allowSend(uint8_t fblock, uint8_t inst) {
    if (started_) return false;
    uint16_t key = registryKey(fblock, inst);
    for (int i = 0; i < nSendKeys_; ++i)
        if (sendKeys_[i] == key) return true;
    if (nSendKeys_ >= kMaxSendKeys) return false;
    sendKeys_[nSendKeys_++] = key;
    return true;
}

bool Client::setTap(TapCb cb, void* user) {
    if (started_ || !cb || tap_) return false;
    tap_ = cb;
    tapUser_ = user;
    return true;
}

bool Client::setInfoTap(InfoTapCb cb, void* user) {
    if (started_ || !cb || infoTap_) return false;
    infoTap_ = cb;
    infoTapUser_ = user;
    return true;
}

void Client::setRawLog(bool on) { rawLog_ = on; }

bool Client::start(const char* clientName) {
    if (started_) return false;
    if (router_.size() == 0 && !tap_) {
        printf("[MOST] no consumer attached: client not started\n");
        return true;
    }
    snprintf(name_, sizeof(name_), "%s", clientName);
    started_ = true;
    lastHealthMs_ = nowMs();
    dsi_.setQuiet(!rawLog_);
    dsi_.setQuietTx(!rawLog_);
    dsi_.setCallbacks(&Client::attachedTramp, &Client::dataTramp, this);
    dsi_.setDetachCallback(&Client::detachedTramp);
    if (!dsi_.open(kServerInterface, kServerRole, kInterfaceMajor, kInterfaceMinor)) {
        printf("[MOST] DSI open FAILED: no MOST messages for any consumer\n");
        return false;
    }
    return true;
}

void Client::attachedTramp(DsiClient* c, void* self) { static_cast<Client*>(self)->onAttached(c); }
void Client::detachedTramp(DsiClient*, void* self) { static_cast<Client*>(self)->onDetached(); }

void Client::dataTramp(DsiClient* c, uint32_t, uint32_t id, uint32_t, const uint8_t* p, uint32_t len, void* self) {
    static_cast<Client*>(self)->onData(c, id, p, len);
}

LinkState Client::deriveStateLocked() const {
    if (!attached_) return kDetached;
    if (!answered_) return kRegistering;
    if (clientId_ == kClientRefused) return kRefused;
    if (commOkSeen_ && comm_ != kCommStateOk) return kCommNotOk;
    return kUp;
}

void Client::updateStateLocked() {
    LinkState s = deriveStateLocked();
    bool changed = s != state_;
    state_ = s;
    pthread_mutex_unlock(&lock_);
    if (!changed) return;
    printf("[MOST] link %s\n", linkStateName(s));
    for (int i = 0; i < nListeners_; ++i) listeners_[i].cb(s, listeners_[i].user);
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

uint16_t Client::selfAddress() const {
    pthread_mutex_lock(&lock_);
    uint16_t a = (attached_ && answered_ && clientId_ != kClientRefused) ? (uint16_t)deviceId_ : 0;
    pthread_mutex_unlock(&lock_);
    return a;
}

bool Client::request(DsiClient* c, uint32_t id, const std::vector<uint8_t>& p) {
    pthread_mutex_lock(&sendLock_);
    bool ok = c->sendRequest(id, p);
    pthread_mutex_unlock(&sendLock_);
    return ok;
}

bool Client::notify(DsiClient* c, uint32_t attr) {
    pthread_mutex_lock(&sendLock_);
    bool ok = c->setNotification(attr);
    pthread_mutex_unlock(&sendLock_);
    return ok;
}

// The DSI library's comment says a send belongs on the connection thread. Sending from another thread, with one lock
// around every send, is what tools/hud_probe did on the car, and the cluster answered every call.
bool Client::send(const Msg& m) {
    bool allowed = false;
    uint16_t key = registryKey(m.fblock, m.inst);
    for (int i = 0; i < nSendKeys_; ++i)
        if (sendKeys_[i] == key) allowed = true;
    bool shape = m.opType <= kOpStartAck && m.dst >= 0x0100 && m.dst <= 0x0FFF;
    pthread_mutex_lock(&lock_);
    bool up = started_ && state_ == kUp && clientId_ != kClientRefused && deviceId_ != 0;
    uint32_t client = clientId_;
    uint16_t self = (uint16_t)deviceId_;
    if (!allowed || !shape || !up) {
        ++total_.sendRefused;
        pthread_mutex_unlock(&lock_);
        return false;
    }
    pthread_mutex_unlock(&lock_);

    Msg out = m;
    out.src = self;
    std::vector<uint8_t> req;
    putLe32(req, client);
    std::vector<uint8_t> body = buildMessage(out);
    req.insert(req.end(), body.begin(), body.end());
    bool ok = request(&dsi_, kReqProcessMessage, req);
    pthread_mutex_lock(&lock_);
    if (ok) ++total_.sent;
    else ++total_.sendRefused;
    pthread_mutex_unlock(&lock_);
    return ok;
}

void Client::onAttached(DsiClient* c) {
    printf("[MOST] attached to %s.%s: registering client %s\n", kServerRole, kServerInterface, name_);
    pthread_mutex_lock(&lock_);
    attached_ = true;
    answered_ = false;
    haveComm_ = false;
    commOkSeen_ = false;
    updateStateLocked();
    std::vector<uint8_t> p;
    DsiClient::putString(p, name_);
    stage_ = 1;
    request(c, kReqRegisterClient, p);
    for (size_t i = 0; i < (size_t)kClientSubscriptionCount; ++i) notify(c, kClientSubscriptions[i]);
}

// Routes and keys persist: the next attach registers again and gets new ids.
void Client::onDetached() {
    printf("[MOST] detached from the server\n");
    stage_ = 0;
    pthread_mutex_lock(&lock_);
    attached_ = false;
    answered_ = false;
    haveComm_ = false;
    commOkSeen_ = false;
    updateStateLocked();
}

void Client::onData(DsiClient* c, uint32_t id, const uint8_t* p, uint32_t len) {
    if (id == kInfoMessage) {
        onMessage(p, len);
        return;
    }
    if (infoTap_) infoTap_(id, p, len, infoTapUser_);
    if (id == kAttrRegistry && nRegListeners_ > 0) {
        std::vector<RegistryEntry> reg;
        if (decodeRegistry(p, len, reg))
            for (int i = 0; i < nRegListeners_; ++i) regListeners_[i].cb(reg, regListeners_[i].user);
        return;
    }
    if (id == kInfoClientResponse && len >= 6) {
        uint32_t client = rdLe32(p);
        unsigned dev = rdLe16(p + 4);
        bool refused = client == kClientRefused;
        printf("[MOST] registered: clientId=%u deviceId=0x%04X%s\n", client, dev, refused ? "  (REGISTRATION REFUSED)" : "");
        pthread_mutex_lock(&lock_);
        answered_ = true;
        clientId_ = client;
        deviceId_ = dev;
        updateStateLocked();
        if (refused || stage_ >= 2) return;
        notify(c, kAttrCommState);
        stage_ = 2;
        if (nKeys_ == 0) return;     // the vendor client skips it too, and the server logs an error for an empty list
        std::vector<uint16_t> keys(keys_, keys_ + nKeys_);
        request(c, kReqRegisterDevCtrls, buildRegisterDevCtrls(client, keys));
        printf("[MOST] registerDevCtrls sent (%d keys)\n", nKeys_);
        return;
    }
    if (id == kAttrCommState && len >= 4) {
        uint32_t v = rdLe32(p);
        pthread_mutex_lock(&lock_);
        bool changed = !haveComm_ || comm_ != v;
        haveComm_ = true;
        comm_ = v;
        if (v == kCommStateOk) commOkSeen_ = true;
        pthread_mutex_unlock(&lock_);
        if (changed) printf("[MOST] comm state %u\n", v);
        pthread_mutex_lock(&lock_);
        updateStateLocked();
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
        char what[64];
        snprintf(what, sizeof(what), "routed function in class %s%s", className(c),
                 c == kTxStatus ? " (not delivered)" : "");
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
    bool wanted = router_.wants(m);
    noteFirst(m, c, wanted);
    if (tap_) tap_(m, c, tapUser_);

    int ran = 0;
    bool joined = false, lost = false;
    if (c == kSegFirst || c == kSegMiddle || c == kSegLast) {
        if (wanted) {
            View whole;
            Reassembler::Result r = reassembler_.feed(m, c, whole);
            joined = r == Reassembler::kJoined;
            lost = r == Reassembler::kLost;
            if (joined) ran = router_.dispatch(whole);
        }
    } else if (c != kTxStatus) {
        ran = router_.dispatch(m);
    }
    uint32_t took = nowMs() - m.rxMs;

    pthread_mutex_lock(&lock_);
    ++total_.msgs;
    switch (c) {
        case kNormal: ++total_.normal; break;
        case kSegFirst: case kSegMiddle: case kSegLast: ++total_.seg; break;
        case kTxStatus: ++total_.txStatus; break;
        case kNoInfo: ++total_.noInfo; break;
    }
    if (ran > 0) ++total_.routed;
    if (wanted && c != kNormal) ++total_.routedNonNormal;
    if (joined) ++total_.joined;
    if (lost) ++total_.segLost;
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
    printf("[MOST] %u s: %u msgs (normal %u, seg %u, txstatus %u, noinfo %u), routed %u, non-normal on routes %u, "
           "joined %u, seg lost %u, bad %u, longest handler %u ms, sent %u, send refused %u, link %s, comm %s, client %u dev 0x%04X\n",
           secs, t.msgs - l.msgs, t.normal - l.normal, t.seg - l.seg, t.txStatus - l.txStatus, t.noInfo - l.noInfo,
           t.routed - l.routed, t.routedNonNormal - l.routedNonNormal, t.joined - l.joined, t.segLost - l.segLost,
           t.bad - l.bad, longest, t.sent - l.sent, t.sendRefused - l.sendRefused, linkStateName(st), commText, client, dev);
}

} // namespace most
} // namespace nbt
