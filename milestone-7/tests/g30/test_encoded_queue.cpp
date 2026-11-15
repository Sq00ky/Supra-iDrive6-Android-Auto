#include "cluster/g30/encoded_queue.hpp"
#include <assert.h>
#include <stdio.h>
#include <vector>
using namespace aa_cluster;
static EncodedQueue queue;
static void check(unsigned epoch,const std::vector<uint8_t>& bytes) {
    const EncodedQueue::Item* i=queue.front();assert(i&&i->epoch==epoch&&i->len==bytes.size());
    for(unsigned j=0;j<i->len;++j)assert(queue.data((i->offset+j)%QueueBytes)[0]==bytes[j]);
    queue.pop();
}
int main() {
    std::vector<uint8_t> small(256,71),large(MaxPayload,0);
    for(unsigned i=0;i<large.size();++i)large[i]=uint8_t(i*37);
    for(unsigned i=0;i<QueueSize;++i)assert(queue.push(i,0,0,small.data(),small.size(),i));
    assert(!queue.push(1,0,0,small.data(),small.size(),0));
    for(unsigned i=0;i<QueueSize;++i)check(i,small);
    // Large frames are still capped at 4 MiB, including wrapped allocations.
    for(unsigned pass=0;pass<3;++pass) {
        for(unsigned i=0;i<8;++i)assert(queue.push(i,large.data(),17,large.data()+17,large.size()-17,i));
        assert(!queue.push(1,0,0,small.data(),small.size(),0));
        for(unsigned i=0;i<8;++i)check(i,large);
    }
    assert(!queue.push(1,0,0,large.data(),MaxPayload+1,0));
    assert(queue.push(9,0,0,small.data(),small.size(),0));queue.discard();assert(!queue.front());
    puts("AA_ENCODED_QUEUE_OK burst=256 bytes=4194304 wrapping=1 capacity_reuse=1");
}
