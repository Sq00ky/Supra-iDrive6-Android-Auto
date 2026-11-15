// QNX 6 ARM ABI used by the hash-pinned NBT runtime. Not host POSIX constants.
#pragma once
#include <stddef.h>
#ifdef IDRIVER_QNX_SDK
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
#else
extern "C" {
int socket(int, int, int);
int connect(int, const void*, unsigned);
int bind(int, const void*, unsigned);
int getsockname(int, void*, unsigned*);
int getsockopt(int, int, int, void*, unsigned*);
int fcntl(int, int, ...);
int send(int, const void*, unsigned, int);
int sendto(int, const void*, unsigned, int, const void*, unsigned);
int recv(int, void*, unsigned, int);
int close(int);
int usleep(unsigned);
int printf(const char*, ...);
int fflush(void*);
void* malloc(size_t);
void free(void*);
struct PollFD { int fd; short events, revents; };
int poll(PollFD*, unsigned, int);
int* __get_errno_ptr();
void* fopen(const char*, const char*);
size_t fread(void*, size_t, size_t, void*);
int fclose(void*);
int fseek(void*, long, int);
long ftell(void*);
struct PipClock { long seconds, nanoseconds; };
int clock_gettime(int, PipClock*);
}
#endif

namespace kombi { namespace pip { namespace detail {
#ifdef IDRIVER_QNX_SDK
inline uint32_t millis() { PipClock t = {}; clock_gettime(CLOCK_MONOTONIC, &t); return uint32_t(t.tv_sec) * 1000u + uint32_t(t.tv_nsec / 1000000); }
#else
inline uint32_t millis() { PipClock t = {}; clock_gettime(2, &t); return uint32_t(t.seconds) * 1000u + uint32_t(t.nanoseconds / 1000000); }
#endif
}}}
