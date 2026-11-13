// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "most/latency_diag.hpp"
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
    : replyTag_(0xffffu), cfg_(cfg), linkUp_(false), self_(0), host_(0), failed_(false), navActive_(cfg.initialNavActive),
      routeOn_(cfg.initialNavActive), frameOff_(false),
      lastNavMs_(0), status_(kOffline), changed_(false), haveCurrent_(false), havePending_(false),
      nextCall_(0), batchStartMs_(0), recent_(cfg.replyMs), tag_(1), epoch_(0) {
    if (cfg_.maxInFlight == 0) cfg_.maxInFlight = 1;
    memset(&stats_, 0, sizeof(stats_));
}

void Core::dropWork(uint32_t now) {
    stats_.batchesDropped += (haveCurrent_ ? 1 : 0) + (havePending_ ? 1 : 0);
    if (haveCurrent_ || havePending_) ++epoch_;
    haveCurrent_ = havePending_ = false;
    current_ = Batch();
    pending_ = Batch();
    nextCall_ = 0;
    for (size_t i = 0; i < inFlight_.size(); ++i) {
        InFlight f = inFlight_[i];
        f.sentMs = now;
        dropped_.push_back(f);
    }
    inFlight_.clear();
}

// An answer to a call dropped within replyMs: taken off the list. Older entries are let go.
bool Core::takeDropped(uint16_t fkt, bool byHandle, uint16_t handle, uint32_t now) {
    size_t k = 0;
    for (size_t i = 0; i < dropped_.size(); ++i)
        if (left(dropped_[i].sentMs, cfg_.replyMs, now) > 0) dropped_[k++] = dropped_[i];
    dropped_.resize(k);
    for (size_t i = 0; i < dropped_.size(); ++i) {
        if (dropped_[i].fkt != fkt || (byHandle && dropped_[i].handle != handle)) continue;
        dropped_.erase(dropped_.begin() + i);
        return true;
    }
    return false;
}

void Core::refresh(const char* why, uint32_t now) {
    Status s = (!linkUp_ || !self_ || !host_) ? kOffline : failed_ ? kFailed
             : navActive_ ? kNavActive : kReady;
    if (s == status_) return;
    printf("[HudTerm] %s -> %s (%s)\n", statusName(status_), statusName(s), why);
    if (status_ == kReady) ++epoch_;          // what we drew may be gone or overdrawn: the driver redraws
    if (s != kReady) dropWork(now);
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
        if (cfg_.initialNavActive) { navActive_=true; routeOn_=true; frameOff_=false; sizes_.clear(); }
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
    if (cfg_.initialNavActive) { navActive_=true; routeOn_=true; frameOff_=false; }
    sizes_.clear();
    failed_ = false;
    dropWork(now);
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
    navActive_ = cfg_.initialNavActive;
    routeOn_ = cfg_.initialNavActive;
    frameOff_ = false;
    sizes_.clear();
    recent_.clear();
    echoes_.clear();
    dropWork(now);
    dropped_.clear();                          // answers from the old host are not ours to match
    refresh(why, now);
}

void Core::reset(uint32_t now) {
    if (!failed_) return;
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
        if (callOk(c, cfg_.handles, cfg_.extendedCalls)) continue;
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
    if (!buildCall(host_, c, tag_, cfg_.handles, out, cfg_.extendedCalls)) {   // checked at submit: only a host change could do this
        fail("a queued call no longer builds", now);
        return false;
    }
    if (nextCall_ == 0) batchStartMs_ = now;
    out.txType = c.txType >= 0 ? (uint32_t)c.txType : cfg_.txType;
    if (cfg_.useFixedTag) out.hdrTag = cfg_.fixedTag;
    const uint32_t replyTag = cfg_.useFixedTag && cfg_.fixedTag == 0xffffu ? replyTag_ : out.hdrTag;
    if (cfg_.strictAnswers && c.fkt == kGetSize) {
        if (replyTag == 0xffffu) { fail("no registered return tag for ImageGetSize", now); return false; }
        for (size_t i=0; i<sizeQueries_.size(); ++i) {
            const SizeQuery& q=sizeQueries_[i];
            if(q.host==host_ && q.self==self_ && q.tag==replyTag && q.handle==c.handle()) {
                fail("ImageGetSize already issued for this registration; refusing ambiguous retry", now);
                return false;
            }
        }
        if (sizeQueries_.size() >= 128) { fail("ImageGetSize registration history full", now); return false; }
        SizeQuery q; q.host=host_; q.self=self_; q.tag=replyTag; q.handle=c.handle();
        sizeQueries_.push_back(q);
        printf("[HudTerm] +%u DISPATCH ImageGetSize host %04X self %04X tag %u handle %04X deadline +%u ms\n",
               now, host_, self_, replyTag, c.handle(), cfg_.sizeReplyMs ? cfg_.sizeReplyMs : cfg_.replyMs);
    }
    if(latencydiag::enabled())latencydiag::record(latencydiag::Dispatch,latencydiag::metadata(out),0,tag_,this);
    ++tag_;
    ++nextCall_;
    InFlight f;
    f.fkt = c.fkt;
    f.handle = c.handle();
    f.sentMs = now;
    f.tag = replyTag;
    f.warned = false;
    f.diagnosticId = tag_-1;
    inFlight_.push_back(f);
    recent_.add(c.fkt, f.handle, now);
    if(cfg_.strictAnswers) {
        Echo e; e.fkt=c.fkt; e.ms=now; e.tag=replyTag; e.payload=c.payload;
        if(echoes_.size()>=128)echoes_.erase(echoes_.begin());
        echoes_.push_back(e);
    }
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
    if (cfg_.strictAnswers && m.src == host_ && m.fkt == kGetSize &&
        (m.dst == self_ || cfg_.acceptMirroredAnswers)) {
        onAnswer(m, now);
        return;
    }
    if (self_ && m.src == self_) return;                      // our own, if the server shows them to us
    if (self_ && m.dst == self_) {
        if (m.src == host_) onAnswer(m, now);
        return;
    }
    if (m.src != host_ && m.dst != host_) return;             // not this terminal
    if (cfg_.acceptMirroredAnswers && m.src==host_ && ownEcho(m,now)) {
        // Some NBT terminals mirror results only to the original navigation client.
        // Accept only matching function+handle AND method-specific payload, never an arbitrary host packet.
        onAnswer(m,now); ++stats_.copies; return;
    }
    if (!cfg_.strictAnswers && m.src == host_ && recent_.isCopy(m.fkt, m.data, m.len, now)) {
        ++stats_.copies;
        return;
    }
    onNav(m, now);
}

bool Core::ownEcho(const most::View& m,uint32_t now) const {
    if(m.len<2 || !m.data)return false;
    for(size_t i=echoes_.size();i>0;--i) {
        const Echo& e=echoes_[i-1];
        if(e.fkt!=m.fkt || e.tag!=m.hdrTag || now-e.ms>=cfg_.replyMs || e.payload.size()<2 ||
           e.payload[0]!=m.data[0] || e.payload[1]!=m.data[1])continue;
        if(m.op==most::kOpErrorAck)return m.len>=3;
        if(m.op!=most::kOpResultAck && m.op!=most::kOpStatus &&
           m.op!=most::kOpProcessingAck && m.op!=most::kOpProcessing)return false;
        if(m.op==most::kOpProcessingAck || m.op==most::kOpProcessing)return true;
        if(m.fkt==kGetSize)return m.len==6 && rdBe16(m.data+2)>0 && rdBe16(m.data+4)>0;
        if(m.fkt==0xC05)return m.len==4 && e.payload.size()>=10 && m.data[2]==e.payload[9] && m.data[3]==1;
        size_t n=e.payload.size();
        if(m.fkt==kTextareaSetText) {
            if(n>=4 && e.payload[2]==1 && m.len==n-1) --n;
            else if(n>=5 && e.payload[2]==0 && m.len==n-2) n-=2;
        }
        if(m.len==n && !memcmp(m.data,&e.payload[0],n))return true;
    }
    return false;
}

// ErrorAck (9) carries the handle first, like every answer to an Ack method; Error (F) does not, so it matches on the
// function alone. An error fails the terminal even when it matches nothing in flight: one of our calls is wrong. Only
// an answer to a call we dropped a moment ago is let go (see the header).
void Core::onAnswer(const most::View& m, uint32_t now) {
    if(latencydiag::enabled())latencydiag::record(latencydiag::CoreAnswer,
        latencydiag::metadata(m),0,inFlight_.empty()?0:inFlight_[0].diagnosticId,this);
    if (cfg_.strictAnswers && m.fkt == kGetSize) {
        // Expiry is evaluated on receipt too, even if the send thread has not
        // ticked yet. Validation never consumes or extends a pending request.
        tick(now);
        size_t i=0;
        while(i<inFlight_.size() && !(inFlight_[i].fkt==m.fkt &&
              inFlight_[i].tag==m.hdrTag && m.data && m.len>=2 &&
              inFlight_[i].handle==rdBe16(m.data))) ++i;
        if(status_!=kReady || i==inFlight_.size()) { ++stats_.strays; return; }
        if(m.op==most::kOpProcessingAck || m.op==most::kOpProcessing) return;
        if(m.op==most::kOpResultAck) {
            // F31 capture: handle BE16, width BE16, height BE16. No selector
            // byte in the result; the old seven-byte shape was synthetic only.
            if(m.len!=6 || !rdBe16(m.data+2) || !rdBe16(m.data+4)) {
                ++stats_.strays; return;
            }
            const InFlight f=inFlight_[i];
            Size sz; sz.handle=f.handle; sz.width=rdBe16(m.data+2); sz.height=rdBe16(m.data+4);
            size_t k=0; while(k<sizes_.size() && sizes_[k].handle!=sz.handle) ++k;
            if(k==sizes_.size()) sizes_.push_back(sz); else sizes_[k]=sz;
            inFlight_.erase(inFlight_.begin()+i);
            ++stats_.answers;
            printf("[HudTerm] +%u RX ImageGetSize tag %u handle %04X %ux%u elapsed %u ms\n",
                   now, m.hdrTag, sz.handle, sz.width, sz.height, now-f.sentMs);
            finishIfDone(now);
            return;
        }
        // Unacknowledged Error has no proven handle correlation here.
        if(m.op!=most::kOpErrorAck || m.len<3) { ++stats_.strays; return; }
        // A correlated error still follows the existing fail-closed path.
    }
    if (m.op == most::kOpProcessingAck || m.op == most::kOpProcessing) return;   // still working: the timeout stands
    if (cfg_.strictAnswers && (m.len < 2 || !m.data)) { ++stats_.strays; return; }
    if (cfg_.strictAnswers && (m.op==most::kOpResultAck || m.op==most::kOpStatus) && !ownEcho(m,now)) {
        ++stats_.strays; return;
    }
    bool error = m.op != most::kOpResultAck && m.op != most::kOpStatus;
    bool byHandle = m.op != most::kOpError && m.len >= 2;
    uint16_t h = byHandle ? rdBe16(m.data) : 0;
    size_t i = 0;
    while (i < inFlight_.size() && !(inFlight_[i].fkt == m.fkt && (!cfg_.strictAnswers || inFlight_[i].tag == m.hdrTag) && (!byHandle || inFlight_[i].handle == h))) ++i;
    bool matched = i < inFlight_.size();
    if (matched) inFlight_.erase(inFlight_.begin() + i);
    if (error && !matched && takeDropped(m.fkt, byHandle, h, now)) {
        ++stats_.lateErrors;
        printf("[HudTerm] %s(%X) to %03X %s handle %u, a call dropped when the terminal left ready: not a failure\n",
               most::opName(m.op), m.op, m.fkt, fktName(m.fkt), h);
        return;
    }
    if (cfg_.strictAnswers && !matched) { ++stats_.strays; return; }
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
        takeDropped(m.fkt, byHandle, h, now);
        ++stats_.strays;
        return;
    }
    ++stats_.answers;
    finishIfDone(now);
}

// Whether a navigation message (its call, or the cluster's answer with the call's payload) draws: a route is on screen.
// Clearing up (RemoveAllItems, empty texts, other instances switched off) is not drawing.
static bool draws(const most::View& m) {
    switch (m.fkt) {
        case kSwitchVisibility: case kAddPolygon: case kAddCircle: case kAddCircleDiameter: case 0xC05:   // ImageAddBitmap
            return true;
        case kTextareaSetText:                    // handle, encoding byte, then the text
            for (size_t i = 3; i < m.len; ++i)
                if (m.data[i]) return true;
            return false;
        default:
            return false;
    }
}

void Core::onNav(const most::View& m, uint32_t now) {
    ++stats_.navMsgs;
    if (cfg_.ignoreFactoryNav) return;
    lastNavMs_ = now;
    if (m.fkt == kSetVisibility && m.len >= 3 && cfg_.navFrame > 0 && rdBe16(m.data) == cfg_.navFrame) {
        routeOn_ = m.data[2] != 0;
        frameOff_ = !routeOn_;
    } else if (draws(m) && !frameOff_) {
        routeOn_ = true;
    }
    if (navActive_) return;
    navActive_ = true;
    char why[96];
    snprintf(why, sizeof(why), "factory navigation: %04X->%04X %03X %s %s(%X) len %u", m.src, m.dst, m.fkt, fktName(m.fkt),
             most::opName(m.op), m.op, (unsigned)m.len);
    refresh(why, now);
}

void Core::tick(uint32_t now) {
    if (status_ == kReady && !inFlight_.empty() && inFlight_[0].fkt==kGetSize &&
        cfg_.sizeReplyMs>cfg_.replyMs && !inFlight_[0].warned &&
        left(inFlight_[0].sentMs,cfg_.replyMs,now)==0 &&
        left(inFlight_[0].sentMs,deadline(inFlight_[0]),now)>0) {
        inFlight_[0].warned=true;
        printf("[HudTerm] +%u ImageGetSize handle %04X diagnostic threshold %u ms; same request pending until +%u\n",
               now,inFlight_[0].handle,cfg_.replyMs,inFlight_[0].sentMs+deadline(inFlight_[0]));
    }
    if (status_ == kReady && !inFlight_.empty() && left(inFlight_[0].sentMs, deadline(inFlight_[0]), now) == 0) {
        if(latencydiag::enabled()) {
            latencydiag::Meta m;m.src=self_;m.dst=host_;m.fb=kFBlock;m.inst=kInst;
            m.fkt=inFlight_[0].fkt;m.handle=inFlight_[0].handle;m.tag=inFlight_[0].tag;
            latencydiag::record(latencydiag::Timeout,m,0,inFlight_[0].diagnosticId,this);
        }
        ++stats_.timeouts;
        char why[96];
        snprintf(why, sizeof(why), "no answer to %03X %s handle %u in %u ms (sent +%u, now +%u)", inFlight_[0].fkt, fktName(inFlight_[0].fkt),
                 inFlight_[0].handle, deadline(inFlight_[0]), inFlight_[0].sentMs, now);
        fail(why, now);
    }
    if (navActive_ && !routeOn_ && left(lastNavMs_, cfg_.navQuietMs, now) == 0) {
        navActive_ = false;
        frameOff_ = false;                     // the next drawing, frame-on seen or not, is a route again
        refresh("factory navigation finished: no route on screen, then quiet", now);
    }
}

uint32_t Core::msUntilDeadline(uint32_t now) const {
    uint32_t d = 0xFFFFFFFFu;
    if (status_ == kReady && !inFlight_.empty()) {
        uint32_t t = left(inFlight_[0].sentMs, deadline(inFlight_[0]), now);
        if (inFlight_[0].fkt==kGetSize && cfg_.sizeReplyMs>cfg_.replyMs && !inFlight_[0].warned) {
            uint32_t diagnostic=left(inFlight_[0].sentMs,cfg_.replyMs,now);
            if(diagnostic<t)t=diagnostic;
        }
        if (t < d) d = t;
    }
    if (navActive_ && !routeOn_) {
        uint32_t t = left(lastNavMs_, cfg_.navQuietMs, now);
        if (t < d) d = t;
    }
    return d;
}

bool Core::imageSize(uint16_t h, uint16_t& w, uint16_t& height) const {
    for(size_t i=0;i<sizes_.size();++i) if(sizes_[i].handle==h) { w=sizes_[i].width; height=sizes_[i].height; return true; }
    return false;
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
      stop_(false), registeredTag_(cfg.useFixedTag && cfg.fixedTag == 0xffffu) {
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

bool HudTerminal::waiting() const {
    pthread_mutex_lock(&lock_);
    bool w = core_.waiting();
    pthread_mutex_unlock(&lock_);
    return w;
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

bool HudTerminal::imageSize(uint16_t h, uint16_t& w, uint16_t& height) const {
    pthread_mutex_lock(&lock_);
    bool ok=core_.imageSize(h,w,height);
    pthread_mutex_unlock(&lock_);return ok;
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
           "answers %u, copies %u, strays %u, nav msgs %u, timeouts %u, errors %u (late %u)\n",
           statusName(s), host, t.batches - l.batches, t.batchesDone - l.batchesDone, t.batchesDropped - l.batchesDropped,
           t.replaced - l.replaced, t.merged - l.merged, t.refused - l.refused, t.calls - l.calls, t.answers - l.answers, t.copies - l.copies,
           t.strays - l.strays, t.navMsgs - l.navMsgs, t.timeouts - l.timeouts, t.errors - l.errors,
           t.lateErrors - l.lateErrors);
}

void HudTerminal::onMessage(const most::View& m) {
    if(latencydiag::enabled())latencydiag::record(latencydiag::TerminalEntry,latencydiag::metadata(m),0,0,this);
    const uint32_t entryMs=nowMs();
    pthread_mutex_lock(&lock_);
    const uint32_t coreMs=nowMs();
    const bool diagnostic=registeredTag_ && m.fblock==kFBlock && m.inst==kInst && m.fkt==kGetSize;
    core_.onMessage(m, coreMs);
    wake();
    pthread_mutex_unlock(&lock_);
    if(diagnostic)
        printf("[HudTerm] ImageGetSize receive DSI callback +%u MOST +%u terminal +%u core +%u tag %u len %u\n",
               m.deliveryMs,m.rxMs,entryMs,coreMs,m.hdrTag,(unsigned)m.len);
}

void HudTerminal::onLink(bool up, uint16_t self) {
    pthread_mutex_lock(&lock_);
    if (up && client_) core_.setReplyTag(client_->registeredHeaderTag());
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
    return t->client_ && t->client_->send(m, t->registeredTag_);
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
            const uint32_t dispatchMs = nowMs();
            bool ok = fn && fn(m, user);
            if (registeredTag_ && m.fkt == kGetSize)
                printf("[HudTerm] +%u ImageGetSize send returned %s after %u ms\n",
                       nowMs(), ok ? "OK" : "FAILED", nowMs()-dispatchMs);
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
