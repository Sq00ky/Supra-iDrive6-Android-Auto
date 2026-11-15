// QNX SDK APIs plus adapters for the verified NBT wire-address structures.
#pragma once
#include <stddef.h>
#ifdef NBT_G30_HOST_TEST
#ifdef __QNXNTO__
#error NBT_G30_HOST_TEST is only for host regression binaries
#endif
#include <qnx_api_stub.hpp>
#else
#ifndef __QNXNTO__
#error Build the G30 runtime with the QNX SDK; host tests must select NBT_G30_HOST_TEST
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
inline int connect(int fd,const void* p,unsigned n) { return ::connect(fd,(const sockaddr*)p,n); }
inline int bind(int fd,const void* p,unsigned n) { return ::bind(fd,(const sockaddr*)p,n); }
inline int getsockname(int fd,void* p,unsigned* n) { return ::getsockname(fd,(sockaddr*)p,n); }
inline int sendto(int fd,const void* p,unsigned n,int flags,const void* a,unsigned an) {
    return ::sendto(fd,p,n,flags,(const sockaddr*)a,an);
}
#endif

namespace kombi { namespace pip { namespace detail {
inline uint32_t millis() { PipClock t = {}; clock_gettime(CLOCK_MONOTONIC, &t); return uint32_t(t.tv_sec) * 1000u + uint32_t(t.tv_nsec / 1000000); }
}}}
