// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include "home_selection.hpp"
#include "home_setting.hpp"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <dlfcn.h>
#ifndef RTLD_DEFAULT
#define RTLD_DEFAULT ((void*)-2)
#endif

namespace nbt { namespace home {
#ifdef __QNX__
namespace {
// All state here is accessed on AppProcUnit, including the native callback.
void* s_registry = NULL;
void* s_command = NULL; // one retained native intrusive reference, HMI lifetime
void* s_carplay = NULL;
bool s_registered = false, s_active = false, s_rejected = false;
bool s_reportedSignal = false;
unsigned s_waits = 0;
int s_target = 0;
uint32_t s_commandId = 0;
const uint32_t* s_signalId = NULL;
const void* s_parameterId = NULL;
const void* s_signalType = NULL;
LogFn s_logger = NULL;
SelectFn s_selected = NULL;

typedef bool (*Contains)(void*, const uint32_t*, void**, int);
typedef void (*Register)(void*, const uint32_t*, void**, int);
typedef void (*MakeCommand)(void**, void*, void*, int);
typedef const void* (*GetParameter)(const void*, const void*);

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
void waiting(const char* message) {
    if (s_logger && (s_waits == 0 || s_waits == 29)) s_logger(message);
    ++s_waits;
}
bool realCarplayAvailable() {
    // Match the primary service interface and native availability getter.
    return !s_carplay || reinterpret_cast<uintptr_t>(table(s_carplay)) != 0x02c52bd0u ||
        strcmp(name(s_carplay), "18CHmiCarPlayService") ||
        *reinterpret_cast<unsigned char*>(static_cast<char*>(s_carplay) + 0x60) != 0;
}

int onWidgetSignal(void*, const void* params) {
    if (!s_active || !s_selected || !params || realCarplayAvailable() ||
        !shortcutEnabled() ||
        access("/dev/shmem/nbt-aad-home-probe", F_OK) == 0) return 0;
    const void* variant = reinterpret_cast<GetParameter>(0x018b70bcu)(params, s_parameterId);
    if (!variant) return 0;
    const char* bytes = static_cast<const char*>(variant);
    const void* descriptor = *reinterpret_cast<void* const*>(bytes + 4);
    // Mirror native CHmiHomeTouchHandling's type check before reading the
    // inline CGfxIdentifier<9>. Ignore every other widget signal/variant type.
    const bool typeMatches = descriptor &&
        *reinterpret_cast<void* const*>(static_cast<const char*>(descriptor) + 16) == s_signalType;
    if (!s_reportedSignal && s_logger) {
        s_logger(typeMatches ? "[Home] selection: first widget signal received; parameter type verified" :
            "[Home] selection: first widget signal has a different parameter type; ignored");
        s_reportedSignal = true;
    }
    if (!typeMatches)
        return 0;
    if (*reinterpret_cast<const uint32_t*>(bytes + 8) != *s_signalId) return 0;
    if (s_logger) s_logger("[Home] selection: native shortcut chosen; queued Android Auto restore");
    s_selected(); // only queues selection; PluginApp performs start/show later
    return 1;
}

bool profile() {
    struct Check { uintptr_t address; uint32_t words[4]; };
    const Check checks[] = {
        { 0x002278f4u, { 0xe92d4df0u, 0xe28db01cu, 0xe24dd030u, 0xe59f012cu } },
        { 0x0033b03cu, { 0xe92d4830u, 0xe28db00cu, 0xe24be02cu, 0xe24dd020u } },
        { 0x018b70bcu, { 0xe92d4bf0u, 0xe59f40ecu, 0xe59f50ecu, 0xe28db01cu } },
        { 0x003011d4u, { 0xe92d4830u, 0xe1a0c001u, 0xe28db00cu, 0xe24dd010u } },
        { 0x00303d70u, { 0xe92d4df0u, 0xe28db01cu, 0xe24dd068u, 0xe1a07002u } },
        { 0x00302d44u, { 0xe92d4df0u, 0xe28db01cu, 0xe24dd048u, 0xe1a07003u } }
    };
    for (unsigned n = 0; n < sizeof(checks) / sizeof(checks[0]); ++n)
        if (memcmp(reinterpret_cast<const void*>(checks[n].address), checks[n].words, sizeof(checks[n].words))) return false;
    if (*reinterpret_cast<const uint32_t*>(0x00227a34u) != 0x02beb645u ||
        strcmp(reinterpret_cast<const char*>(0x02beb645u), "CHmiSceneManager")) return false;
    s_signalId = reinterpret_cast<const uint32_t*>(dlsym(RTLD_DEFAULT,
        "_ZN14GfxIdentifiers36ID_SIGNAL_CLICKAPPLECARPLAYTOUCHHOOKE"));
    s_parameterId = dlsym(RTLD_DEFAULT, "_ZN20CHmiCommandParameter14PARAM_SIGNALIDE");
    s_signalType = dlsym(RTLD_DEFAULT, "_ZTI14CGfxIdentifierIL18EGfxIdentifierType9EE");
    return s_signalId && s_parameterId && s_signalType && *s_signalId;
}

void removeObserver() {
    s_active = false;
    if (!s_registered) return;
    reinterpret_cast<Register>(table(s_registry)[6])(s_registry, &s_commandId, &s_command, -1);
    s_registered = reinterpret_cast<Contains>(0x003011d4u)(s_registry, &s_commandId, &s_command, -1);
    if (s_logger) s_logger(s_registered ?
        "[Home] selection: observer removal pending; callback disabled" :
        "[Home] selection: native shortcut observer removed");
}
}

void updateNativeSelection(void* sceneManager, void* carplay, bool wanted,
    bool probeOnly, bool stopping, LogFn logger, SelectFn selected) {
    s_logger = logger;
    s_carplay = carplay;
    s_selected = selected;
    if (!wanted || probeOnly || stopping || realCarplayAvailable()) {
        removeObserver();
        return;
    }
    if (s_rejected) return;
    if (s_registered) { s_active = true; return; }
    if (!profile()) {
        s_rejected = true;
        if (logger) logger("[Home] selection: command ABI/profile mismatch; observer disabled");
        return;
    }
    // The global plugin scope does not expose CHmiSceneManager (rc 3 on
    // this firmware). Mirror HomeTouchHandling::init: its native getter
    // returns a thread-local dependency cache, whose +8 field is the service.
    // This must run on AppProcUnit, never on the PluginApp polling thread.
    if (!sceneManager) {
        typedef void* (*GetSceneCache)();
        void* cache = reinterpret_cast<GetSceneCache>(0x002278f4u)();
        sceneManager = cache ? *reinterpret_cast<void**>(static_cast<char*>(cache) + 8) : NULL;
    }
    if (!hasTable(sceneManager, "_ZTV16CHmiSceneManager", "16CHmiSceneManager")) {
        waiting("[Home] selection: scene manager not verified; waiting");
        return;
    }
    // Same path used by CHmiHomeTouchHandling::init at 0x339544..0x3395a8.
    void* graphics = *reinterpret_cast<void**>(static_cast<char*>(sceneManager) + 0xf8);
    if (!graphics) { waiting("[Home] selection: graphics manager not ready; waiting"); return; }
    void* registry = static_cast<char*>(graphics) + 0x204;
    const char* holderSymbol = "_ZTV17THmiCommandHolderI14CGfxIdentifierIL18EGfxIdentifierType14EEE";
    void* holderBase = dlsym(RTLD_DEFAULT, holderSymbol);
    if (!holderBase || table(registry) != static_cast<void**>(holderBase) + 2) {
        waiting("[Home] selection: widget command registry not verified; waiting");
        return;
    }
    const uint32_t commandId = *reinterpret_cast<const uint32_t*>(0x03a08b04u);
    if (!commandId) { waiting("[Home] selection: widget command identifier not ready; waiting"); return; }
    if (!s_command) {
        // Native factory returns intrusive_ptr<IHmiCommand> through out.
        // It captures {method pointer, ARM adjustment=0, target} and owns its
        // native boost::function layout. The library is pinned by home_shortcut.
        reinterpret_cast<MakeCommand>(0x0033b03cu)(&s_command, &s_target,
            reinterpret_cast<void*>(&onWidgetSignal), 0);
        if (!s_command) { waiting("[Home] selection: command factory returned empty; waiting"); return; }
    }
    s_registry = registry;
    s_commandId = commandId;
    // Native holder supports several commands under one identifier; -1 is
    // the same context used by the factory home handler, not a priority.
    reinterpret_cast<Register>(table(registry)[2])(registry, &s_commandId, &s_command, -1);
    s_registered = reinterpret_cast<Contains>(0x003011d4u)(registry, &s_commandId, &s_command, -1);
    s_active = s_registered;
    if (!s_registered) { waiting("[Home] selection: registry did not retain observer; waiting"); return; }
    s_waits = 0;
    s_reportedSignal = false;
    if (logger) logger("[Home] selection: native shortcut observer registered");
}
#else
void updateNativeSelection(void*, void*, bool, bool, bool, LogFn, SelectFn) {}
#endif
} }
