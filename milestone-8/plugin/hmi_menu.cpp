// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Communication menu entry through iDrive's own menu services (see plugin_abi.hpp). Runs on the HMI thread.
#include "hmi_menu.hpp"
#include <dlfcn.h>
#include <string.h>
#include <new>

void nbtPluginLog(const char* fmt, ...);   // defined in nbt_aad_plugin.cpp

namespace nbt {
namespace hmi {

namespace {

// HmiMain3D functions that are not exported (this firmware build; checked by checkMenuFirmware()).
const uint32_t kRegistryGetter = 0x00300084;  // returns the CHmiMenuProviderRegistry instance cache
const uint32_t kEntryCtor      = 0x0251a3a4;  // CHmiGenericMenuEntry constructor, object 0x94 bytes
const uint32_t kEntrySize      = 0x94;
const int      kEntrySlots     = 61;
const int      kExecuteSlot    = 2;

struct CodeCheck { uint32_t addr, w0, w1; const char* what; };
const CodeCheck kChecks[] = {
    { kRegistryGetter, 0xe92d4df0, 0xe28db01c, "menu registry getter" },
    { kEntryCtor,      0xe92d48f0, 0xe28db014, "CHmiGenericMenuEntry ctor" },
};

typedef void* (*CacheLookupFn)(const char* id);
typedef void* (*CacheGetFn)(void* cache);
typedef void* (*RegistryGetterFn)();
typedef void (*StrCtorFn)(void* s, const char* c);
typedef void (*StrDtorFn)(void* s);
typedef bool (*StrLessFn)(const void* lessObj, const void* a, const void* b);
typedef int (*AddEntryFn)(void* provider, void** entryPtr);
typedef void (*EntryCtorFn)(void* self, void* targetState, int focusId, int order, const uint32_t* text,
                            const uint32_t* text2, const uint32_t* icon, int zero1, const uint32_t* res1,
                            int zero2, const uint32_t* res2);

bool s_checked = false;
bool s_added = false;
SelectFn s_onSelect = 0;
uint32_t s_vtbl[2 + kEntrySlots];          // our subclass vtable: [offset-to-top, typeinfo, slots...]

inline uint32_t rd32(uintptr_t a) { return *reinterpret_cast<volatile uint32_t*>(a); }
inline void* word(const void* base, size_t off) {
    return *reinterpret_cast<void* const*>(reinterpret_cast<const char*>(base) + off);
}

const char* rttiName(const void* obj) {
    const void* vtbl = word(obj, 0);
    const void* ti = *(reinterpret_cast<void* const*>(vtbl) - 1);
    return reinterpret_cast<const char*>(word(ti, 4));
}

// Our override of CHmiGenericMenuEntry::execute.
int entryExecute(void* self, int arg) {
    nbtPluginLog("Android Auto entry selected (entry %p, arg %d)", self, arg);
    if (s_onSelect) s_onSelect(self);
    return 0;
}

void* menuRegistry() {
    CacheLookupFn lookup = reinterpret_cast<CacheLookupFn>(
        dlsym(RTLD_DEFAULT, "_ZN9hmiplugin28getInstanceCacheByIdentifierEPKc"));
    if (lookup) {
        void* cache = lookup("CHmiMenuProviderRegistry");
        if (cache) {
            CacheGetFn get = reinterpret_cast<CacheGetFn>(reinterpret_cast<void* const*>(word(cache, 0))[2]);
            void* holder = get(cache);
            if (holder && word(holder, 8)) return word(holder, 8);
        }
    }
    void* holder = reinterpret_cast<RegistryGetterFn>(static_cast<uintptr_t>(kRegistryGetter))();
    return holder ? word(holder, 8) : 0;
}

// CHmiMenuProviderRegistry +4: std::map<THmiString<ushort>, intrusive_ptr<provider>> (key +0x10, value +0x14).
void* findProvider(void* registry, const void* name, StrLessFn less) {
    char* r = reinterpret_cast<char*>(registry);
    void* header = r + 8;
    void* best = header;
    for (void* n = word(r, 0xc); n; ) {
        if (!less(r + 4, reinterpret_cast<char*>(n) + 0x10, name)) { best = n; n = word(n, 8); }
        else n = word(n, 0xc);
    }
    if (best == header || less(r + 4, name, reinterpret_cast<char*>(best) + 0x10)) return 0;
    return word(best, 0x14);
}

} // namespace

bool checkMenuFirmware() {
    for (size_t i = 0; i < sizeof(kChecks) / sizeof(kChecks[0]); ++i) {
        const CodeCheck& c = kChecks[i];
        if (rd32(c.addr) != c.w0 || rd32(c.addr + 4) != c.w1) {
            nbtPluginLog("firmware check FAILED: %s @0x%08x", c.what, c.addr);
            return false;
        }
    }
    s_checked = true;
    nbtPluginLog("firmware check OK");
    return true;
}

int addMenuEntry(const MenuEntryCfg& cfg, SelectFn onSelect) {
    if (s_added) return 1;
    if (!s_checked) return -1;
    StrCtorFn strCtor = reinterpret_cast<StrCtorFn>(dlsym(RTLD_DEFAULT, "_ZN10THmiStringItEC1ItvEEPKc"));
    StrDtorFn strDtor = reinterpret_cast<StrDtorFn>(dlsym(RTLD_DEFAULT, "_ZN10THmiStringItED1Ev"));
    StrLessFn less = reinterpret_cast<StrLessFn>(dlsym(RTLD_DEFAULT, "_ZNKSt4lessI10THmiStringItEEclERKS1_S4_"));
    if (!strCtor || !strDtor || !less) { nbtPluginLog("THmiString symbols missing"); return -1; }

    void* registry = menuRegistry();
    if (!registry) return 0;

    char name[16];
    memset(name, 0, sizeof(name));
    strCtor(name, cfg.menuName);
    void* provider = findProvider(registry, name, less);
    strDtor(name);
    if (!provider) return 0;                          // Communication has not built its menu yet

    char state[16];
    memset(state, 0, sizeof(state));
    strCtor(state, cfg.fallbackState);
    uint32_t text = cfg.text, text2 = cfg.text, icon = cfg.icon, res1 = 0x20000001u, res2 = 0x20000001u;
    void* entry = ::operator new(kEntrySize);
    memset(entry, 0, kEntrySize);
    reinterpret_cast<EntryCtorFn>(static_cast<uintptr_t>(kEntryCtor))(
        entry, state, (int)cfg.focus, (int)cfg.order, &text, &text2, &icon, 0, &res1, 0, &res2);
    strDtor(state);

    if (strcmp(rttiName(entry), "20CHmiGenericMenuEntry") != 0) {
        nbtPluginLog("new entry is '%s', not CHmiGenericMenuEntry - not added", rttiName(entry));
        return -1;
    }
    // Subclass: copy the class's vtable (from the object itself) and override execute.
    const uint32_t* base = reinterpret_cast<const uint32_t*>(word(entry, 0)) - 2;
    memcpy(s_vtbl, base, sizeof(s_vtbl));
    s_vtbl[2 + kExecuteSlot] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&entryExecute));
    *reinterpret_cast<uint32_t*>(entry) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&s_vtbl[2]));
    s_onSelect = onSelect;

    uint32_t* refs = reinterpret_cast<uint32_t*>(reinterpret_cast<char*>(entry) + 4);
    *refs += 2;                                       // one kept by us for the HMI lifetime, one for 'tmp'
    void* tmp = entry;
    AddEntryFn add = reinterpret_cast<AddEntryFn>(reinterpret_cast<void* const*>(word(provider, 0))[2]);
    int rc = add(provider, &tmp);
    *refs -= 1;
    s_added = true;
    nbtPluginLog("Android Auto entry %p added to %s (provider %p, focus 0x%x, order 0x%x) -> %d",
                 entry, cfg.menuName, provider, cfg.focus, cfg.order, rc);
    return 1;
}

} // namespace hmi
} // namespace nbt
