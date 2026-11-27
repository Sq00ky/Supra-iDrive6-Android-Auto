// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "cluster/hud6wa_display.hpp"
#include "fake_dsi_client.hpp"
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
using namespace nbt;
using namespace nbt::hudterm;
const uint16_t host=0x0123, self=0x2077, nav=0x2033;

static Config config() {
    Config c; c.handles=handlesOf(k6wa); c.navFrame=1; c.measureHandle=5; c.nativeBars=compactNativeBarProfile(k6wa);
    return c;
}
static most::Msg incoming(uint16_t fkt,uint8_t op,const std::vector<uint8_t>& p,uint16_t dst=self) {
    most::Msg m; m.txType=0;m.varTag=2;m.varVal=0;m.src=host;m.dst=dst;m.fblock=kFBlock;m.inst=0;
    m.fkt=fkt;m.opType=op;m.hdrTag=0;m.data=p;return m;
}
static void feed(Core& c,const most::Msg& m,uint32_t now) {
    std::vector<uint8_t> bytes=most::buildMessage(m);most::View v;assert(most::parseView(&bytes[0],bytes.size(),v));c.onMessage(v,now);
}
static void deliver(const most::Msg& m) {
    std::vector<uint8_t> b=most::buildMessage(m);fake_dsi::data(most::kInfoMessage,&b[0],b.size());
}
static void deliverTerminal(HudTerminal& t,const most::Msg& m) {
    std::vector<uint8_t> b=most::buildMessage(m);most::View v;
    assert(most::parseView(&b[0],b.size(),v));t.onMessage(v);
}
static most::Msg size(int w=126,int h=77,int marker=-1,uint8_t op=most::kOpResultAck) {
    std::vector<uint8_t> p=getSize(5);if(marker>=0)p.push_back(marker);putBe16(p,w);putBe16(p,h);
    return incoming(kGetSize,op,p);
}
static void ready(Core& c) {c.setLink(true,self,0);c.setHost(host,0);}
static void ask(Core& c,uint32_t now=0) {most::Msg m;assert(c.next(now,m)&&m.fkt==kGetSize);}
static int draw(Core& c,uint32_t now=20,bool distance=false,int count=1) {
    Batch b("draw");for(int i=1;i<count;++i)b.add(kSetFillColor,setFillColor(5,4));
    b.add(distance?kTextareaSetText:kSetVisibility,distance?setText(3,"120"):setVisibility(1,true));
    assert(c.submit(b,now)==kQueued);most::Msg m;bool native;int n=0,normal=0;
    while(c.next(now,m,&native)) {
        if(native) {
            assert(++n<=2);assert(m.fkt==kSetVisibility&&m.data==setVisibility(20+n,false));m.src=self;
            assert(c.nativeSendAllowed(m,c.nativeGeneration(),now));
        } else ++normal;
        feed(c,incoming(m.fkt,most::kOpResultAck,m.data),now);
    }
    assert(normal==count);return n;
}
static void testProof() {
    for(int form=0;form<14;++form) {
        Core c(config());ready(c);ask(c);
        most::Msg m=size();bool good=false;
        switch(form) {
        case 0:good=true;break;
        case 1:m=size(126,77,1);good=true;break;
        case 2:m=size(126,77,0);break;
        case 3:m=size(126,77,2);break;
        case 4:m=size(126,77,-1,most::kOpStatus);break;
        case 5:m=size(0,0);break;
        case 6:m=size(65535,77);break;
        case 7:m=size(176,126);break;
        case 8:m=size(221,158,1);break;
        case 9:m.data.pop_back();break;
        case 10:m.data.push_back(0);m.data.push_back(0);break;
        case 11:m=size(23,77);break;
        case 12:m.varTag=1;break;
        case 13:m.varVal=4;break;
        }
        feed(c,m,10);assert(draw(c)==(good?2:0));
        if(form==4) { // Legacy geometry may already equal the valid reply; proof must still wake the driver.
            uint32_t e=c.epoch();feed(c,size(),25);assert(c.epoch()!=e);assert(draw(c,30)==2);
        }
    }
    for(int wrong=0;wrong<5;++wrong) {
        Core c(config());ready(c);ask(c);most::Msg m=size();
        if(wrong==0)m.src=0x0140;
        if(wrong==1)m.dst=0x2078;
        if(wrong==2)m.inst=1;
        if(wrong==3)m.fblock=0x81;
        if(wrong==4)m.data[1]=6;
        feed(c,m,10);assert(draw(c)==0);
    }
    Core c(config());ready(c);feed(c,size(),0);ask(c);assert(draw(c)==0); // unsolicited
    puts("  proof: current primary, requested handle, ResultAck, marker, bounds and late equal-size upgrade");
}
static void testResetAndWhitelist() {
    Core c(config());ready(c);ask(c);feed(c,size(),1);assert(draw(c)==2);
    Batch raw("raw");raw.add(kSetVisibility,setVisibility(21,false));assert(c.submit(raw,30)==kRefused);
    raw.calls.clear();raw.add(kRemoveAllItems,removeAllItems(22));assert(c.submit(raw,30)==kRefused);
    uint32_t generation=c.nativeGeneration();int w,h;c.identityChanged();assert(c.measuredSize(w,h)&&w==126);
    most::Msg stale=incoming(kSetVisibility,most::kOpStartResultAck,setVisibility(21,false),host);stale.src=self;
    assert(!c.nativeSendAllowed(stale,generation,30));feed(c,size(),31);ask(c,32);assert(draw(c,33)==0);
    feed(c,size(),34);assert(draw(c,35)==2); // one new probe per registration identity
    c.setLink(false,0,40);c.setLink(true,self,41);ask(c,42);assert(draw(c,43)==0);feed(c,size(),44);assert(draw(c,45)==2);
    c.setHost(0x0140,50);assert(!c.measuredSize(w,h));c.setHost(host,51);ask(c,52);assert(draw(c,53)==0);
    feed(c,size(),54);assert(draw(c,55,true)==2); // distance redraw
    most::Msg moved=incoming(kGuidanceMovedFkt,most::kOpStatus,std::vector<uint8_t>(1,1));moved.fblock=kGuidanceFBlock;
    std::vector<uint8_t> bytes=most::buildMessage(moved);most::View v;assert(most::parseView(&bytes[0],bytes.size(),v));
    c.guidanceMoved(v);assert(c.measuredSize(w,h));ask(c,60);assert(draw(c,61)==0);feed(c,size(),62);assert(draw(c,63)==2);
    c.guidanceMoved(v);most::Msg none;assert(!c.next(64,none)); // repeated guidance notification does not flood probes
    Config manual=config();manual.handles.allow(21);manual.handles.allow(22);Core m(manual);ready(m);ask(m);feed(m,size(),1);assert(draw(m)==0);
    Core large(config());ready(large);ask(large);feed(large,size(),1);assert(draw(large,20,false,32)==2);
    assert(large.stats().batches==2&&large.stats().batchesDone==2); // original 32-call batch + bounded 2-call batch
    puts("  resets: cached geometry retained, proof reset, new probe, narrow permissions, manual hides, bounded batches");
}
static void testNewIdentityNeedsFrame() {
    Core c(config());ready(c);ask(c);feed(c,size(),1);assert(draw(c)==2);
    c.identityChanged();ask(c,30);feed(c,size(),31);
    assert(draw(c,32,true)==0); // fresh size alone cannot reuse a frame-on from the previous identity
    assert(draw(c,33)==2);
    puts("  identity: fresh size still needs a current frame-on before hiding native bars");
}
static void testSendWaitClock() {
    Core c(config());ready(c);ask(c);feed(c,size(),1);
    Batch b("frame");b.add(kSetVisibility,setVisibility(1,true));assert(c.submit(b,2)==kQueued);
    most::Msg m;bool native;assert(c.next(2,m,&native)&&!native);feed(c,incoming(m.fkt,most::kOpResultAck,m.data),2);
    assert(c.next(3,m,&native)&&native);m.src=self;
    const uint32_t delayed=config().replyMs+1000;
    assert(c.nativeSendAllowed(m,c.nativeGeneration(),delayed));c.tick(delayed+1);assert(c.status()==kReady);
    feed(c,incoming(m.fkt,most::kOpResultAck,m.data),delayed+2);
    assert(c.next(delayed+3,m,&native)&&native);m.src=self;assert(c.nativeSendAllowed(m,c.nativeGeneration(),delayed+3));
    feed(c,incoming(m.fkt,most::kOpResultAck,m.data),delayed+4);assert(!c.busy());
    puts("  timer: native reply deadline starts at guarded dispatch, not before send-lock wait");
}
static void testLateNativeCopies() {
    Config cfg=config();Core c(cfg);ready(c);ask(c);feed(c,size(),1);
    Batch b("frame");b.add(kSetVisibility,setVisibility(1,true));assert(c.submit(b,2)==kQueued);
    most::Msg m;bool native;assert(c.next(2,m,&native)&&!native);feed(c,incoming(m.fkt,most::kOpResultAck,m.data),2);
    assert(c.next(3,m,&native)&&native);m.src=self;assert(c.nativeSendAllowed(m,c.nativeGeneration(),3));
    const uint32_t late=cfg.copyMs+1000;
    const uint32_t copies=c.stats().copies;
    feed(c,incoming(m.fkt,most::kOpResultAck,m.data,nav),late); // copy before our late answer: still awaited
    c.tick(late);assert(c.status()==kReady&&c.stats().copies==copies+1);
    feed(c,incoming(m.fkt,most::kOpResultAck,m.data),late+1);assert(c.stats().slow==1);
    feed(c,incoming(m.fkt,most::kOpResultAck,m.data,nav),late+2); // copy after our answer: new copy window
    assert(c.status()==kReady&&c.stats().copies==copies+2);
    assert(c.next(late+3,m,&native)&&native);m.src=self;assert(c.nativeSendAllowed(m,c.nativeGeneration(),late+3));
    feed(c,incoming(m.fkt,most::kOpResultAck,m.data),late+4);assert(!c.busy());
    puts("  late native answers: copies before/after our answer stay ours, and the hide pair completes");
}
struct PortCore:Port {
    Core core;unsigned n;
    PortCore():core(config()),n(0){ready(core);}
    SubmitResult submit(const Batch& b){++n;return core.submit(b,10);}
    Status status()const{return core.status();}uint32_t epoch()const{return core.epoch();}
    bool waiting(int s)const{return core.waiting(s);}void reset(){core.reset(0);}bool waitIdle(uint32_t){return true;}
    bool measuredSize(int&w,int&h)const{return core.measuredSize(w,h);}
};
static int drain(Core& c,uint32_t now,bool allowSize=false) {
    int hides=0;most::Msg m;bool native;
    while(c.next(now,m,&native)) {
        if(m.fkt==kGetSize){if(allowSize)feed(c,size(),now);continue;}
        if(native){m.src=self;assert(c.nativeSendAllowed(m,c.nativeGeneration(),now));++hides;}
        feed(c,incoming(m.fkt,most::kOpResultAck,m.data),now);
    }return hides;
}
static void testReplyBarrierWithNativeBars() {
    Core c(config());ready(c);ask(c);feed(c,size(),1);
    Batch b("frame before text");b.add(kSetVisibility,setVisibility(1,true));
    b.add(kTextareaSetText,setText(2,"Road"),true);
    assert(c.submit(b,2)==kQueued);most::Msg m;bool native;
    assert(c.next(2,m,&native)&&!native&&m.fkt==kSetVisibility);
    most::Msg blocked;assert(!c.next(2,blocked));
    feed(c,incoming(m.fkt,most::kOpResultAck,m.data),3);
    assert(c.next(3,m,&native)&&!native&&m.fkt==kTextareaSetText);
    assert(!c.next(3,blocked));feed(c,incoming(m.fkt,most::kOpResultAck,m.data),4);
    assert(drain(c,5)==2);
    puts("  reply barrier: ordinary calls wait for replies; automatic hides follow the completed batch");
}
static void testF15DoesNotEnableNativeBars() {
    Config cfg;cfg.handles=handlesOf(k6waF15);cfg.navFrame=k6waF15.frame;cfg.measureHandle=k6waF15.bufB;
    cfg.nativeBars=compactNativeBarProfile(k6waF15);assert(!cfg.nativeBars);
    Core c(cfg);ready(c);ask(c);feed(c,size(),1);
    assert(draw(c)==0); // even a compact measured buffer does not grant the F30 hide capability to F15
    puts("  F15: compact measurement never enables the F30 native-bar hides");
}
static void testClearDoesNotHide() {
    Core c(config());ready(c);ask(c);feed(c,size(),1);assert(draw(c)==2);
    Batch b("clear");b.add(kSetVisibility,setVisibility(1,false));
    b.add(kTextareaSetText,setText(3,""));b.add(kTextareaSetText,setText(4,""));
    assert(c.submit(b,30)==kQueued);assert(drain(c,30)==0);
    assert(draw(c,40,true)==0); // a distance alone must not hide native bars after our panel ended
    assert(draw(c,50)==2);
    puts("  clear: panel-off never causes automatic hides, nor a later distance without a new panel-on");
}
static void testPinnedRedraw() {
    for(int pinned=0;pinned<2;++pinned)for(int media=0;media<2;++media) {
        if(pinned){setenv("NBT_HUD_CANVAS","0 0 125 76",1);setenv("NBT_HUD_GLYPH","37 23 82 68",1);}
        HudProfile p=k6wa;applyHudEnv(p);PortCore port;
        kombinav::Hud6waDisplay d(port,p,false);kombinav::Settings s;s.junction=false;d.setSettings(s);
        kombinav::NavView v;v.aa_type=8;v.road="Road";v.dist_text="100";v.aa_unit=1;
        if(media)assert(d.showIdleText("Song",0)==kombinav::ClusterDisplay::kOk);
        else assert(d.showManeuver(v,0,true,true)==kombinav::ClusterDisplay::kOk);
        assert(drain(port.core,10)==0);port.core.tick(1600);
        feed(port.core,size(126,77,-1,most::kOpStatus),1700); // legacy fit, not trusted
        if(media)d.showIdleText("Song",1);else {d.sameManeuver(v,1);d.showManeuver(v,1,false,false);}
        assert(drain(port.core,1710)==0);
        unsigned before=port.n;feed(port.core,size(),1800);
        if(media)assert(d.showIdleText("Song",2)==kombinav::ClusterDisplay::kOk);
        else {assert(d.sameManeuver(v,2));assert(d.showManeuver(v,2,false,false)==kombinav::ClusterDisplay::kOk);}
        assert(port.n>before&&drain(port.core,1810)==2);
        unsetenv("NBT_HUD_CANVAS");unsetenv("NBT_HUD_GLYPH");
    }
    for(int role=0;role<6;++role) {
        HudProfile p=k6wa;uint16_t* roles[]={&p.frame,&p.street,&p.distance,&p.unit,&p.bufA,&p.bufB};++*roles[role];
        assert(!compactNativeBarProfile(p));
    }
    puts("  display: late equal-size proof redraws unchanged maneuver/media with and without explicit pins");
}
// Test-only transport barriers: callbacks may run while the sender waits on MOST's real sendLock.
struct State {
    pthread_mutex_t lock;pthread_cond_t cond;bool hold,held,release,frame,between,fail,pauseSize;int frameCount,queries;
    State():hold(false),held(false),release(false),frame(false),between(false),fail(false),pauseSize(false),frameCount(0),queries(0){pthread_mutex_init(&lock,0);pthread_cond_init(&cond,0);}
    ~State(){pthread_cond_destroy(&cond);pthread_mutex_destroy(&lock);}
};
static bool hook(uint32_t id,const std::vector<uint8_t>& p,void* user) {
    if(id!=most::kReqProcessMessage)return true;
    most::View v;assert(most::parseView(&p[4],p.size()-4,v));State&s=*(State*)user;
    if(v.fkt==kSetFillColor){pthread_mutex_lock(&s.lock);if(s.hold){s.held=true;pthread_cond_broadcast(&s.cond);while(!s.release)pthread_cond_wait(&s.cond,&s.lock);}pthread_mutex_unlock(&s.lock);return true;}
    if(v.fkt==kGetSize){
        pthread_mutex_lock(&s.lock);++s.queries;bool pause=s.pauseSize;pthread_cond_broadcast(&s.cond);pthread_mutex_unlock(&s.lock);
        if(!pause){most::Msg reply=size();reply.dst=v.src;deliver(reply);}return true;
    }
    std::vector<uint8_t> data(v.data,v.data+v.len);
    if(v.fkt==kSetVisibility&&data==setVisibility(1,true)) {
        pthread_mutex_lock(&s.lock);s.frame=true;++s.frameCount;pthread_cond_broadcast(&s.cond);pthread_mutex_unlock(&s.lock);return true;
    }
    if(v.fkt==kSetVisibility&&data==setVisibility(21,false)&&s.fail)return false;
    most::Msg ack=incoming(v.fkt,most::kOpResultAck,data);ack.dst=v.src;deliver(ack);
    if(v.fkt==kSetVisibility&&data==setVisibility(21,false)&&s.between)
        deliver(incoming(kTextareaSetText,most::kOpResultAck,setText(2,"Other"),nav));
    return true;
}
static void waitFlag(State&s,bool State::*flag) {
    for(int i=0;i<400;++i){pthread_mutex_lock(&s.lock);bool x=s.*flag;pthread_mutex_unlock(&s.lock);if(x)return;usleep(5000);}assert(false);
}
static void setup(most::Client& c,HudTerminal&t,State&s) {
    fake_dsi::reset();fake_dsi::setRequestHook(hook,&s);assert(t.attach(c)&&c.start("T"));fake_dsi::attach();
    std::vector<uint8_t> p;most::putLe32(p,77);most::putLe16(p,self);fake_dsi::data(most::kInfoClientResponse,&p[0],p.size());
    p.clear();most::putLe32(p,1);most::putLe16(p,host);most::putLe16(p,most::registryKey(0x82,0));fake_dsi::data(most::kAttrRegistry,&p[0],p.size());assert(t.start());
    for(int i=0;i<400&&t.stats().answers==0;++i)usleep(5000);
    assert(t.stats().answers>0);
}
static int sentHide(int h) {
    int n=0;std::vector<fake_dsi::Call> calls=fake_dsi::snapshot();
    for(size_t i=0;i<calls.size();++i)if(calls[i].id==most::kReqProcessMessage){most::View v;assert(most::parseView(&calls[i].payload[4],calls[i].payload.size()-4,v));if(v.fkt==kSetVisibility&&v.len==3&&v.data[1]==h&&!v.data[2])++n;}
    return n;
}
static void* holdSend(void* user){most::Client*c=(most::Client*)user;most::Msg m;assert(buildCall(host,Call(kSetFillColor,setFillColor(5,4)),1,config().handles,m));assert(c->send(m));return 0;}
static void testLockedSend() {
    for(int scenario=0;scenario<3;++scenario) {
        most::Client client;State s;HudTerminal t(config());s.between=scenario==0;s.fail=scenario==2;setup(client,t,s);
        Batch b("frame");b.add(kSetVisibility,setVisibility(1,true));assert(t.submit(b)==kQueued);waitFlag(s,&State::frame);
        if(scenario==1) {
            s.hold=true;pthread_t holder;assert(!pthread_create(&holder,0,holdSend,&client));waitFlag(s,&State::held);
            uint32_t before=t.stats().calls;deliverTerminal(t,incoming(kSetVisibility,most::kOpResultAck,setVisibility(1,true)));
            for(int i=0;i<400&&t.stats().calls==before;++i)usleep(5000);
            assert(t.stats().calls>before);
            deliverTerminal(t,incoming(kTextareaSetText,most::kOpResultAck,setText(2,"Other"),nav));
            pthread_mutex_lock(&s.lock);s.release=true;pthread_cond_broadcast(&s.cond);pthread_mutex_unlock(&s.lock);pthread_join(holder,0);
        } else deliverTerminal(t,incoming(kSetVisibility,most::kOpResultAck,setVisibility(1,true)));
        for(int i=0;i<400&&t.status()==kReady;++i)usleep(5000);
        assert(t.status()==(scenario==2?kFailed:kNavActive));t.stop();
        assert(sentHide(22)==0&&sentHide(21)==(scenario==1?0:1));
        if(scenario==2){uint32_t copies=t.stats().copies;deliver(incoming(kSetVisibility,most::kOpResultAck,setVisibility(21,false),nav));assert(t.stats().copies==copies);}
        fake_dsi::setRequestHook(0,0);
    }
    puts("  sender: takeover between hides and during sendLock wait; failed-send echo removed");
}
static void testRegistrationProof() {
    for(int scenario=0;scenario<3;++scenario) {
        most::Client client;State s;HudTerminal t(config());setup(client,t,s);
        uint32_t identity=client.identityEpoch();
        pthread_mutex_lock(&s.lock);s.pauseSize=true;int before=s.queries;pthread_mutex_unlock(&s.lock);
        uint16_t address=scenario==1?0x2088:self;
        if(scenario<2) {
            std::vector<uint8_t> p;most::putLe32(p,88);most::putLe16(p,address);
            fake_dsi::data(most::kInfoClientResponse,&p[0],p.size());assert(client.identityEpoch()!=identity);
        } else {
            std::vector<uint8_t> p;most::putLe32(p,1);most::putLe16(p,host);most::putLe16(p,most::registryKey(0x83,0));
            fake_dsi::data(most::kAttrRegistry,&p[0],p.size());p.back()=0x82;
            fake_dsi::data(most::kAttrRegistry,&p[0],p.size());
        }
        // Hold the fresh answer until a complete drawing has proved that cached size is insufficient.
        most::Msg stale=size();stale.dst=address;
        bool asked=false;
        for(int i=0;i<400&&!asked;++i){pthread_mutex_lock(&s.lock);asked=s.queries>before;pthread_mutex_unlock(&s.lock);if(!asked)usleep(5000);}
        assert(asked);
        Batch b("frame");b.add(kSetVisibility,setVisibility(1,true));assert(t.submit(b)==kQueued);waitFlag(s,&State::frame);
        most::Msg ack=incoming(kSetVisibility,most::kOpResultAck,setVisibility(1,true));ack.dst=address;deliverTerminal(t,ack);
        assert(t.waitIdle(1000)&&sentHide(21)==0&&sentHide(22)==0);
        deliverTerminal(t,stale); // now solicited, current registered address
        pthread_mutex_lock(&s.lock);s.frame=false;pthread_mutex_unlock(&s.lock);
        assert(t.submit(b)==kQueued);waitFlag(s,&State::frame);deliverTerminal(t,ack);
        assert(t.waitIdle(1000)&&sentHide(21)==1&&sentHide(22)==1);
        t.stop();fake_dsi::setRequestHook(0,0);
    }
    puts("  identity: same/new-address re-registration and registry removal/reappearance re-probe before hides");
}
int main(){testProof();testResetAndWhitelist();testNewIdentityNeedsFrame();testSendWaitClock();testLateNativeCopies();testReplyBarrierWithNativeBars();testF15DoesNotEnableNativeBars();testPinnedRedraw();testClearDoesNotHide();testLockedSend();testRegistrationProof();puts("native HUD bars: PASS");}
