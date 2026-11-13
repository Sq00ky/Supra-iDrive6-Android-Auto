// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host-only. CSV timing reused; all method payloads in these tests are synthetic.
#include "cluster/hud_terminal.hpp"
#include "cluster/hud6wb_display.hpp"
#include "cluster/hud6wb_resources.hpp"
#include "cluster/hud6wb_codec.hpp"
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
using namespace nbt;
using namespace nbt::hudterm;
using namespace nbt::kombinav;
namespace {
Config config() {
    Bmw6wbProfile p;Config c;c.handles=handlesOf(p);c.extendedCalls=true;
    c.strictAnswers=true;c.acceptMirroredAnswers=true;c.ignoreFactoryNav=true;
    c.useFixedTag=true;c.fixedTag=99;c.txType=4;c.maxInFlight=1;c.maxBatch=128;
    c.replyMs=3000;c.sizeReplyMs=5000;c.pacedGraphics=true;return c;
}
void up(Core& c) {c.setLink(true,0x2063,0);c.setHost(0x101,0);assert(c.status()==kReady);}
most::View answer(uint16_t f,const std::vector<uint8_t>& p,uint8_t op=most::kOpResultAck) {
    most::View m={};m.src=0x101;m.dst=0x2063;m.fblock=0x82;m.inst=0;m.fkt=f;m.op=op;
    m.hdrTag=99;m.data=p.empty()?NULL:&p[0];m.len=p.size();return m;
}
Batch visibility(uint16_t h=0x1f) {Batch b("visibility",true);b.add(kSetVisibility,setVisibility(h,false));return b;}
most::Msg send(Core& c,uint32_t t) {most::Msg m;assert(c.next(t,m));c.sent(t);return m;}
void answerMsg(Core& c,const most::Msg& m,uint32_t t,uint8_t op=most::kOpResultAck) {
    std::vector<uint8_t> p=m.data;
    if(op==most::kOpErrorAck) {p.resize(2);p.push_back(0x41);} // opaque error, not interpreted
    c.onMessage(answer(m.fkt,p,op),t);
}
void cadenceAndOrder() {
    Config cfg=config();Core c(cfg);up(c);Batch b("ordered",true);
    b.add(kSetVisibility,setVisibility(0x20,false));
    b.add(kRemoveAllItems,removeAllItems(0x1f));b.add(kSetFillColor,setFillColor(0x1f,2));
    b.add(kSetLineWidth,setLineWidth(0x1f,6));
    std::vector<Point> pts;Point a={40,90},z={100,140};pts.push_back(a);pts.push_back(z);
    b.add(kAddPolygon,addPolygon(0x1f,0x80,kPolyline,pts));
    b.add(0x227,distanceBar(0x35,20));
    b.add(kTextareaSetText,latinText(0x1a,"Synthetic",15));
    b.add(kSwitchVisibility,switchVisibility(0x20,0x1f));
    b.add(kRemoveAllItems,removeAllItems(0x20));b.add(kSetVisibility,setVisibility(0x17,true));
    assert(c.submit(b,100)==kQueued);
    for(size_t i=0;i<b.calls.size();++i) {
        uint32_t t=100+(uint32_t)i*15;most::Msg m=send(c,t);
        assert(m.fkt==b.calls[i].fkt&&m.data==b.calls[i].payload&&m.txType==4&&m.opType==6);
        most::Msg blocked;assert(!c.next(t+14,blocked));
        if(i+1<b.calls.size())assert(c.msUntilDeadline(t)==15);
    }
    assert(!c.busy()&&c.stats().batchesDone==1&&c.stats().answers==0);
    c.tick(3100);assert(c.status()==kReady&&c.stats().timeouts==0);
    c.tick(8300);assert(c.status()==kReady&&c.stats().graphicsUnconfirmed==b.calls.size());
    cfg.graphicIntervalMs=25;Core custom(cfg);up(custom);assert(custom.submit(b,0)==kQueued);
    send(custom,0);most::Msg m;assert(!custom.next(24,m)&&custom.msUntilDeadline(24)==1);send(custom,25);
}
void lateMissingDuplicateOutOfOrder() {
    Core c(config());up(c);Batch b("two");b.add(kSetVisibility,setVisibility(0x1f,false));
    b.add(kSetLineWidth,setLineWidth(0x1f,6));assert(c.submit(b,100)==kQueued);
    most::Msg first=send(c,100),second=send(c,115);
    answerMsg(c,second,200);assert(c.stats().answers==1); // out of order
    answerMsg(c,first,3465); // 3365 ms: observed CSV latency, synthetic echo
    assert(c.status()==kReady&&c.stats().answers==2&&c.stats().graphicsLate==1);
    answerMsg(c,first,3466);assert(c.stats().answers==2&&c.stats().copies==1);
    c.tick(8200);assert(c.stats().graphicsUnconfirmed==0&&c.stats().timeouts==0);
    Core missing(config());up(missing);missing.submit(visibility(),0);send(missing,0);
    missing.tick(8000);assert(missing.status()==kReady&&missing.stats().graphicsUnconfirmed==1);
    answerMsg(missing,first,9000,most::kOpErrorAck);
    assert(missing.status()==kReady&&missing.stats().errors==1&&missing.stats().graphicsAmbiguous==1);
}
void errorsAndGenerations() {
    Core c(config());up(c);Batch b("fatal");b.add(kSetVisibility,setVisibility(0x1f,false));
    b.add(kSetLineWidth,setLineWidth(0x1f,6));c.submit(b,0);most::Msg m=send(c,0);
    answerMsg(c,m,1,most::kOpErrorAck);assert(c.status()==kFailed&&c.stats().errors==1);
    most::Msg out;assert(!c.next(15,out)&&c.stats().batchesDropped==1);
    // A uniquely identifiable error from a previous batch is reported but cannot fail new work.
    Core old(config());up(old);old.submit(visibility(),0);most::Msg prev=send(old,0);
    Batch newer("new");newer.add(kSetLineWidth,setLineWidth(0x1f,6));newer.add(kSetFillColor,setFillColor(0x1f,2));
    old.submit(newer,1);send(old,15);answerMsg(old,prev,3365,most::kOpErrorAck);
    assert(old.status()==kReady&&old.stats().lateErrors==1&&old.stats().errors==1);send(old,3370);
    // Same key has no transaction nonce. Neither success nor error may choose a generation.
    Core reuse(config());up(reuse);reuse.submit(visibility(),0);prev=send(reuse,0);
    reuse.submit(visibility(),15);send(reuse,15);answerMsg(reuse,prev,3365);
    assert(reuse.stats().answers==0&&reuse.stats().graphicsAmbiguous==1);
    answerMsg(reuse,prev,3366,most::kOpErrorAck);
    assert(reuse.status()==kReady&&reuse.stats().errors==1&&reuse.stats().graphicsAmbiguous==2);
    reuse.tick(8015);reuse.submit(visibility(),9000);send(reuse,9000);
    answerMsg(reuse,prev,9001); // arbitrarily old duplicate after ledger expiry
    assert(reuse.stats().answers==0&&reuse.status()==kReady&&reuse.stats().graphicsAmbiguous==3);
}
void validationAndDisconnect() {
    Core c(config());up(c);c.submit(visibility(),0);most::Msg m=send(c,0);
    std::vector<uint8_t> p=m.data;most::View a=answer(m.fkt,p);
    a.hdrTag=98;c.onMessage(a,1);a.hdrTag=99;a.src=0x102;c.onMessage(a,2);
    a.src=0x101;a.inst=1;c.onMessage(a,3);a.inst=0;a.fblock=0x83;c.onMessage(a,4);
    a.fblock=0x82;a.dst=0x5000;c.onMessage(a,5);a.dst=0x2063;a.op=most::kOpStatus;c.onMessage(a,6);
    a.op=most::kOpResultAck;p[2]=1;c.onMessage(a,7);p[2]=0;
    assert(c.stats().answers==0);answerMsg(c,m,8);assert(c.stats().answers==1);
    c.submit(visibility(0x20),15);m=send(c,15);c.setLink(false,0,20);
    answerMsg(c,m,30,most::kOpErrorAck);assert(c.status()==kOffline&&c.stats().graphicsAbandoned==1);
    c.setReplyTag(100);c.setLink(true,0x2064,40);answerMsg(c,m,50);
    assert(c.status()==kReady&&c.stats().answers==1); // old reply is not new-registration work
}
void updatesAndBarriers() {
    Core c(config());up(c);Batch first("current",true);first.add(kSetVisibility,setVisibility(0x1f,false));
    first.add(kSwitchVisibility,switchVisibility(0x20,0x1f));first.add(kRemoveAllItems,removeAllItems(0x20));
    c.submit(first,0);send(c,0);
    Batch pending=visibility(0x18),latest=visibility(0x19);
    assert(c.submit(pending,1)==kQueued&&c.submit(latest,2)==kReplaced);
    assert(send(c,15).fkt==kSwitchVisibility);assert(send(c,30).fkt==kRemoveAllItems);
    assert(send(c,45).data==latest.calls[0].payload);assert(c.stats().replaced==1&&!c.busy());
    // Blocking information: no later graphics until the size is validated, final deadline unchanged.
    Core gate(config());up(gate);Batch b("barrier");std::vector<uint8_t> q=getSize(0x17);q.push_back(1);
    b.add(kGetSize,q);b.add(kSetVisibility,setVisibility(0x17,true));gate.submit(b,0);send(gate,0);
    most::Msg out;assert(!gate.next(15,out));gate.tick(3000);assert(gate.status()==kReady);
    gate.tick(5000);assert(gate.status()==kFailed&&gate.stats().timeouts==1&&gate.stats().calls==1);
    Core fail(config());up(fail);fail.submit(first,0);assert(fail.next(0,out));fail.sendFailed(0);
    assert(fail.status()==kFailed&&fail.stats().batchesDone==0&&!fail.next(15,out));
    // C05 is not on the asynchronous whitelist: cross-channel bitmap upload remains a barrier.
    Bmw6wbProfile profile;std::vector<uint32_t> pixels(305*44,0);
    std::vector<uint8_t> bmp=bitmap(0x18,1,pixels,305,44,4096);
    Batch upload("bitmap");Call call(0xc05,bmp);call.txType=0;upload.calls.push_back(call);
    upload.add(kSetVisibility,setVisibility(0x18,true));Core bitmapCore(config());up(bitmapCore);
    bitmapCore.submit(upload,0);send(bitmapCore,0);assert(!bitmapCore.next(15,out));
    bitmapCore.tick(3000);assert(bitmapCore.status()==kFailed);
    Core uploaded(config());up(uploaded);uploaded.submit(upload,0);send(uploaded,0);
    std::vector<uint8_t> result=getSize(0x18);result.push_back(1);result.push_back(1);
    uploaded.onMessage(answer(0xc05,result),250);
    assert(send(uploaded,251).fkt==kSetVisibility&&uploaded.stats().answers==1);
}
// Exercise the actual driver + resource cache with the CSV's eight receive times.
struct CorePort : Port {
    Core c;CorePort():c(config()){up(c);}
    SubmitResult submit(const Batch& b){return c.submit(b,0);}Status status()const{return c.status();}
    uint32_t epoch()const{return c.epoch();}bool waiting()const{return c.waiting();}
    void reset(){c.reset(0);}bool waitIdle(uint32_t){return !c.busy()&&c.status()==kReady;}
    bool imageSize(uint16_t h,uint16_t& w,uint16_t& hh)const{return c.imageSize(h,w,hh);}
};
void sizesThenRender() {
    CorePort port;Bmw6wbProfile p;Hud6wbDisplay display(port,p,false);NavView v;v.aa_type=8;v.road="Synthetic Road";
    assert(display.showManeuver(v,0,true,true)==ClusterDisplay::kOk);
    const uint32_t tx[]={2847549,2850919,2854295,2857643,2861003,2864374,2867773,2871128};
    const uint32_t rx[]={2850918,2854295,2857643,2861003,2864374,2867773,2871127,2874504};
    const uint16_t wh[][2]={{309,330},{221,158},{221,158},{305,44},{305,44},{283,40},{283,40},{28,113}};
    for(unsigned i=0;i<8;++i) {
        most::Msg m=send(port.c,tx[i]);assert(m.fkt==kGetSize);
        std::vector<uint8_t> a=getSize(rdBe16(&m.data[0]));putBe16(a,wh[i][0]);putBe16(a,wh[i][1]);
        port.c.onMessage(answer(kGetSize,a),rx[i]);
        if(i<7) {display.showManeuver(v,i+1,true,true);assert(port.c.stats().graphicsSent==0);}
    }
    assert(port.c.stats().answers==8&&!port.c.busy());display.showManeuver(v,9,true,true);
    uint32_t t=2874589;unsigned count=0;
    while(port.c.busy()) {
        most::Msg m=send(port.c,t);assert(m.fkt!=kGetSize);t+=15;++count;assert(count<128);
        if(count==2) {
            v.aa_type=7;display.showManeuver(v,10,true,true); // Maps update during active batch
            assert(display.sameManeuver(v,10)); // dirty, without replacing the partial buffer swap
        }
    }
    assert(count>10&&port.c.stats().graphicsSent==count);
    port.c.tick(t+3000);assert(port.c.status()==kReady); // no fatal graphic deadline
    v.aa_type=7;display.showManeuver(v,10,true,true);
    assert(send(port.c,t+3015).fkt!=kGetSize); // cached dimensions, next Maps update
}
// Real sender thread: condition-variable scheduling, no acknowledgements.
struct Sender {
    pthread_mutex_t lock;std::vector<uint32_t> times;std::vector<uint16_t> functions;bool refuse;
    Sender():refuse(false){pthread_mutex_init(&lock,NULL);}
    ~Sender(){pthread_mutex_destroy(&lock);}
    static uint32_t ms(){timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint32_t)t.tv_sec*1000+t.tv_nsec/1000000;}
    static bool send(const most::Msg& m,void* user) {
        Sender& s=*static_cast<Sender*>(user);pthread_mutex_lock(&s.lock);
        bool ok=!s.refuse;if(ok){s.times.push_back(ms());s.functions.push_back(m.fkt);}
        pthread_mutex_unlock(&s.lock);return ok;
    }
};
void threadedCadence() {
    Sender sender;HudTerminal terminal(config());terminal.setSender(&Sender::send,&sender);
    terminal.onLink(true,0x2063);std::vector<most::RegistryEntry> reg;most::RegistryEntry e={0x101,0x8200};reg.push_back(e);
    terminal.onRegistry(reg);assert(terminal.start());Batch b("threaded",true);
    b.add(kSetVisibility,setVisibility(0x20,false));b.add(kRemoveAllItems,removeAllItems(0x1f));
    b.add(kSwitchVisibility,switchVisibility(0x20,0x1f));assert(terminal.submit(b)==kQueued);
    assert(terminal.waitIdle(2000));terminal.stop();
    assert(sender.times.size()==3&&sender.functions[0]==kSetVisibility&&sender.functions[1]==kRemoveAllItems&&sender.functions[2]==kSwitchVisibility);
    for(size_t i=1;i<3;++i)assert(sender.times[i]-sender.times[i-1]>=15);
    assert(terminal.stats().answers==0&&terminal.stats().batchesDone==1&&terminal.status()==kReady);
    Sender refused;refused.refuse=true;HudTerminal failed(config());failed.setSender(&Sender::send,&refused);
    failed.onLink(true,0x2063);failed.onRegistry(reg);assert(failed.start());assert(failed.submit(b)==kQueued);
    assert(!failed.waitIdle(2000));failed.stop();assert(failed.status()==kFailed&&failed.stats().batchesDone==0);
}
void boundedHistoryAndClockWrap() {
    Config cfg=config();cfg.graphicIntervalMs=1;Core c(cfg);up(c);
    for(unsigned i=0;i<1024;++i) {assert(c.submit(visibility(),i)==kQueued);send(c,i);}
    c.submit(visibility(),1024);most::Msg m;assert(!c.next(1024,m)&&c.status()==kFailed);
    Core wrap(config());up(wrap);Batch b("wrap");b.add(kSetVisibility,setVisibility(0x1f,false));
    b.add(kRemoveAllItems,removeAllItems(0x1f));wrap.submit(b,0xfffffff8u);send(wrap,0xfffffff8u);
    assert(!wrap.next(6,m));send(wrap,7);wrap.tick(8007);assert(wrap.status()==kReady&&wrap.stats().graphicsUnconfirmed==2);
}
}
int main() {
    cadenceAndOrder();lateMissingDuplicateOutOfOrder();errorsAndGenerations();validationAndDisconnect();
    updatesAndBarriers();sizesThenRender();threadedCadence();boundedHistoryAndClockWrap();puts("test_hud6wb_async OK (synthetic payloads; CSV timing)");
}
