#ifndef NBT_NAV_DIAG_TRACE_HPP
#define NBT_NAV_DIAG_TRACE_HPP
// Metadata-only multi-producer trace. No payload, parsing, allocation or I/O.
#include <stdint.h>
#include <pthread.h>
#include <time.h>
namespace nbt { namespace navtrace {
inline uint64_t now() { timespec t;return clock_gettime(CLOCK_MONOTONIC,&t)?0:(uint64_t)t.tv_sec*1000000000ull+t.tv_nsec; }
enum Stage { Receive, ParserEnter, StateStored, SnapshotDecoded, DisplayBegin, DisplayEnd, MostSend, FragmentDrop, MostBegin, SnapshotBegin };
inline const char* name(Stage s) {
    static const char* names[]={"aa_receive","parser_enter","state_store_done","snapshot_decoded",
        "display_submit_begin","display_submit_end","most_send_return","aa_fragment_drop","most_send_begin","snapshot_decode_begin"};
    return names[(unsigned)s<10?(unsigned)s:0];
}
struct Token {
    uint64_t sequence,rx_ns;uint32_t connection;uint16_t channel,id;
    Token():sequence(0),rx_ns(0),connection(0),channel(0),id(0){}
};
struct Event {
    uint64_t ns,serial;Token token;Stage stage;int result,type;int64_t exit,angle;double metres;uint16_t handle;
};
struct Buffer {
    enum { Capacity=128 };
    pthread_mutex_t mutex;bool ready,enabled;unsigned read,write,dropped;uint64_t serial;Token latest;
    Event queue[Capacity];
    Buffer():ready(false),enabled(false),read(0),write(0),dropped(0),serial(0) { ready=pthread_mutex_init(&mutex,NULL)==0; }
    ~Buffer() { if(ready)pthread_mutex_destroy(&mutex); }
};
inline Buffer& buffer() { static Buffer b;return b; }
// Lifecycle only, outside RX. There is no Recorder pointer to outlive its owner.
inline void enable(bool on) { Buffer& b=buffer();if(!b.ready)return;pthread_mutex_lock(&b.mutex);b.enabled=on;
    if(on){b.read=b.write=b.dropped=0;b.serial=0;b.latest=Token();}pthread_mutex_unlock(&b.mutex); }
inline Token latest(uint16_t id) { Buffer& b=buffer();Token t;if(!b.ready)return t;if(pthread_mutex_trylock(&b.mutex))return t;
    if(b.enabled && (!id||b.latest.id==id))t=b.latest;pthread_mutex_unlock(&b.mutex);return t; }
inline void event(Stage stage,const Token& token=Token(),int result=0,int type=-1,
                  int64_t exit=-1,int64_t angle=-1,double metres=-1,uint16_t handle=0) {
    Buffer& b=buffer();if(!b.ready)return;if(pthread_mutex_trylock(&b.mutex))return;
    if(b.enabled) {
        if(stage==Receive)b.latest=token;
        if(b.write-b.read==Buffer::Capacity)++b.dropped;
        else {Event& e=b.queue[b.write%Buffer::Capacity];e.ns=now();e.serial=++b.serial;e.token=token;
            e.stage=stage;e.result=result;e.type=type;e.exit=exit;e.angle=angle;e.metres=metres;e.handle=handle;++b.write;}
    }
    pthread_mutex_unlock(&b.mutex);
}
inline void fragmentDrop(uint16_t channel,int reason) { Token t;t.channel=channel;event(FragmentDrop,t,reason); }
inline bool take(Event& e,unsigned& dropped) { Buffer& b=buffer();bool have=false;if(!b.ready)return false;
    pthread_mutex_lock(&b.mutex);dropped=b.dropped;
    if(b.read!=b.write){e=b.queue[b.read%Buffer::Capacity];++b.read;have=true;}
    pthread_mutex_unlock(&b.mutex);return have;
}
} }
#endif
