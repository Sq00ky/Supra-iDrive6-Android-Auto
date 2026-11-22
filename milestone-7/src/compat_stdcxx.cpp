// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
//
// Compat shim for building nbt-aad with a GNU libstdc++ toolchain (our QNX GCC 4.9.4) and
// running it against the head unit's OLDER libstdc++.so.6 (6.0.13, GCC 4.4 era).
//
// The daemon must link libstdc++ DYNAMICALLY: the OEM graphics libs (libscreen -> libKD/libEGL/
// libGLESv2) are built against the shared libstdc++.so.6 and expect it loaded in the process; a
// statically-linked C++ runtime leaves their C++ symbols unresolved and the process faults on init
// (libKD "Unresolved symbols" -> SIGSEGV). But GCC >= 4.8 emits two libstdc++ references the head's
// 6.0.13 does not export: std::__throw_out_of_range_fmt (newer bounds-checked .at()) and
// __cxa_throw_bad_array_new_length (C++11 new[] length check). We define them here, forwarding to
// equivalents that ARE in 6.0.13, so everything else resolves against the head's libstdc++ and these
// two resolve locally. No-op under a non-GNU C++ runtime (e.g. Dinkum, which has its own).
#if defined(__GLIBCXX__)
#include <stdexcept>
#include <new>
#include <string>
#include <cstdarg>
#include <cstdio>

namespace std {
void __throw_out_of_range_fmt(const char* fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    throw std::out_of_range(std::string(buf));
}
}  // namespace std

extern "C" void __cxa_throw_bad_array_new_length() {
    throw std::bad_alloc();
}
#endif
