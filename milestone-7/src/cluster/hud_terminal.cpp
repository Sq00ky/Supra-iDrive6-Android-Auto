// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "cluster/hud_terminal.hpp"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

namespace nbt {
namespace hudterm {

namespace {

uint32_t nowMs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)ts.tv_sec * 1000u + (uint32_t)(ts.tv_nsec / 1000000);
}

// ms left of a span that started at `from`, 0 once it has passed.
uint32_t left(uint32_t from, uint32_t span, uint32_t now) {
    uint32_t gone = now - from;
    return gone >= span ? 0 : span - gone;
}

} // namespace

const char* statusName(Status s) {
    switch (s) {
        case kOffline: return "offline";
        case kReady: return "ready";
        case kNavActive: return "factory-nav";
        case kFailed: return "failed";
    }
    return "?";
}

const char* submitResultName(SubmitResult r) {
    switch (r) {
        case kQueued: return "queued";
        case kReplaced: return "replaced";
        case kMerged: return "merged";
        case kNotReady: return "not-ready";
        case kRefused: return "refused";
    }
    return "?";
}

// ---- Core ----------------------------------------------------------------------------------------------------------

Core::Core(const Config& cfg)
    : cfg_(cfg), linkUp_(false), self_(0), host_(0), failed_(false), navActive_(false), frameOff_(false),
      mismatch_(false), lastNavMs_(0), status_(kOffline), changed_(false), haveCurrent_(false), havePending_(false),
      nextCall_(0), batchStartMs_(0), recent_(cfg.replyMs), tag_(1), epoch_(0) {
    if (cfg_.maxInFlight == 0) cfg_.maxInFlight = 1;
    memset(&stats_, 0, sizeof(stats_));
}

void Core::dropWork() {
    stats_.batchesDropped += (haveCurrent_ ? 1 : 0) + (havePending_ ? 1 : 0);
    if (haveCurrent_ || havePending_) ++epoch_;
    haveCurrent_ = havePending_ = false;
    current_ = Batch();
    pending_ = Batch();
    nextCall_ = 0;
    inFlight_.clear();
}

void Core::refresh(const char* why, uint32_t) {
    Status s = (!linkUp_ || !self_ || !host_) ? kOffline : (failed_ || mismatch_) ? kFailed
             : navActive_ ? kNavActive : kReady;
    if (s == status_) return;
    printf("[HudTerm] %s -> %s (%s)\n", statusName(status_), statusName(s), why);
    if (status_ == kReady) ++epoch_;          // what we drew may be gone or overdrawn: the driver redraws
    if (s != kReady) dropWork();
    status_ = s;
    changed_ = true;
}

void Core::fail(const char* why, uint32_t now) {
    printf("[HudTerm] FAILED: %s\n", why);
    failed_ = true;
    refresh(why, now);
}

// The navigation state is kept across a link change: a route that ends while the link is down leaves us yielded until
// the navigation's next route ends, which is the safe side.
void Core::setLink(bool up, uint16_t self, uint32_t now) {
    if (!up) {
        if (!linkUp_) return;
        linkUp_ = false;          // self_ stays, so late answers to our old address are not taken for the navigation
        refresh("link down", now);
        return;
    }
    if (!isAppAddress(self)) {
        printf("[HudTerm] link up but address %04X is not an app address: nothing will be sent\n", self);
        linkUp_ = false;
        refresh("no usable address", now);
        return;
    }
    if (linkUp_ && self == self_) return;
    linkUp_ = true;
    self_ = self;
    failed_ = false;
    dropWork();
    char why[48];
    snprintf(why, sizeof(why), "link up, address %04X", self);
    refresh(why, now);
}

void Core::setHost(uint16_t host, uint32_t now) {
    if (!isRingNode(host)) host = 0;
    if (host == host_) return;
    char why[64];
    if (host) snprintf(why, sizeof(why), "82/00 host %04X%s", host, host_ ? " (changed)" : "");
    else snprintf(why, sizeof(why), "no 82/00 host on a ring node in the registry");
    host_ = host;
    failed_ = false;
    navActive_ = false;
    frameOff_ = false;
    recent_.clear();
    dropWork();
    refresh(why, now);
}

void Core::reset(uint32_t now) {
    if (!failed_) return;                     // a profile mismatch stays
    failed_ = false;
    refresh("reset", now);
}

SubmitResult Core::submit(const Batch& b, uint32_t) {
    if (b.calls.empty() || b.calls.size() > cfg_.maxBatch) {
        ++stats_.refused;
        printf("[HudTerm] batch '%s' refused: %u calls (1..%u)\n", b.label.c_str(), (unsigned)b.calls.size(), cfg_.maxBatch);
        return kRefused;
    }
    for (size_t i = 0; i < b.calls.size(); ++i) {
        const Call& c = b.calls[i];
        if (callOk(c, cfg_.handles)) continue;
        ++stats_.refused;
        printf("[HudTerm] batch '%s' refused: call %u, %03X %s handle %u, %u bytes, breaks a limit\n", b.label.c_str(),
               (unsigned)i, c.fkt, fktName(c.fkt), c.handle(), (unsigned)c.payload.size());
        return kRefused;
    }
    if (status_ != kReady) return kNotReady;
    if (!haveCurrent_) {
        ++stats_.batches;
        current_ = b;
        haveCurrent_ = true;
        nextCall_ = 0;
        batchStartMs_ = 0;
        return kQueued;
    }
    if (!havePending_) {
        ++stats_.batches;
        pending_ = b;
        havePending_ = true;
        return kQueued;
    }
    if (b.full) {
        ++stats_.batches;
        ++stats_.replaced;
        pending_ = b;
        return kReplaced;
    }
    if (!merge(b)) {
        ++stats_.refused;
        printf("[HudTerm] batch '%s' refused: merged into '%s' it would be over %u calls\n", b.label.c_str(),
               pending_.label.c_str(), cfg_.maxBatch);
        return kRefused;
    }
    ++stats_.merged;
    return kMerged;
}

// A partial batch into the waiting one: a text for a handle it already sets replaces that payload where it stands
// (the newest text wins, in the waiting batch's order), anything else is appended. Nothing changes if the result would
// be too long.
bool Core::merge(const Batch& b) {
    Batch m = pending_;
    for (size_t i = 0; i < b.calls.size(); ++i) {
        const Call& c = b.calls[i];
        size_t k = 0;
        if (c.fkt == kTextareaSetText)
            while (k < m.calls.size() && !(m.calls[k].fkt == kTextareaSetText && m.calls[k].handle() == c.handle())) ++k;
        else
            k = m.calls.size();
        if (k < m.calls.size()) m.calls[k].payload = c.payload;
        else m.calls.push_back(c);
    }
    if (m.calls.size() > cfg_.maxBatch) return false;
    pending_ = m;
    return true;
}

bool Core::next(uint32_t now, most::Msg& out) {
    if (status_ != kReady || !haveCurrent_ || nextCall_ >= current_.calls.size()) return false;
    if (inFlight_.size() >= cfg_.maxInFlight) return false;
    const Call& c = current_.calls[nextCall_];
    if (!buildCall(host_, c, tag_, cfg_.handles, out)) {   // checked at submit: only a host change could do this
        fail("a queued call no longer builds", now);
        return false;
    }
    if (nextCall_ == 0) batchStartMs_ = now;
    ++tag_;
    ++nextCall_;
    InFlight f;
    f.fkt = c.fkt;
    f.handle = c.handle();
    f.sentMs = now;
    inFlight_.push_back(f);
    recent_.add(c.fkt, f.handle, now);
    ++stats_.calls;
    return true;
}

void Core::sendFailed(uint32_t now) { fail("the MOST client did not send a call", now); }

void Core::finishIfDone(uint32_t now) {
    if (!haveCurrent_ || nextCall_ < current_.calls.size() || !inFlight_.empty()) return;
    ++stats_.batchesDone;
    if (cfg_.logBatches)
        printf("[HudTerm] batch '%s': %u calls answered in %u ms\n", current_.label.c_str(),
               (unsigned)current_.calls.size(), now - batchStartMs_);
    haveCurrent_ = false;
    if (!havePending_) {
        current_ = Batch();
        return;
    }
    current_ = pending_;
    pending_ = Batch();
    havePending_ = false;
    haveCurrent_ = true;
    nextCall_ = 0;
}

void Core::onMessage(const most::View& m, uint32_t now) {
    if (m.fblock != kFBlock || m.inst != kInst || !host_) return;
    if (self_ && m.src == self_) return;                      // our own, if the server shows them to us
    if (self_ && m.dst == self_) {
        if (m.src == host_) onAnswer(m, now);
        return;
    }
    if (m.src != host_ && m.dst != host_) return;             // not this terminal
    if (m.src == host_ && recent_.isCopy(m.fkt, m.data, m.len, now)) {
        ++stats_.copies;
        return;
    }
    onNav(m, now);
}

// ErrorAck (9) carries the handle first, like every answer to an Ack method; Error (F) does not, so it matches on the
// function alone. An error fails the terminal even when it matches nothing in flight: one of our calls is wrong.
void Core::onAnswer(const most::View& m, uint32_t now) {
    if (m.op == most::kOpProcessingAck || m.op == most::kOpProcessing) return;   // still working: the timeout stands
    bool error = m.op != most::kOpResultAck && m.op != most::kOpStatus;
    bool byHandle = m.op != most::kOpError && m.len >= 2;
    uint16_t h = byHandle ? rdBe16(m.data) : 0;
    size_t i = 0;
    while (i < inFlight_.size() && !(inFlight_[i].fkt == m.fkt && (!byHandle || inFlight_[i].handle == h))) ++i;
    bool matched = i < inFlight_.size();
    if (matched) inFlight_.erase(inFlight_.begin() + i);
    if (error) {
        ++stats_.errors;
        char why[160];
        int n = snprintf(why, sizeof(why), "%s(%X) to %03X %s%s, payload", most::opName(m.op), m.op, m.fkt, fktName(m.fkt),
                         matched ? "" : " (matches no call in flight)");
        for (size_t k = 0; k < m.len && k < 12 && n < (int)sizeof(why) - 4; ++k)
            n += snprintf(why + n, sizeof(why) - n, " %02x", m.data[k]);
        fail(why, now);
        return;
    }
    if (!matched) {
        ++stats_.strays;
        return;
    }
    ++stats_.answers;
    finishIfDone(now);
}

// A navigation call (to the host) that does not fit the driver's profile: text on an instance that is not one of its
// texts, or a swap of instances that are not its buffer pair.
void Core::checkNav(const most::View& m, uint32_t now) {
    if (!cfg_.checkNav || mismatch_ || m.dst != host_ || m.len < 2) return;
    uint16_t h = rdBe16(m.data);
    char why[128];
    if (m.fkt == kTextareaSetText && !cfg_.navTexts.has(h)) {
        snprintf(why, sizeof(why), "profile mismatch: the factory navigation put text on instance %u", h);
    } else if (m.fkt == kSwitchVisibility && m.len >= 4 &&
               (!cfg_.navBuffers.has(h) || !cfg_.navBuffers.has(rdBe16(m.data + 2)))) {
        snprintf(why, sizeof(why), "profile mismatch: the factory navigation swapped instances %u and %u", h,
                 rdBe16(m.data + 2));
    } else {
        return;
    }
    mismatch_ = true;
    printf("[HudTerm] FAILED: %s; this cluster is not laid out as the profile says: nothing is sent until the "
           "daemon restarts\n", why);
    refresh(why, now);
}

void Core::onNav(const most::View& m, uint32_t now) {
    ++stats_.navMsgs;
    lastNavMs_ = now;
    checkNav(m, now);
    if (m.fkt == kSetVisibility && m.len >= 3 && cfg_.navFrame > 0 && rdBe16(m.data) == cfg_.navFrame)
        frameOff_ = m.data[2] == 0;
    if (navActive_) return;
    navActive_ = true;
    char why[96];
    snprintf(why, sizeof(why), "factory navigation: %04X->%04X %03X %s %s(%X) len %u", m.src, m.dst, m.fkt, fktName(m.fkt),
             most::opName(m.op), m.op, (unsigned)m.len);
    refresh(why, now);
}

void Core::tick(uint32_t now) {
    if (status_ == kReady && !inFlight_.empty() && left(inFlight_[0].sentMs, cfg_.replyMs, now) == 0) {
        ++stats_.timeouts;
        char why[96];
        snprintf(why, sizeof(why), "no answer to %03X %s handle %u in %u ms", inFlight_[0].fkt, fktName(inFlight_[0].fkt),
                 inFlight_[0].handle, cfg_.replyMs);
        fail(why, now);
    }
    if (navActive_ && frameOff_ && left(lastNavMs_, cfg_.navQuietMs, now) == 0) {
        navActive_ = false;
        frameOff_ = false;
        refresh("factory navigation finished: frame off, then quiet", now);
    }
}

uint32_t Core::msUntilDeadline(uint32_t now) const {
    uint32_t d = 0xFFFFFFFFu;
    if (status_ == kReady && !inFlight_.empty()) {
        uint32_t t = left(inFlight_[0].sentMs, cfg_.replyMs, now);
        if (t < d) d = t;
    }
    if (navActive_ && frameOff_) {
        uint32_t t = left(lastNavMs_, cfg_.navQuietMs, now);
        if (t < d) d = t;
    }
    return d;
}

bool Core::takeStatusChange(Status& s) {
    if (!changed_) return false;
    changed_ = false;
    s = status_;
    return true;
}

// ---- HudTerminal ---------------------------------------------------------------------------------------------------

HudTerminal::HudTerminal(const Config& cfg)
    : core_(cfg), client_(NULL), send_(NULL), sendUser_(NULL), statusCb_(NULL), statusUser_(NULL), running_(false),
      stop_(false) {
    memset(&lastHealth_, 0, sizeof(lastHealth_));
    pthread_mutex_init(&lock_, NULL);
    pthread_condattr_t a;
    pthread_condattr_init(&a);
    pthread_condattr_setclock(&a, CLOCK_MONOTONIC);
    pthread_cond_init(&cond_, &a);
    pthread_condattr_destroy(&a);
}

HudTerminal::~HudTerminal() {
    stop();
    pthread_cond_destroy(&cond_);
    pthread_mutex_destroy(&lock_);
}

bool HudTerminal::attach(most::Client& c, bool sending) {
    client_ = &c;
    if (!send_) {
        send_ = &HudTerminal::clientSend;
        sendUser_ = this;
    }
    if (sending && !c.allowSend(kFBlock, kInst)) return false;
    return c.addRoute(kFBlock, kInst, most::kAny, most::kAny, &HudTerminal::msgTramp, this) &&
           c.addLinkListener(&HudTerminal::linkTramp, this) && c.addRegistryListener(&HudTerminal::regTramp, this);
}

void HudTerminal::setSender(SendFn fn, void* user) {
    send_ = fn;
    sendUser_ = user;
}

void HudTerminal::setStatusListener(StatusCb cb, void* user) {
    statusCb_ = cb;
    statusUser_ = user;
}

bool HudTerminal::start() {
    if (running_) return false;
    stop_ = false;
    if (pthread_create(&thread_, NULL, &HudTerminal::threadMain, this) != 0) {
        printf("[HudTerm] could not start the send thread\n");
        return false;
    }
    running_ = true;
    return true;
}

void HudTerminal::stop() {
    if (!running_) return;
    pthread_mutex_lock(&lock_);
    stop_ = true;
    pthread_cond_signal(&cond_);
    pthread_mutex_unlock(&lock_);
    pthread_join(thread_, NULL);
    running_ = false;
}

void HudTerminal::wake() { pthread_cond_signal(&cond_); }

SubmitResult HudTerminal::submit(const Batch& b) {
    pthread_mutex_lock(&lock_);
    SubmitResult r = core_.submit(b, nowMs());
    wake();
    pthread_mutex_unlock(&lock_);
    return r;
}

void HudTerminal::reset() {
    pthread_mutex_lock(&lock_);
    core_.reset(nowMs());
    wake();
    pthread_mutex_unlock(&lock_);
}

Status HudTerminal::status() const {
    pthread_mutex_lock(&lock_);
    Status s = core_.status();
    pthread_mutex_unlock(&lock_);
    return s;
}

uint32_t HudTerminal::epoch() const {
    pthread_mutex_lock(&lock_);
    uint32_t e = core_.epoch();
    pthread_mutex_unlock(&lock_);
    return e;
}

// Polled: it is only used for a clear, where a short wait does not matter.
bool HudTerminal::waitIdle(uint32_t ms) {
    uint32_t t0 = nowMs();
    for (;;) {
        pthread_mutex_lock(&lock_);
        Status s = core_.status();
        bool busy = core_.busy();
        pthread_mutex_unlock(&lock_);
        if (s != kReady) return false;
        if (!busy) return true;
        if (nowMs() - t0 >= ms) return false;
        struct timespec d = { 0, 10 * 1000000 };
        nanosleep(&d, NULL);
    }
}

TerminalStats HudTerminal::stats() const {
    pthread_mutex_lock(&lock_);
    TerminalStats s = core_.stats();
    pthread_mutex_unlock(&lock_);
    return s;
}

void HudTerminal::logHealth() {
    pthread_mutex_lock(&lock_);
    TerminalStats t = core_.stats(), l = lastHealth_;
    lastHealth_ = t;
    Status s = core_.status();
    uint16_t host = core_.host();
    pthread_mutex_unlock(&lock_);
    printf("[HudTerm] %s, host %04X: batches %u (done %u, dropped %u, replaced %u, merged %u, refused %u), calls %u, "
           "answers %u, copies %u, strays %u, nav msgs %u, timeouts %u, errors %u\n",
           statusName(s), host, t.batches - l.batches, t.batchesDone - l.batchesDone, t.batchesDropped - l.batchesDropped,
           t.replaced - l.replaced, t.merged - l.merged, t.refused - l.refused, t.calls - l.calls, t.answers - l.answers, t.copies - l.copies,
           t.strays - l.strays, t.navMsgs - l.navMsgs, t.timeouts - l.timeouts, t.errors - l.errors);
}

void HudTerminal::onMessage(const most::View& m) {
    pthread_mutex_lock(&lock_);
    core_.onMessage(m, nowMs());
    wake();
    pthread_mutex_unlock(&lock_);
}

void HudTerminal::onLink(bool up, uint16_t self) {
    pthread_mutex_lock(&lock_);
    core_.setLink(up, self, nowMs());
    wake();
    pthread_mutex_unlock(&lock_);
}

void HudTerminal::onRegistry(const std::vector<most::RegistryEntry>& reg) {
    uint16_t host = terminalHost(reg);
    pthread_mutex_lock(&lock_);
    core_.setHost(host, nowMs());
    wake();
    pthread_mutex_unlock(&lock_);
}

void HudTerminal::msgTramp(const most::View& m, void* self) { static_cast<HudTerminal*>(self)->onMessage(m); }

// On the DSI callback thread, after the client released its lock, so selfAddress() is safe here.
void HudTerminal::linkTramp(most::LinkState s, void* self) {
    HudTerminal* t = static_cast<HudTerminal*>(self);
    bool up = s == most::kUp;
    t->onLink(up, up && t->client_ ? t->client_->selfAddress() : 0);
}

void HudTerminal::regTramp(const std::vector<most::RegistryEntry>& reg, void* self) {
    static_cast<HudTerminal*>(self)->onRegistry(reg);
}

bool HudTerminal::clientSend(const most::Msg& m, void* self) {
    HudTerminal* t = static_cast<HudTerminal*>(self);
    return t->client_ && t->client_->send(m);
}

void* HudTerminal::threadMain(void* self) {
    static_cast<HudTerminal*>(self)->run();
    return NULL;
}

// Sends with the lock released, so the DSI callback thread never waits behind a send; the call is already in flight
// when its answer can arrive.
void HudTerminal::run() {
    pthread_mutex_lock(&lock_);
    while (!stop_) {
        uint32_t now = nowMs();
        core_.tick(now);
        most::Msg m;
        while (!stop_ && core_.next(now, m)) {
            SendFn fn = send_;
            void* user = sendUser_;
            pthread_mutex_unlock(&lock_);
            bool ok = fn && fn(m, user);
            pthread_mutex_lock(&lock_);
            now = nowMs();
            if (!ok) core_.sendFailed(now);
        }
        Status s;
        if (core_.takeStatusChange(s)) {
            StatusCb cb = statusCb_;
            void* user = statusUser_;
            pthread_mutex_unlock(&lock_);
            if (cb) cb(s, user);
            pthread_mutex_lock(&lock_);
            continue;
        }
        uint32_t wait = core_.msUntilDeadline(now);
        if (wait > 1000) wait = 1000;
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        uint64_t ns = (uint64_t)ts.tv_nsec + (uint64_t)(wait + 1) * 1000000ull;
        ts.tv_sec += (time_t)(ns / 1000000000ull);
        ts.tv_nsec = (long)(ns % 1000000000ull);
        int rc = pthread_cond_timedwait(&cond_, &lock_, &ts);
        (void)rc;
    }
    pthread_mutex_unlock(&lock_);
}

} // namespace hudterm
} // namespace nbt
