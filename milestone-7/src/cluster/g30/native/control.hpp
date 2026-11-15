// Experimental physical-car session. Transport/graph framing is shared with the SDK.
#pragma once
#include "pip.hpp"
#include "qnx_abi.hpp"
#include "presentation.hpp"
#include "factory_profile.hpp"
#include "kombi_graph_template.hpp"
using namespace kombi::pip::detail;
static volatile int interrupted;
static uint32_t end_time;
static bool service_mode;
static void on_signal(int) { interrupted=1; }
// The owning AA session bounds service lifetime. Individual network operations
// still have deadlines; only standalone trials have a whole-process deadline.
static bool expired() { return interrupted || (!service_mode && int32_t(millis()-end_time)>=0); }
enum { PollRead=1, PollWrite=2, PollErrorOrHangup=0x60 };
struct Address { uint8_t length,family; uint16_t port; uint32_t ip; uint8_t zero[8]; };
static const uint32_t ProducerIP=0x78c730a0; // temporary vlan73 alias 160.48.199.120
static unsigned discovery_session=1;
static bool discovery_reboot=true;
struct Control {
    int control, udp, guidance;
    unsigned port, rpc_session;
    uint32_t handle, renewed;
    unsigned pending_method, pending_session, reply_used, reply_size;
    bool pending_reply, broken_stream;
    uint32_t cleanup_deadline;
    uint8_t reply_bytes[4096];
    bool stopped() const { return expired(); }
    void refresh() {
        uint32_t now=millis();
        if(udp>=0 && uint32_t(now-renewed)>=10000) { subscribe(udp,port,30); renewed=now; }
    }
    // Reuse the tested SDK framing, graph and cancellation RPC machinery.
    // The car-specific sink handshake is implemented below.
#define sink_ready reference_sink_ready
#include "native_methods.inc"
#undef sink_ready
    bool sink(bool initial) {
        uint8_t p[64]={}; int n=message(control,p,sizeof(p),false,initial);
        if(!initial && n==16 && get32(p)==0xffff8000) return true;
        if(n!=29 || get32(p)!=0xb0098001 || p[12]!=1 || p[13]!=1 || p[14]!=2 || p[15]) return false;
        printf("SINK width=%u height=%u selection=%u\n",get16(p+24),get16(p+26),p[28]); fflush(0);
        if(get16(p+24)!=620 || get16(p+26)!=313 || p[28]!=1) {
            printf("REFUSE_SINK requires_cluster_620x313_selection_1\n"); return false;
        }
        uint8_t ack[18]={}; put32(ack,0xb0090001); put32(ack+4,10);
        put16(ack+8,1); ack[12]=ack[13]=ack[14]=1; ack[16]=1;
        // Same no-response method and body as all captured cluster handshakes.
        bool ok=transfer(control,ack,sizeof(ack),true);
        printf("SINK_COMPLETION sent=%u\n",unsigned(ok)); return ok;
    }
    bool start() {
        control=open_tcp(0x60c730a0,30503);
        if(control<0) { printf("DISPLAY_SINK_CONNECT_FAILED errno=%d\n",*__get_errno_ptr()); return false; }
        Address local={}; unsigned size=sizeof(local);
        if(getsockname(control,&local,&size)<0) return false;
        port=(local.port>>8)|((local.port&255)<<8);
        udp=socket(2,2,0); if(udp<0) return false;
        Address sender=address(ProducerIP,0);
        if(bind(udp,&sender,sizeof(sender))<0) return false;
        subscribe(udp,port,30); renewed=millis(); if(!sink(true)) return false;
        guidance=open_tcp(0x60c730a0,30502);
        if(guidance<0) printf("GUIDANCE_CONNECT_FAILED errno=%d\n",*__get_errno_ptr());
        return guidance>=0 && guidance_start(guidance,handle,rpc_session,car_demo::FactoryResource);
    }
    bool pump() {
        if(stopped()) return false;
        refresh();
        if(!guidance_heartbeat(guidance)) return false;
        PollFD p={control,PollRead,0}; int n=poll(&p,1,0);
        return n>=0 && (n==0 || sink(false));
    }
    bool close() {
        bool ok=true;
        // A stopped/expired read may have consumed only part of a reply. Finish
        // that exact transaction before sending cancellation on the same TCP
        // stream. Never mistake its late acknowledgement for the Stop reply.
        cleanup_deadline=millis()+6000;
        if(guidance>=0 && pending_reply)
            ok=finish_guidance(guidance,millis()+1000,true,pending_method==1?&handle:0);
        ok=ok&&!broken_stream;
        if(ok && guidance>=0 && handle) {
            uint8_t body[512];
            for(unsigned i=0;i<nbt::kombinav::tmpl::cancel_count;++i) {
                const nbt::kombinav::tmpl::Frame& f=nbt::kombinav::tmpl::cancel[i];
                if(f.len>sizeof(body) || f.len<4) { ok=false; break; }
                for(unsigned j=0;j<f.len;++j) body[j]=f.body[j];
                put32(body,handle);
                if(!guidance_call(guidance,f.method,body,f.len,rpc_session++,0,true)) { ok=false; break; }
            }
        }
        if(udp>=0 && port) subscribe(udp,port,0);
        if(udp>=0) ::close(udp); if(control>=0) ::close(control); if(guidance>=0) ::close(guidance);
        udp=control=guidance=-1;
        printf("GUIDANCE_CLEANUP acknowledged=%u handle=%u\n",unsigned(ok),handle); fflush(0);
        return ok;
    }
};
