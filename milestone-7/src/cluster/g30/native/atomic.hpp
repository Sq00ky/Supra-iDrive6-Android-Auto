#pragma once
namespace kombi { namespace pip { namespace detail {
#if defined(__QNXNTO__) && defined(__arm__) && __GNUC__ == 4 && __GNUC_MINOR__ < 7
// QNX GCC 4.4 emits unresolved __sync_* calls for ARM. These aligned 32-bit
// accesses use the ARMv7 acquire/release barriers directly. Exchange is the
// only read-modify-write operation and retries when its exclusive store loses.
inline unsigned atomic_load(const unsigned* p) {
    unsigned value;
    __asm__ __volatile__("ldr %0, [%1]\n\tdmb" : "=r"(value) : "r"(p) : "memory");
    return value;
}
inline void atomic_store(unsigned* p,unsigned value) {
    __asm__ __volatile__("dmb\n\tstr %1, [%0]" : : "r"(p),"r"(value) : "memory");
}
inline unsigned atomic_exchange(unsigned* p,unsigned value) {
    unsigned old,status;
    __asm__ __volatile__("dmb\n1:\tldrex %0, [%2]\n\tstrex %1, %3, [%2]\n\t"
                         "cmp %1, #0\n\tbne 1b\n\tdmb"
                         : "=&r"(old),"=&r"(status) : "r"(p),"r"(value) : "cc","memory");
    return old;
}
#else
inline unsigned atomic_load(const unsigned* p) { return __atomic_load_n(p,__ATOMIC_ACQUIRE); }
inline void atomic_store(unsigned* p,unsigned v) { __atomic_store_n(p,v,__ATOMIC_RELEASE); }
inline unsigned atomic_exchange(unsigned* p,unsigned v) { return __atomic_exchange_n(p,v,__ATOMIC_ACQ_REL); }
#endif
}}}
