// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Opt-in use of the factory CarPlay dashboard widget; no factory connection state is changed.
// Profile derived from the user's HmiMain3D dump; see HOME-SHORTCUT.md.
#include "home_shortcut.hpp"
#include "home_visibility.hpp"
#include "home_selection.hpp"
#include "home_setting.hpp"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <dlfcn.h>
#include <pthread.h>
#include <sys/stat.h>
#include <new>
#ifndef RTLD_DEFAULT
#define RTLD_DEFAULT ((void*)-2)
#endif

namespace nbt { namespace home {
#ifdef __QNX__
extern "C" void nbtAadHomePinAnchor() {}
namespace {
const char* const kProbe = "/dev/shmem/nbt-aad-home-probe";
const char* const kExecutable = "/opt/hmi/ID5/bin/HmiMain3D";
const uintptr_t kCtor = 0x003364b0u;
const uintptr_t kEntryVtable = 0x02c10a58u;
const uintptr_t kApplicationVtableSymbol = 0x03939ce8u;
union Storage { void* align; unsigned char bytes[32]; };

typedef void* (*GetFn)(void*);
typedef int (*ResolveFn)(void*, const void*, void**, unsigned);
typedef void (*StringCtor)(void*, const char*);
typedef void (*StringDtor)(void*);
typedef void (*MutexFn)(void*);
typedef void* (*TreeNext)(void*);
typedef int (*SetTimer)(void*, void*, void*, int, unsigned, unsigned, unsigned);

pthread_mutex_t s_mutex = PTHREAD_MUTEX_INITIALIZER;
LogFn s_log = NULL;
bool s_connected = false, s_selected = false, s_stopping = false;
bool s_scheduled = false, s_rejected = false;
void* s_unit = NULL;
int s_timerTarget = 0;
unsigned s_dispatcherAttempts = 0;
void** s_managerGlobal = NULL;
GetFn s_getResolver = NULL;
ResolveFn s_resolve = NULL;
StringCtor s_capuCtor = NULL;
StringDtor s_capuDtor = NULL;
void* s_dispatcher = NULL;
void* s_variantServer = NULL;
void* s_carplayService = NULL;
void* s_uiFactory = NULL;
void* s_sceneManager = NULL;
MutexFn s_take = NULL, s_give = NULL;
TreeNext s_next = NULL;

void log(const char* message) { if (s_log) s_log(message); }
void* word(const void* p, size_t offset) {
    return *reinterpret_cast<void* const*>(static_cast<const char*>(p) + offset);
}
void** vtable(void* p) { return *reinterpret_cast<void***>(p); }
const char* typeName(void* p) {
    if (!p || !vtable(p) || !vtable(p)[-1]) return "";
    const char* name = static_cast<const char*>(word(vtable(p)[-1], sizeof(void*)));
    return name ? name : "";
}
bool resolveService(void* scope, const char* name, void*& result) {
    if (result) return true;
    Storage key;
    memset(&key, 0, sizeof(key));
    s_capuCtor(&key, name);
    void* out = NULL;
    const int rc = s_resolve(scope, &key, &out, 0);
    s_capuDtor(&key);
    char message[192];
    if (rc == 0 && out) {
        // The process-global scope retains the proxy. Keep its service for
        // the same HMI lifetime as the menu entry and the pinned library.
        result = out;
        snprintf(message, sizeof(message), "[Home] resolve(%s) -> rc %d, %p", name, rc, out);
        log(message);
        return true;
    }
    if (s_dispatcherAttempts == 0 || s_dispatcherAttempts == 29) {
        snprintf(message, sizeof(message), "[Home] resolve(%s) -> rc %d, %p; waiting", name, rc, out);
        log(message);
    }
    return false;
}
bool resolveServices() {
    void* manager = *s_managerGlobal;
    if (!manager) {
        if (!s_dispatcherAttempts) log("[Home] global plugin manager unavailable; waiting");
        return false;
    }
    if (strcmp(typeName(manager), "N9hmiplugin17CHmiPluginManagerE")) {
        log("[Home] unexpected global plugin manager type; shortcut disabled");
        s_rejected = true;
        return false;
    }
    // getResolver() returns const boost::shared_ptr<PluginProxyScope>&,
    // NOT IDependencyResolver*. Its first word is the complete scope pointer.
    void* shared = s_getResolver(manager);
    void* scope = shared ? word(shared, 0) : NULL;
    if (!scope) {
        if (!s_dispatcherAttempts) log("[Home] global dependency scope unavailable; waiting");
        return false;
    }
    if (strcmp(typeName(scope), "N15PluginFramework16PluginProxyScopeE")) {
        log("[Home] unexpected dependency scope type; shortcut disabled");
        s_rejected = true;
        return false;
    }
    // Use the exported method on the complete scope. The resolver interface
    // is a secondary base at +4; calling its slots on the complete object
    // would select the wrong functions. Do not use the thread-local cache.
    const bool dispatcher = resolveService(scope, "CHmiMainDispatcher", s_dispatcher);
    // Resolve before publishing the HMI timer; it may later be put in probe
    // mode without racing updates of these pointers from another thread.
    resolveService(scope, "IHmiVariantServer", s_variantServer);
    resolveService(scope, "IHmiCarPlayService", s_carplayService);
    resolveService(scope, "IHmiUIFactory", s_uiFactory);
    // CHmiSceneManager is obtained by home_selection through the native
    // thread-local getter on AppProcUnit; the global scope cannot resolve it.
    return dispatcher;
}

bool profileMatches() {
    struct stat st;
    if (stat(kExecutable, &st) != 0 || st.st_size != 59792948) return false;
    if (stat("/opt/hmi/ID5/lib/libhmicom.so", &st) != 0 || st.st_size != 158868) return false;
    if (stat("/opt/hmi/ID5/lib/libhmidb.so", &st) != 0 || st.st_size != 1378852) return false;
    if (stat("/opt/hmi/ID5/lib/libhmiplugin.so", &st) != 0 || st.st_size != 199520) return false;
    if (reinterpret_cast<uintptr_t>(dlsym(RTLD_DEFAULT, "_ZTV23CHmiApplicationAbstract")) != kApplicationVtableSymbol)
        return false;
    const uint32_t expectedCtor[] = { 0xe92d4870u, 0xe28db010u, 0xe24dd014u, 0xe1a05002u };
    const uint32_t expectedVtable[] = { 0x00336238u, 0x003361d8u, 0x00335f5cu, 0x00335f08u };
    FILE* f = fopen(kExecutable, "rb");
    if (!f) return false;
    uint32_t data[4];
    bool ok = fseek(f, (long)(kCtor - 0x100000u), SEEK_SET) == 0 &&
        fread(data, sizeof(data), 1, f) == 1 && !memcmp(data, expectedCtor, sizeof(data));
    if (ok) ok = fseek(f, (long)(kEntryVtable - 0x100000u), SEEK_SET) == 0 &&
        fread(data, sizeof(data), 1, f) == 1 && !memcmp(data, expectedVtable, sizeof(data));
    fclose(f);
    if (!ok) return false;
    return !memcmp(reinterpret_cast<const void*>(kCtor), expectedCtor, sizeof(expectedCtor)) &&
        !memcmp(reinterpret_cast<const void*>(kEntryVtable), expectedVtable, sizeof(expectedVtable));
}

bool symbols() {
    s_managerGlobal = reinterpret_cast<void**>(dlsym(RTLD_DEFAULT,
        "_ZN9hmiplugin17CHmiPluginManager19s_pHmiPluginManagerE"));
    s_getResolver = reinterpret_cast<GetFn>(dlsym(RTLD_DEFAULT,
        "_ZN9hmiplugin17CHmiPluginManager11getResolverEv"));
    s_resolve = reinterpret_cast<ResolveFn>(dlsym(RTLD_DEFAULT,
        "_ZN15PluginFramework16PluginProxyScope33requestPluginInstanceSyncInternalERKN4capu6StringEPPvj"));
    s_capuCtor = reinterpret_cast<StringCtor>(dlsym(RTLD_DEFAULT, "_ZN4capu6StringC1EPKc"));
    s_capuDtor = reinterpret_cast<StringDtor>(dlsym(RTLD_DEFAULT, "_ZN4capu6StringD1Ev"));
    s_take = reinterpret_cast<MutexFn>(dlsym(RTLD_DEFAULT, "_ZN8COSMutex4takeEv"));
    s_give = reinterpret_cast<MutexFn>(dlsym(RTLD_DEFAULT, "_ZN8COSMutex4giveEv"));
    s_next = reinterpret_cast<TreeNext>(dlsym(RTLD_DEFAULT, "_ZSt18_Rb_tree_incrementPSt18_Rb_tree_node_base"));
    return s_managerGlobal && s_getResolver && s_resolve && s_capuCtor && s_capuDtor &&
        s_take && s_give && s_next;
}

void* appUnit(void* dispatcher) {
    if (!dispatcher) return NULL;
    void* result = NULL;
    char* d = static_cast<char*>(dispatcher);
    s_take(d + 0x18);
    void* header = d + 4;
    unsigned count = 0;
    for (void* node = word(d, 0xc); node && node != header && !result && count++ < 256; node = s_next(node)) {
        void** it = static_cast<void**>(word(node, 0x14));
        void** end = static_cast<void**>(word(node, 0x18));
        for (unsigned n = 0; it && it != end && n++ < 256; ++it)
            if (*it && !strcmp(typeName(*it), "15CHmiAppProcUnit")) { result = *it; break; }
    }
    s_give(d + 0x18);
    return result;
}

int selected(void*, int) {
    pthread_mutex_lock(&s_mutex);
    if (!s_stopping && s_connected && shortcutEnabled()) s_selected = true;
    pthread_mutex_unlock(&s_mutex);
    return 0;
}

void onTimer(void*, unsigned, unsigned, unsigned);
void nativeShortcutSelected() { selected(NULL, 0); }
int arm() {
    void* timer = reinterpret_cast<GetFn>(vtable(s_unit)[3])(s_unit);
    if (!timer) return 0;
    return reinterpret_cast<SetTimer>(vtable(timer)[1])(timer, &s_timerTarget,
        reinterpret_cast<void*>(&onTimer), 0, 1000, 0, 0);
}
void onTimer(void*, unsigned, unsigned, unsigned) {
    pthread_mutex_lock(&s_mutex);
    const bool stopping = s_stopping;
    const bool probeOnly = access(kProbe, F_OK) == 0;
    const bool want = !stopping && !probeOnly && s_connected && shortcutEnabled();
    pthread_mutex_unlock(&s_mutex);
    const bool nativeVisibility = updateNativeVisibility(s_uiFactory, s_variantServer,
        s_carplayService, want, probeOnly, stopping, s_log);
    updateNativeSelection(s_sceneManager, s_carplayService, nativeVisibility && want,
        probeOnly, stopping, s_log, &nativeShortcutSelected);
    if (stopping) {
        return;
    }
    if (!arm()) log("[Home] timer re-arm failed");
}
} // namespace

void start(LogFn logger) {
    if (!s_log) s_log = logger;
    pthread_mutex_lock(&s_mutex);
    s_stopping = false;
    pthread_mutex_unlock(&s_mutex);
}
bool poll(bool connected) {
    pthread_mutex_lock(&s_mutex);
    s_connected = connected;
    const bool probeOnly = access(kProbe, F_OK) == 0;
    const bool open = s_selected && connected && !s_stopping && !probeOnly && shortcutEnabled();
    s_selected = false;
    pthread_mutex_unlock(&s_mutex);
    if (!s_scheduled && !s_rejected && (probeOnly || (connected && shortcutEnabled()))) {
        if (!s_dispatcherAttempts) log(probeOnly ? "[Home] read-only native comparison requested; AA connection not required" :
            "[Home] home shortcut setting enabled while connected; checking native shortcut prerequisites");
        if (!profileMatches() || !symbols()) {
            log("[Home] firmware profile or required symbols do not match; shortcut disabled");
            s_rejected = true;
            return open;
        }
        const bool servicesReady = resolveServices();
        if (s_rejected) return open;
        s_unit = servicesReady ? appUnit(s_dispatcher) : NULL;
        if (!s_unit) {
            if (s_dispatcherAttempts == 0 || s_dispatcherAttempts == 29)
                log(servicesReady ? "[Home] services resolved, but CHmiAppProcUnit not found; waiting" :
                    "[Home] native dependency services unavailable; waiting");
            if (++s_dispatcherAttempts >= 30) {
                log("[Home] native dispatch unit unavailable after 30 polls; shortcut disabled until HMI restart");
                s_rejected = true;
            }
            return open;
        }
        log("[Home] native dispatch unit found; scheduling shortcut registration");
        // Native services retain callbacks for HMI lifetime. Pin this library
        // so a queued timer/entry callback cannot point into unloaded code.
        void* pin = dlopen("/opt/hmi/ID5/data/ro/Plugins/NbtAad/libNbtAad.so", RTLD_NOW);
        if (!pin) { log("[Home] cannot pin plugin library; shortcut disabled"); s_rejected = true; return open; }
        if (dlsym(pin, "nbtAadHomePinAnchor") != reinterpret_cast<void*>(&nbtAadHomePinAnchor)) {
            dlclose(pin);
            s_rejected = true;
            log("[Home] installed plugin differs from the loaded module; reboot before enabling the shortcut");
            return open;
        }
        if (arm()) { s_scheduled = true; log("[Home] native HMI-thread polling started"); }
        else { dlclose(pin); s_rejected = true; log("[Home] cannot arm HMI timer; shortcut disabled"); }
    }
    return open;
}
void stop() {
    pthread_mutex_lock(&s_mutex);
    s_stopping = true;
    s_connected = s_selected = false;
    pthread_mutex_unlock(&s_mutex);
}
#else
void start(LogFn) {}
bool poll(bool) { return false; }
void stop() {}
#endif
} }
