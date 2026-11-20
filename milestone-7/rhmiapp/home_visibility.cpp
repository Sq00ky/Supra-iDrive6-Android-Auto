// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "home_visibility.hpp"
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <dlfcn.h>
#include <sys/stat.h>
#ifndef RTLD_DEFAULT
#define RTLD_DEFAULT ((void*)-2)
#endif

namespace nbt { namespace home {
#ifdef __QNX__
namespace {
bool s_ownsVisibility = false;
unsigned s_waits = 0;

void** table(void* object) { return *reinterpret_cast<void***>(object); }
const char* name(void* object) {
    if (!object || !table(object) || !table(object)[-1]) return "";
    const char* result = reinterpret_cast<const char* const*>(table(object)[-1])[1];
    return result ? result : "";
}
bool hasTable(void* object, const char* symbol, const char* type) {
    void* base = dlsym(RTLD_DEFAULT, symbol);
    return object && base && table(object) == static_cast<void**>(base) + 2 && !strcmp(name(object), type);
}
void waiting(LogFn logger, const char* message) {
    if (logger && (s_waits == 0 || s_waits == 29)) logger(message);
    ++s_waits;
}
}

bool updateNativeVisibility(void* factory, void* variants, void* carplay,
    bool aaWanted, bool probeOnly, bool stopping, LogFn logger) {
    // The visibility adapter is limited to the proven small ID6L shortcut.
    // Do not turn this into a coding/SWT setter or a fake CarPlay connection.
    struct stat st;
    typedef bool (*Query)(void*, unsigned);
    Query query = reinterpret_cast<Query>(dlsym(RTLD_DEFAULT, "_ZN17CHmiVariantServer12isVariantSetEj"));
    if (!query || !hasTable(variants, "_ZTV17CHmiVariantServer", "17CHmiVariantServer") ||
        stat("/opt/hmi/ID5/lib/libhmicoding.so", &st) != 0 || st.st_size != 923344)
        return false;
    if (!query(variants, 0x5383)) return false;
    if (!aaWanted && !s_ownsVisibility) return true;

    if (!carplay || reinterpret_cast<uintptr_t>(table(carplay)) != 0x02c52bd0u ||
        strcmp(name(carplay), "18CHmiCarPlayService")) {
        waiting(logger, "[Home] visibility: CarPlay service interface not verified; no model change");
        return true;
    }
    // Native getter 0x45e1b8 returns this byte. Read, never modify it.
    const bool realCarplay = *reinterpret_cast<unsigned char*>(static_cast<char*>(carplay) + 0x60) != 0;
    const bool own = aaWanted && !probeOnly && !stopping && !realCarplay;
    const bool visibilityChanged = own != s_ownsVisibility;

    const uint32_t expected[] = { 0xe92d49f0u, 0xe28db018u, 0xe24dd014u, 0xe1a05001u };
    const uintptr_t setter = 0x0045e638u;
    if (memcmp(reinterpret_cast<const void*>(setter), expected, sizeof(expected)) ||
        *reinterpret_cast<const uint32_t*>(0x0045e804u) != 0x02c52f0du ||
        strcmp(reinterpret_cast<const char*>(0x02c52f0du), "CarPlayVisible")) {
        waiting(logger, "[Home] visibility: native setter profile mismatch; no model change");
        return true;
    }
    if (!hasTable(factory, "_ZTV13CHmiUIFactory", "13CHmiUIFactory")) {
        waiting(logger, "[Home] visibility: UI factory interface not verified; waiting");
        return true;
    }
    // Mirror the native setter's lookup, but check readiness before calling it.
    // Factory slot 3 -> layer 1; container slot 55 -> main scene;
    // scene slot 96 -> reference to the model's shared-pointer holder.
    typedef void* (*GetLayer)(void*, int, int);
    typedef void* (*GetScene)(void*, int);
    typedef void* (*GetModel)(void*);
    void* container = reinterpret_cast<GetLayer>(table(factory)[3])(factory, 1, 1);
    if (!hasTable(container, "_ZTV19CHmiUIMainContainer", "19CHmiUIMainContainer")) {
        waiting(logger, "[Home] visibility: main UI container not ready/verified; waiting");
        return true;
    }
    void* scene = reinterpret_cast<GetScene>(table(container)[55])(container, 0);
    void* holder = scene ? reinterpret_cast<GetModel>(table(scene)[96])(scene) : NULL;
    if (!holder || !*static_cast<void**>(holder)) {
        waiting(logger, "[Home] visibility: main scene model not ready; waiting");
        return true;
    }
    if (!visibilityChanged) return true;
    // Native CP owns its transitions. Do not reapply its visibility property
    // when the real service is available; only release our AA ownership.
    if (realCarplay) {
        s_ownsVisibility = false;
        if (logger) logger("[Home] visibility: released AA ownership to native CarPlay; no visibility write");
        return true;
    }
    // The complete native function does not use its original this argument.
    // It constructs/owns the correct bool-variant type and writes only the
    // scene model property. It does not set service availability or SWT state.
    typedef void (*SetVisible)(void*, bool);
    reinterpret_cast<SetVisible>(setter)(NULL, own || realCarplay);
    s_ownsVisibility = own;
    s_waits = 0;
    if (logger) logger(own ?
        "[Home] visibility: requested native ID6L shortcut for AA" :
        "[Home] visibility: restored scene property to actual CarPlay availability");
    return true;
}
#else
bool updateNativeVisibility(void*, void*, void*, bool, bool, bool, LogFn) { return false; }
#endif
} }
