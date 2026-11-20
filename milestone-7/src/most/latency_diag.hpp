// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef MOST_LATENCY_DIAG_HPP
#define MOST_LATENCY_DIAG_HPP
#include "most/msg.hpp"
#include "most/ids.hpp"
#include <pthread.h>
#include <time.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

namespace nbt { namespace latencydiag {
enum Phase { Dispatch=1, BeforeSendLock, AfterSendLock, VendorSendEnter,
    VendorSendReturn, DsiCallbackSpan, MostParsed, MostHandlerSpan,
    TerminalEntry, CoreAnswer, Timeout, MostRouteSpan, GraphicAck, GraphicUnconfirmed,
    GraphicAmbiguous, GraphicError, GraphicAbandoned, BatchDispatched, GraphicLate };
struct Meta {
    uint16_t src,dst,fkt,handle;
    uint8_t fb,inst,op;
    uint32_t tag,len,event,seq;
    Meta():src(0),dst(0),fkt(0),handle(0),fb(0),inst(0),op(0),tag(0),len(0),event(0),seq(0) {}
};
inline Meta metadata(const most::View& m) {
    Meta v;v.src=m.src;v.dst=m.dst;v.fkt=m.fkt;v.fb=m.fblock;v.inst=m.inst;
    v.op=m.op;v.tag=m.hdrTag;v.len=(uint32_t)m.len;
    if(m.data && m.len>=2)v.handle=(uint16_t)((m.data[0]<<8)|m.data[1]);
    return v;
}
inline Meta metadata(const most::Msg& m) {
    Meta v;v.src=m.src;v.dst=m.dst;v.fkt=m.fkt;v.fb=m.fblock;v.inst=m.inst;
    v.op=m.opType;v.tag=m.hdrTag;v.len=(uint32_t)m.data.size();
    if(m.data.size()>=2)v.handle=(uint16_t)((m.data[0]<<8)|m.data[1]);
    return v;
}
inline Meta dsiMetadata(uint32_t id,const uint8_t* data,size_t len,bool request=false) {
    Meta m;m.event=id;
    if((request && id==most::kReqProcessMessage) || (!request && id==most::kInfoMessage)) {
        if(request) {if(len<4)return m;data+=4;len-=4;}
        most::View v;if(most::parseView(data,len,v)) {m=metadata(v);m.event=id;}
    }
    return m;
}
struct Event { uint64_t ns,endNs,serial;uintptr_t thread,context;uint32_t request;Phase phase;Meta meta; };
enum { Capacity=2048, Chunk=128, MaxBytes=8*1024*1024 };
struct State {
    bool enabled;pthread_mutex_t lock;
    Event events[Capacity];uint64_t written,read,overwritten;
    bool stop,started;pthread_t exporter;const char* path;
};
// One inline-function static shared by all daemon translation units. No heap storage.
inline State& state() { static State s={false,PTHREAD_MUTEX_INITIALIZER};return s; }
inline bool enabled() { return state().enabled; } // immutable after startup, including shutdown
inline uint64_t clockNs() {struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return 0;return (uint64_t)t.tv_sec*1000000000ull+t.tv_nsec;}
inline uint64_t stamp() {return enabled()?clockNs():0;}
inline bool record(Phase phase,const Meta& meta,uint64_t begin=0,uint32_t request=0,const void* context=NULL) {
    State& s=state();if(!s.enabled)return false;
    const uint64_t end=clockNs();
    // Never wait on another producer or on the exporter. Contention loses trace data only.
    if(pthread_mutex_trylock(&s.lock)!=0)return false;
    if(s.written-s.read==Capacity){++s.read;++s.overwritten;}
    Event& e=s.events[s.written%Capacity];e.ns=begin?begin:end;e.endNs=end;
    e.serial=++s.written;e.thread=(uintptr_t)pthread_self();e.context=(uintptr_t)context;
    e.request=request;e.phase=phase;e.meta=meta;
    pthread_mutex_unlock(&s.lock);return true;
}
inline bool hud(const Meta& m) {return m.fb==0x82 && m.inst==0 && (m.fkt==0xc04 || m.fkt==0x204);}
inline void* exportLoop(void*) {
    State& s=state();FILE* f=fopen(s.path,"w");
    if(!f)return NULL;
    int flags=fcntl(fileno(f),F_GETFD);
    if(flags>=0)fcntl(fileno(f),F_SETFD,flags|FD_CLOEXEC);
    size_t bytes=0;bool capped=false,stopping=false;uint64_t stopAt=0;
    const char* header="# monotonic nanoseconds; span ends in end_ns; request is an INTERNAL candidate ID, not a MOST transaction ID\n# trylock contention drops are possible and NOT counted; overwritten is ring eviction only\nserial,ns,end_ns,phase,thread,context,request,src,dst,fb,inst,fkt,op,tag,handle,len,dsi_event,dsi_seq\n";
    bytes+=fwrite(header,1,strlen(header),f);
    for(;;) {
        Event copy[Chunk];size_t n=0;bool stop=false;uint64_t lost=0;
        // The exporter also uses trylock; it copies at most 128 fixed records per acquisition.
        if(pthread_mutex_trylock(&s.lock)==0) {
            if(s.stop && !stopping){stopping=true;stopAt=s.written;}
            while(n<Chunk && s.read<s.written && (!stopping || s.read<stopAt)) {
                copy[n++]=s.events[s.read%Capacity];++s.read;
            }
            stop=stopping && s.read>=stopAt;lost=s.overwritten;
            pthread_mutex_unlock(&s.lock);
        }
        for(size_t i=0;i<n && !capped;++i) {
            const Event& e=copy[i];char line[320];
            int len=snprintf(line,sizeof(line),"%llu,%llu,%llu,%u,%llu,%llu,%u,%04x,%04x,%02x,%02x,%03x,%x,%u,%04x,%u,%u,%u\n",
                (unsigned long long)e.serial,(unsigned long long)e.ns,(unsigned long long)e.endNs,(unsigned)e.phase,
                (unsigned long long)e.thread,(unsigned long long)e.context,e.request,e.meta.src,e.meta.dst,e.meta.fb,
                e.meta.inst,e.meta.fkt,e.meta.op,e.meta.tag,e.meta.handle,e.meta.len,e.meta.event,e.meta.seq);
            if(len<0 || (size_t)len>=sizeof(line) || bytes+(size_t)len+100>MaxBytes){capped=true;break;}
            if(fwrite(line,1,(size_t)len,f)!=(size_t)len){fclose(f);return NULL;}
            bytes+=(size_t)len;
        }
        if(capped || stop) {
            fprintf(f,"# overwritten=%llu cap_reached=%u\n",(unsigned long long)lost,capped?1:0);break;
        }
        fflush(f);
        if(!n)usleep(100000); // exporter thread only
    }
    fclose(f);return NULL;
}
// Call once before application threads start. Never toggle enabled during execution.
inline bool start(bool on,const char* path="/dev/shmem/nbt-aad-6wb-diag.csv") {
    State& s=state();s.enabled=on;if(!on)return true;s.path=path;
    s.started=pthread_create(&s.exporter,NULL,&exportLoop,NULL)==0;
    if(!s.started)s.enabled=false; // startup only, before application producers
    return s.started;
}
// Non-critical shutdown only; the producer path never joins or waits.
inline void stop() {
    State& s=state();if(!s.started)return;
    pthread_mutex_lock(&s.lock);s.stop=true;pthread_mutex_unlock(&s.lock);
    pthread_join(s.exporter,NULL);s.started=false;
}
} }
#endif
