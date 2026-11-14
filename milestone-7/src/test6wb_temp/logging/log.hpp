// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#ifndef TEST6WB_LOG_HPP
#define TEST6WB_LOG_HPP
#include "most/msg.hpp"
#include "most/client.hpp"
#include <stdio.h>
#include <pthread.h>
#include <stdint.h>
namespace test6wb {
uint32_t monotonicMs();
// Only the owner thread writes to disk. Callback producers use a bounded queue.
class Log {
public:
    Log();
    bool open(const char* path);
    void setTrial(unsigned id,unsigned testId=0);
    void beginCleanup() {cleanup_=true;}
    bool finish(int status);
    void note(const char* phase,const char* text);
    void recordLink(nbt::most::LinkState state,uint16_t address,uint32_t tag);
    void write(const char* phase,const char* format,...);
    void message(const char* phase,const nbt::most::Msg& m,uint16_t source,uint32_t tag);
    void response(const nbt::most::View& m);
    void drain();
    bool healthy() const {return !failed_ && !limited_;}
private:
    struct Event {
        uint32_t ms,tag,bytes;unsigned trial,testId;uint16_t src,dst,fkt;uint8_t op,tx;
        char phase[16];uint8_t prefix[16];unsigned prefixSize;
        char text[160];
    };
    struct LinkEvent {
        uint32_t ms,tag;unsigned trial,testId;
        nbt::most::LinkState state;uint16_t address;
    };
    struct QueueEntry {
        bool isLink;
        union Payload {Event event;LinkEvent link;} payload;
    };
    struct Timing {uint16_t src,dst,fkt,handle;uint32_t tag,ms;unsigned trial,testId;bool reused,answered;};
    void timing(const Event& e);
    void enqueue(Event& e);
    FILE* file_;bool failed_,limited_,cleanup_;size_t written_;
    pthread_mutex_t mutex_;QueueEntry queue_[256];unsigned read_,count_,dropped_,trial_,testId_;
    Timing timings_[512];unsigned timingCount_;
};
}
#endif
