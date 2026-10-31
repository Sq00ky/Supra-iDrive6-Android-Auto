// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Exported HMI services: plugin resolver, main dispatcher unit list, processing-unit timer.
// Layouts are documented in plugin_abi.hpp (reversed from libhmicom.so / HmiMain3D).
#include "hmi_services.hpp"
#include <dlfcn.h>
#include <string.h>
#include <stdio.h>

void nbtPluginLog(const char* fmt, ...);   // defined in nbt_aad_plugin.cpp

namespace nbt {
namespace hmi {

namespace {

typedef void (*CapuStrCtor)(void* self, const char* s);
typedef void (*CapuStrDtor)(void* self);
typedef void (*MutexOp)(void* self);
typedef void* (*TreeNext)(void* node);
typedef int (*ResolveFn)(void* resolver, const void* name, void** out, int flags);
typedef void* (*GetTimerFn)(void* procUnit);
typedef int (*SetTimerFn)(void* timer, void* target, void* fn, int adj, unsigned firstMs, unsigned periodMs,
                          unsigned user);

CapuStrCtor s_strCtor = 0;
CapuStrDtor s_strDtor = 0;
MutexOp s_mutexTake = 0;
MutexOp s_mutexGive = 0;
TreeNext s_treeNext = 0;

void* sym(const char* name) {
    void* p = dlsym(RTLD_DEFAULT, name);
    if (!p) nbtPluginLog("symbol %s not found", name);
    return p;
}

inline void* word(const void* base, size_t off) {
    return *reinterpret_cast<void* const*>(reinterpret_cast<const char*>(base) + off);
}

const char* rttiName(const void* obj) {
    const void* vtbl = word(obj, 0);
    if (!vtbl) return "";
    const void* ti = *(reinterpret_cast<void* const*>(vtbl) - 1);
    if (!ti) return "";
    const char* n = reinterpret_cast<const char*>(word(ti, 4));
    return n ? n : "";
}

} // namespace

bool loadSymbols() {
    s_strCtor = reinterpret_cast<CapuStrCtor>(sym("_ZN4capu6StringC1EPKc"));
    s_strDtor = reinterpret_cast<CapuStrDtor>(sym("_ZN4capu6StringD1Ev"));
    s_mutexTake = reinterpret_cast<MutexOp>(sym("_ZN8COSMutex4takeEv"));
    s_mutexGive = reinterpret_cast<MutexOp>(sym("_ZN8COSMutex4giveEv"));
    s_treeNext = reinterpret_cast<TreeNext>(sym("_ZSt18_Rb_tree_incrementPSt18_Rb_tree_node_base"));
    return s_strCtor && s_strDtor && s_mutexTake && s_mutexGive && s_treeNext;
}

void* resolvePlugin(void* resolver, const char* name) {
    if (!resolver || !s_strCtor) return 0;
    char str[32];                                   // capu::String (16 bytes used)
    memset(str, 0, sizeof(str));
    s_strCtor(str, name);
    void* out = 0;
    ResolveFn fn = reinterpret_cast<ResolveFn>(reinterpret_cast<void* const*>(word(resolver, 0))[2]);
    int rc = fn(resolver, str, &out, 0);
    s_strDtor(str);
    nbtPluginLog("resolve(%s) -> rc %d, %p", name, rc, out);
    return rc == 0 ? out : 0;
}

// CHmiMainDispatcher: std::map<uint, std::vector<IHmiProcUnit*>> header at +4 (leftmost +0xc),
// node vector begin/end at +0x14/+0x18, guarded by COSMutex at +0x18.
void* findAppProcUnit(void* disp) {
    if (!disp) return 0;
    char* d = reinterpret_cast<char*>(disp);
    void* found = 0;
    s_mutexTake(d + 0x18);
    void* header = d + 4;
    for (void* n = word(d, 0xc); n && n != header && !found; n = s_treeNext(n)) {
        void** it = reinterpret_cast<void**>(word(n, 0x14));
        void** end = reinterpret_cast<void**>(word(n, 0x18));
        for (; it && it != end; ++it) {
            if (*it && strcmp(rttiName(*it), "15CHmiAppProcUnit") == 0) { found = *it; break; }
        }
    }
    s_mutexGive(d + 0x18);
    return found;
}

// IHmiProcUnit vtbl[3] = getTimer(); IHmiTimer vtbl[1] = setTimer(target, memfn{fn, adj}, first, period, user).
int armTimer(void* pu, void* target, TimerFn fn, unsigned delayMs) {
    if (!pu) return 0;
    GetTimerFn gt = reinterpret_cast<GetTimerFn>(reinterpret_cast<void* const*>(word(pu, 0))[3]);
    void* timer = gt(pu);
    if (!timer) return 0;
    SetTimerFn st = reinterpret_cast<SetTimerFn>(reinterpret_cast<void* const*>(word(timer, 0))[1]);
    return st(timer, target, reinterpret_cast<void*>(fn), 0, delayMs < 10 ? 10 : delayMs, 0, 0);
}

} // namespace hmi
} // namespace nbt
