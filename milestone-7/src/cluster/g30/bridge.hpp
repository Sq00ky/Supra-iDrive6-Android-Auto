#pragma once
#include <stddef.h>
#include <stdint.h>
extern "C" {
typedef void (*ClusterSetup)(void*,unsigned);
typedef void (*ClusterFocus)(void*,unsigned,bool,bool);
typedef void (*ClusterAck)(void*,unsigned);
void idriver_cluster_configure_acks(ClusterAck);
void idriver_cluster_drain_acks(void*);
void idriver_cluster_tick(void*);
unsigned idriver_cluster_ack_window();
void idriver_cluster_init(ClusterSetup,ClusterFocus);
// Result: 1 consumes setup/focus; 2 defers a queued media ACK; 0 uses stock dispatch.
int idriver_cluster_receive(void*,unsigned,const uint8_t*,size_t);
void idriver_cluster_end();
const uint8_t* idriver_cluster_manifest(const uint8_t*,size_t);
}
