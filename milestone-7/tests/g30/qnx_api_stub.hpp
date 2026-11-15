#pragma once
// Minimal QNX-shaped test API. Each test supplies its own in-memory peers;
// these declarations are never available on the production include path.
#if !defined(NBT_G30_HOST_TEST) || defined(__QNXNTO__)
#error QNX API stubs are only for explicitly selected host regression tests
#endif
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
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
struct PollFD { int fd; short events, revents; };
int poll(PollFD*, unsigned, int);
int* __get_errno_ptr();
struct PipClock { long tv_sec, tv_nsec; };
int clock_gettime(int, PipClock*);
int listen(int, int);
int accept(int, void*, unsigned*);
void (*signal(int, void (*)(int)))(int);
}
#define CLOCK_MONOTONIC 2
