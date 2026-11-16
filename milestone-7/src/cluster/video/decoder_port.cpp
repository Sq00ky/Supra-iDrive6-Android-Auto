// OpenH264's single-thread decoder port. No QNX pthread structure layout is
// assumed. Accidentally enabling the codec's internal worker pool aborts.
#include "wels_decoder_thread.h"
#include <stdlib.h>
#include <stddef.h>

int32_t GetCPUCount() {
    return 1;
}
int EventCreate(SWelsDecEvent*, int, int) {
    abort();
}
void EventPost(SWelsDecEvent*) {
    abort();
}
int EventWait(SWelsDecEvent*, int32_t) {
    abort();
}
void EventReset(SWelsDecEvent*) {
    abort();
}
void EventDestroy(SWelsDecEvent*) {
    abort();
}
int SemCreate(SWelsDecSemphore*, long, long) {
    abort();
}
int SemWait(SWelsDecSemphore*, int32_t) {
    abort();
}
void SemRelease(SWelsDecSemphore*, long*) {
    abort();
}
void SemDestroy(SWelsDecSemphore*) {
    abort();
}
int ThreadCreate(SWelsDecThread*, LPWELS_THREAD_ROUTINE, void*) {
    abort();
}
int ThreadWait(SWelsDecThread*) {
    abort();
}
extern "C" {
int WelsMutexInit(WELS_MUTEX*) {
    abort();
}
int WelsMutexDestroy(WELS_MUTEX*) {
    abort();
}
int WelsMutexLock(WELS_MUTEX*) {
    abort();
}
int WelsMutexUnlock(WELS_MUTEX*) {
    abort();
}
}

#ifdef IDRIVER_QNX
#include <stdio.h>
#include <stdarg.h>
extern "C" int idriver_codec_fprintf(FILE*, const char* format, ...) {
    char line[1024];
    va_list arguments;
    va_start(arguments, format);
    int length = vsnprintf(line, sizeof(line), format, arguments);
    va_end(arguments);
    printf("[OpenH264] %s", line);
    return length;
}
// Freestanding C++ runtime: decoder allocation failures follow the host's
// fail-stop policy; the supervisor owns PiP cleanup if this process exits.
void* operator new(size_t size) {
    void* allocation = malloc(size ? size : 1);
    if (!allocation) {
        abort();
    }
    return allocation;
}
void* operator new[](size_t size) {
    return operator new(size);
}
void operator delete(void* allocation) noexcept {
    free(allocation);
}
void operator delete[](void* allocation) noexcept {
    free(allocation);
}
extern "C" void __cxa_pure_virtual() {
    abort();
}
// This helper is absent from BMW's libc. Split into exact 32-bit terms so the
// compiler emits VFP conversions, not a recursive 64-bit conversion helper.
extern "C" double __aeabi_l2d(int64_t value) {
    uint64_t magnitude = value < 0 ? uint64_t(0) - uint64_t(value) : uint64_t(value);
    double result = double(uint32_t(magnitude >> 32)) * 4294967296.0 + double(uint32_t(magnitude));
    return value < 0 ? -result : result;
}
extern "C" void __aeabi_memcpy(void* destination, const void* source, size_t size) {
    unsigned char* output = (unsigned char*)destination;
    const unsigned char* input = (const unsigned char*)source;
    for (size_t i = 0; i < size; ++i) {
        output[i] = input[i];
    }
}
extern "C" void __aeabi_memcpy4(void* destination, const void* source, size_t size) {
    __aeabi_memcpy(destination, source, size);
}
extern "C" void __aeabi_memcpy8(void* destination, const void* source, size_t size) {
    __aeabi_memcpy(destination, source, size);
}
extern "C" void __aeabi_memclr(void* destination, size_t size) {
    for (size_t i = 0; i < size; ++i) {
        ((unsigned char*)destination)[i] = 0;
    }
}
extern "C" void __aeabi_memclr4(void* destination, size_t size) {
    __aeabi_memclr(destination, size);
}
extern "C" void __aeabi_memclr8(void* destination, size_t size) {
    __aeabi_memclr(destination, size);
}
#endif
