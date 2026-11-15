#pragma once
#include "atomic.hpp"
#include "protocol.hpp"
namespace aa_cluster {
// Single producer (socket worker), single consumer (AA session thread).
// Epochs prevent a late completion from acknowledging a replacement stream.
struct Credits {
    unsigned head,tail,epochs[QueueSize];
    bool publish(unsigned epoch) {
        unsigned h=atomic_load(&head);
        if(h-atomic_load(&tail)>=QueueSize)return false;
        epochs[h%QueueSize]=epoch;atomic_store(&head,h+1);return true;
    }
    unsigned take(unsigned current) {
        unsigned t=atomic_load(&tail),h=atomic_load(&head),count=0;
        while(t!=h) {
            if(epochs[t%QueueSize]==current)++count;
            atomic_store(&tail,++t);
        }
        return count;
    }
};
}
