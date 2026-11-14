// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "logging/log.hpp"
#include "most/ids.hpp"
#include <stdarg.h>
#include <string.h>
#include <time.h>
namespace test6wb {
uint32_t monotonicMs() {
    struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);
    return (uint32_t)t.tv_sec*1000u+(uint32_t)(t.tv_nsec/1000000);
}
Log::Log():file_(NULL),failed_(false),limited_(false),cleanup_(false),written_(0),read_(0),count_(0),dropped_(0),trial_(0),testId_(0),timingCount_(0) {memset(queue_,0,sizeof(queue_));pthread_mutex_init(&mutex_,NULL);}
void Log::setTrial(unsigned id,unsigned testId) {
    drain();pthread_mutex_lock(&mutex_);trial_=id;testId_=testId;pthread_mutex_unlock(&mutex_);
}
bool Log::open(const char* path) {
    if(!path || !*path)return false;
    failed_=false;limited_=false;cleanup_=false;written_=0;timingCount_=0;
    pthread_mutex_lock(&mutex_);read_=count_=dropped_=trial_=testId_=0;pthread_mutex_unlock(&mutex_);
    file_=fopen(path,"w");return file_!=NULL;
}
void Log::write(const char* phase,const char* format,...) {
    char body[1024],date[40];va_list args;va_start(args,format);
    vsnprintf(body,sizeof(body),format,args);va_end(args);
    time_t now=time(NULL);struct tm local;localtime_r(&now,&local);
    strftime(date,sizeof(date),"%Y-%m-%dT%H:%M:%S%z",&local);
    char line[1280];
    int n=snprintf(line,sizeof(line),"%s mono_ms=%u trial=R%03u test=%03u %s %s\n",
                   date,monotonicMs(),trial_,testId_,phase,body);
    size_t length=n<0?0:(size_t)n;if(length>=sizeof(line))length=sizeof(line)-1;
    const size_t cap=5000000,reserve=16384,footer=512;
    if(file_ && !failed_) {
        if(!limited_ && written_+length>cap-reserve) {
            limited_=true;
            char marker[256];snprintf(marker,sizeof(marker),"LOG_LIMIT mono_ms=%u test=%03u occurrence=R%03u: stopping trials; reserved cleanup/footer space\n",monotonicMs(),testId_,trial_);
            size_t len=strlen(marker);
            if(fwrite(marker,1,len,file_)!=len)failed_=true;else written_+=len;
            fprintf(stderr,"%s",marker);
        }
        // Late RX noise cannot exhaust the cleanup reserve.
        bool essential=(cleanup_ && strcmp(phase,"RX_OBSERVED") && strcmp(phase,"REPLY_TIMING")) ||
                       !strcmp(phase,"PHOTO_END") || !strcmp(phase,"FATAL") || !strcmp(phase,"ERROR") ||
                       !strcmp(phase,"TIMEOUT") || !strcmp(phase,"WAIT_FAIL") || !strcmp(phase,"EXECUTION_FAIL");
        if((!limited_ || essential) && written_+length<=cap-footer) {
            if(fwrite(line,1,length,file_)!=length)failed_=true;else written_+=length;
        }
        if(fflush(file_)!=0 || ferror(file_))failed_=true;
        if(failed_)fprintf(stderr,"LOG_IO_ERROR: file write failed; live work will stop\n");
    }
    // Keep SSH output concise: protocol detail belongs to the independent file.
    bool announce=!strcmp(phase,"LOG_OPEN") || !strcmp(phase,"MODE") || !strcmp(phase,"RUN") ||
                  !strcmp(phase,"TRIAL_BEGIN") || !strcmp(phase,"PHOTO_BEGIN") || !strcmp(phase,"PHOTO_END") ||
                  !strcmp(phase,"DONE") || !strcmp(phase,"SESSION_END") || !strcmp(phase,"FATAL") ||
                  !strcmp(phase,"TIMEOUT") || strstr(phase,"ERROR") || strstr(phase,"FAIL");
    if(announce)fprintf(stderr,"%s",line);
}
void Log::enqueue(Event& e) {
    // Mutex contention may lose telemetry, never delay a DSI callback.
    if(pthread_mutex_trylock(&mutex_)!=0)return;
    e.trial=trial_;e.testId=testId_;
    if(count_==256)++dropped_;
    else {
        QueueEntry& slot=queue_[(read_+count_)%256];
        slot.payload.event=e;slot.isLink=false;++count_;
    }
    pthread_mutex_unlock(&mutex_);
}
void Log::recordLink(nbt::most::LinkState state,uint16_t address,uint32_t tag) {
    // DSI callback: fill only the small active payload in the preallocated ring.
    // No local Event, text formatting, memset, allocation or file I/O.
    if(pthread_mutex_trylock(&mutex_)!=0)return;
    if(count_==256)++dropped_;
    else {
        QueueEntry& slot=queue_[(read_+count_)%256];
        slot.payload.link.ms=monotonicMs();slot.payload.link.tag=tag;
        slot.payload.link.trial=trial_;slot.payload.link.testId=testId_;
        slot.payload.link.state=state;slot.payload.link.address=address;
        slot.isLink=true;++count_;
    }
    pthread_mutex_unlock(&mutex_);
}
void Log::message(const char* phase,const nbt::most::Msg& m,uint16_t source,uint32_t tag) {
    if(pthread_mutex_trylock(&mutex_)!=0)return;
    if(count_==256)++dropped_;
    else {
        QueueEntry& slot=queue_[(read_+count_)%256];Event& e=slot.payload.event;
        e.ms=monotonicMs();e.tag=tag;e.bytes=m.data.size();e.trial=trial_;e.testId=testId_;
        e.src=source;e.dst=m.dst;e.fkt=m.fkt;e.op=m.opType;e.tx=m.txType;
        strncpy(e.phase,phase,sizeof(e.phase)-1);e.phase[sizeof(e.phase)-1]=0;e.text[0]=0;
        e.prefixSize=m.data.size()<16?m.data.size():16;
        if(e.prefixSize)memcpy(e.prefix,&m.data[0],e.prefixSize);
        slot.isLink=false;++count_;
    }
    pthread_mutex_unlock(&mutex_);
}
void Log::response(const nbt::most::View& m) {
    if(pthread_mutex_trylock(&mutex_)!=0)return;
    if(count_==256)++dropped_;
    else {
        QueueEntry& slot=queue_[(read_+count_)%256];Event& e=slot.payload.event;
        e.ms=monotonicMs();e.tag=m.hdrTag;e.bytes=m.len;e.trial=trial_;e.testId=testId_;
        e.src=m.src;e.dst=m.dst;e.fkt=m.fkt;e.op=m.op;e.tx=m.txType;
        strcpy(e.phase,"RX_OBSERVED");e.text[0]=0;e.prefixSize=m.len<16?m.len:16;
        if(e.prefixSize)memcpy(e.prefix,m.data,e.prefixSize);
        slot.isLink=false;++count_;
    }
    pthread_mutex_unlock(&mutex_);
}
void Log::drain() {
    // Take one record at a time, never hold the producer mutex while writing.
    for(unsigned consumed=0;consumed<256;++consumed) {
        Event e;pthread_mutex_lock(&mutex_);
        if(!count_) {
            unsigned dropped=dropped_;dropped_=0;pthread_mutex_unlock(&mutex_);
            if(dropped)write("LOG_OVERFLOW","events_dropped=%u",dropped);
            break;
        }
        if(queue_[read_].isLink) {
            LinkEvent link=queue_[read_].payload.link;
            read_=(read_+1)%256;--count_;pthread_mutex_unlock(&mutex_);
            write("MOST_LINK","observed_during=R%03u test_at_event=%03u event_ms=%u state=%s self=0x%04X tag=%u",
                  link.trial,link.testId,link.ms,nbt::most::linkStateName(link.state),link.address,link.tag);
            continue;
        }
        e=queue_[read_].payload.event;read_=(read_+1)%256;--count_;pthread_mutex_unlock(&mutex_);
        if(e.text[0]) {write(e.phase,"observed_during=R%03u test_at_event=%03u event_ms=%u %s",e.trial,e.testId,e.ms,e.text);continue;}
        char hex[49];for(unsigned i=0;i<e.prefixSize;++i)snprintf(hex+3*i,4,"%02x ",e.prefix[i]);
        hex[3*e.prefixSize]=0;
        unsigned h=e.prefixSize>=2?(e.prefix[0]<<8)|e.prefix[1]:0;
        write(e.phase,"observed_during=R%03u test_at_event=%03u event_ms=%u src=0x%04x dst=0x%04x tag=%u fkt=0x%03x op=0x%x tx=%u payload_u16_hint=0x%02x bytes=%u prefix=%s",
              e.trial,e.testId,e.ms,e.src,e.dst,e.tag,e.fkt,e.op,e.tx,h,e.bytes,hex);
        if(!strcmp(e.phase,"TX_ATTEMPT") && e.fkt==0x20e && e.prefixSize>=4)
            write("SWAP_REQUEST","hide=0x%02x show=0x%02x; dispatch attempt, physical result unverified",h,(e.prefix[2]<<8)|e.prefix[3]);
        if(!strcmp(e.phase,"TX_ATTEMPT") && e.fkt==0x204 && e.prefixSize>=3)
            write(h==0x17?"FRAME_REQUEST":"VIS_REQUEST","handle=0x%02x on=%u; dispatch attempt",h,e.prefix[2]);
        timing(e);
    }
}
void Log::note(const char* phase,const char* text) {
    Event e;memset(&e,0,sizeof(e));e.ms=monotonicMs();
    snprintf(e.phase,sizeof(e.phase),"%s",phase);snprintf(e.text,sizeof(e.text),"%s",text);enqueue(e);
}
void Log::timing(const Event& e) {
    if(e.prefixSize<2)return;
    unsigned h=(e.prefix[0]<<8)|e.prefix[1];
    if(!strcmp(e.phase,"TX_ATTEMPT")) {
        unsigned i=0;
        for(;i<timingCount_;++i) {
            Timing& t=timings_[i];
            if(t.src==e.src && t.dst==e.dst && t.tag==e.tag && t.fkt==e.fkt && t.handle==h) {
                t.reused=true;return; // Never invent unique transaction IDs on a reused routing key.
            }
        }
        if(timingCount_==512){write("TIMING_LIMIT","512 keys retained; no eviction/retry");return;}
        Timing t;t.src=e.src;t.dst=e.dst;t.tag=e.tag;t.fkt=e.fkt;t.handle=h;t.ms=e.ms;
        t.trial=e.trial;t.testId=e.testId;t.reused=false;t.answered=false;
        timings_[timingCount_++]=t;
    } else if(!strcmp(e.phase,"RX_OBSERVED") &&
              (e.op==nbt::most::kOpResultAck || e.op==nbt::most::kOpErrorAck)) {
        for(unsigned i=0;i<timingCount_;++i) {
            Timing& t=timings_[i];
            if(t.src!=e.dst || t.dst!=e.src || t.tag!=e.tag || t.fkt!=e.fkt || t.handle!=h)continue;
            if(t.reused || t.answered)write("REPLY_TIMING","routing key reused/duplicate; elapsed NOT attributed; event_ms=%u",e.ms);
            else {
                write("REPLY_TIMING","recorded_key_once dispatch_test=%03u dispatch_occurrence=R%03u since_dispatch_attempt_ms=%u fkt=0x%03x handle=0x%02x; payload NOT validated, not device execution time",
                      t.testId,t.trial,e.ms-t.ms,e.fkt,h);
                t.answered=true;
            }
            return;
        }
        write("REPLY_TIMING","no recorded matching routing key; elapsed unavailable");
    }
}
bool Log::finish(int status) {
    if(!file_)return false;
    drain();
    if(!healthy() && status==0)status=4;
    char footer[512];
    int n=snprintf(footer,sizeof(footer),"LOG_END mono_ms=%u exit=%d log_limit=%u io_error=%u bytes_before_footer=%lu; physical_display=UNVERIFIED\n",
                   monotonicMs(),status,limited_?1:0,failed_?1:0,(unsigned long)written_);
    size_t len=n<0?0:(size_t)n;if(len>=sizeof(footer))len=sizeof(footer)-1;
    if(!failed_ && written_+len<=5000000) {
        if(fwrite(footer,1,len,file_)!=len || fflush(file_)!=0)failed_=true;
    }
    if(fclose(file_)!=0)failed_=true;file_=NULL;
    fprintf(stderr,"%s",footer);
    if(failed_)fprintf(stderr,"LOG_IO_ERROR: final log flush/close failed\n");
    return healthy();
}

}
