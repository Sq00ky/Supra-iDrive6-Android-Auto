#pragma once
#include "protocol.hpp"
#include "atomic.hpp"
#include <string.h>
namespace aa_cluster {
// SPSC: the protocol thread publishes complete access units; the socket worker
// releases them. Small P-frames share the same 4 MiB budget as large IDRs.
class EncodedQueue {
public:
    struct Item { unsigned epoch,len,offset;uint64_t pts; };
private:
    Item items_[QueueSize];
    uint8_t bytes_[QueueBytes];
    unsigned head_,tail_,produced_,consumed_;
    void copy(unsigned offset,const uint8_t* p,unsigned n) {
        if(!n)return;
        offset%=QueueBytes;unsigned first=QueueBytes-offset;if(first>n)first=n;
        memcpy(bytes_+offset,p,first);if(n>first)memcpy(bytes_,p+first,n-first);
    }
public:
    EncodedQueue():head_(0),tail_(0),produced_(0),consumed_(0) {}
    bool push(unsigned epoch,const uint8_t* a,unsigned na,const uint8_t* b,unsigned nb,uint64_t pts) {
        if(nb>MaxPayload||na>MaxPayload-nb)return false;
        unsigned h=atomic_load(&head_),p=atomic_load(&produced_),n=na+nb;
        if(!n||h-atomic_load(&tail_)>=QueueSize||n>QueueBytes-(p-atomic_load(&consumed_)))return false;
        copy(p,a,na);copy(p+na,b,nb);
        items_[h%QueueSize]={epoch,n,p%QueueBytes,pts};
        atomic_store(&produced_,p+n);atomic_store(&head_,h+1);return true;
    }
    const Item* front() const {
        unsigned t=atomic_load(&tail_);return t==atomic_load(&head_)?0:&items_[t%QueueSize];
    }
    const uint8_t* data(unsigned offset) const {return bytes_+offset;}
    void pop() {
        unsigned t=atomic_load(&tail_);
        atomic_store(&consumed_,atomic_load(&consumed_)+items_[t%QueueSize].len);
        atomic_store(&tail_,t+1);
    }
    void discard() {while(front())pop();}
};
}
