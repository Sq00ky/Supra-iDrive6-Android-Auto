#pragma once

#include <stddef.h>
#include <stdint.h>

extern "C" {

typedef void (*ClusterSetup)(void* session, unsigned channel);
typedef void (*ClusterFocus)(void* session, unsigned channel, bool focused, bool unsolicited);
typedef void (*ClusterAck)(void* session, unsigned channel);

void idriver_cluster_configure_acks(ClusterAck callback);
void idriver_cluster_drain_acks(void* session);
void idriver_cluster_tick(void* session);
unsigned idriver_cluster_ack_window();
void idriver_cluster_init(ClusterSetup setup, ClusterFocus focus);
// Result: 1 consumes setup/focus; 2 defers a queued media ACK; 0 uses stock dispatch.
int idriver_cluster_receive(void* session, unsigned channel, const uint8_t* bytes, size_t size);
void idriver_cluster_end();
const uint8_t* idriver_cluster_manifest(const uint8_t* bytes, size_t size);

} // extern "C"
