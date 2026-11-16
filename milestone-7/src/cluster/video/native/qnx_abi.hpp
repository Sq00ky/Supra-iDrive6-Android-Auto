// QNX SDK APIs plus adapters for the verified NBT wire-address structures.
#pragma once
#include <stddef.h>
#ifdef NBT_CLUSTER_HOST_TEST
#ifdef __QNXNTO__
#error NBT_CLUSTER_HOST_TEST is only for host regression binaries
#endif
#include <qnx_api_stub.hpp>
#else
#ifndef __QNXNTO__
#error Build cluster video with the QNX SDK; host tests must select NBT_CLUSTER_HOST_TEST
#endif
#include <stdint.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>
typedef struct pollfd PollFD;
typedef struct timespec PipClock;

// The wire-address structs are deliberately shared with the proven car demo.
// Use SDK socket declarations while retaining those byte-exact structures.
inline int connect(int socket_fd, const void* address, unsigned address_size) {
    return ::connect(socket_fd, (const sockaddr*)address, address_size);
}

inline int bind(int socket_fd, const void* address, unsigned address_size) {
    return ::bind(socket_fd, (const sockaddr*)address, address_size);
}

inline int getsockname(int socket_fd, void* address, unsigned* address_size) {
    return ::getsockname(socket_fd, (sockaddr*)address, address_size);
}

inline int sendto(int socket_fd, const void* data, unsigned size, int flags,
                  const void* destination, unsigned address_size) {
    return ::sendto(socket_fd, data, size, flags, (const sockaddr*)destination, address_size);
}
#endif

namespace kombi {
namespace pip {
namespace detail {

// Monotonic milliseconds intentionally wrap at 32 bits. Deadline comparisons
// use signed differences; elapsed-time comparisons use unsigned differences.
inline uint32_t millis() {
    PipClock now = {};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return uint32_t(now.tv_sec) * 1000u + uint32_t(now.tv_nsec / 1000000);
}

} // namespace detail
} // namespace pip
} // namespace kombi
