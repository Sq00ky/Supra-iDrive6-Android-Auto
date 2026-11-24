// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef NBT_NATIVE_MEDIA_HOOK_HPP
#define NBT_NATIVE_MEDIA_HOOK_HPP

#include "../src/audio/native_media_record.hpp"
#include <stdint.h>
#include <string.h>

namespace nbt { namespace native_media {
typedef void (*LogFn)(const char*, ...);

struct RowShape { uint16_t id; uint32_t type, format2, format1, symbol; };
inline bool noAudioShape(uint32_t window, uint32_t audioMode, unsigned count, const RowShape* rows) {
    // Build66: No Audio (C495), or Bluetooth unavailable (C491) while Browser Audio plays.
    // Generic media/radio notices can share the row shape and must retain their native text.
    if ((window != 0xc495 && window != 0xc491) || count != 2 || !rows) return false;
    if (window == 0xc491 && audioMode != 20) return false;
    for (unsigned i = 0; i != 2; ++i)
        if (rows[i].id != i + 1 || rows[i].type != 2 || rows[i].format2 != 1 ||
            rows[i].format1 != 1 || rows[i].symbol != 0) return false;
    return true;
}
inline bool cloneContentVtable(const uint32_t original[20], uint32_t replacement, uint32_t out[20]) {
    if (!original || !out || !replacement || original[0] != 0 || original[1] != 0x02ec0284 ||
        original[18] != 0x0235b634) return false;
    memcpy(out, original, 20 * sizeof(*out));
    out[18] = replacement; // Address point+0x40; retain RTTI and every other native method.
    return true;
}
inline unsigned restoreMask(bool sameOwner, bool row0StillOurs, bool row1StillOurs) {
    return sameOwner ? (row0StillOurs ? 1u : 0u) | (row1StillOurs ? 2u : 0u) : 0;
}

#ifdef __QNXNTO__
} }
#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
namespace nbt { namespace native_media {
namespace detail {
typedef void* (*GetUnit)();
typedef void (*MutexOp)(void*);
typedef void* (*NextNode)(void*);
typedef void* (*GetTimer)(void*);
typedef int (*SetTimer)(void*, void*, void*, int, unsigned, unsigned, unsigned short);
typedef void (*StringCtor)(void*, const uint16_t*);
typedef void (*StringCopy)(void*, const void*);
typedef void (*StringDtor)(void*);
typedef bool (*StringEq)(const void*, const void*);
typedef bool (*SetText)(void*, unsigned, const void*);
typedef int (*Publish)(void*, unsigned, unsigned, unsigned, unsigned);
typedef bool (*HighUpdate)();
typedef unsigned (*CurrentWindow)(void*);

inline uint32_t word(uintptr_t p) { return *reinterpret_cast<const uint32_t*>(p); }
inline void* pointer(uintptr_t p) { return *reinterpret_cast<void* const*>(p); }
inline uint32_t nowMs() {
    timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}
inline const char* typeName(void* p) {
    if (!p) return "";
    uintptr_t vt = (uintptr_t)pointer((uintptr_t)p);
    uintptr_t ti = (uintptr_t)pointer(vt - 4);
    return (const char*)pointer(ti + 4);
}

struct State {
    pthread_mutex_t mutex;
    NativeMediaSnapshot record;
    LogFn log;
    pthread_t worker;
    void *dispatcher, *unit, *timer, *pin;
    GetUnit getUnit;
    MutexOp take, give;
    NextNode next;
    SetTimer setTimer;
    StringCtor ctor;
    StringCopy copy;
    StringDtor dtor;
    StringEq equal;
    Publish original;
    uint32_t vtable[20];
    void* hookedOwner;
    volatile int stopping, stopped, pending, generation, installed, statusNote;
    bool installFailed, ownsRow[2];
    bool running, saved;
    void *savedOwner, *savedRows;
    uint32_t originalText[2], appliedText[2];
    State() : log(0), dispatcher(0), unit(0), timer(0), pin(0), getUnit(0), take(0), give(0),
        next(0), setTimer(0), ctor(0), copy(0), dtor(0), equal(0), original(0), hookedOwner(0),
        stopping(0), stopped(0), pending(0), generation(0), installed(0), statusNote(0), installFailed(false),
        running(false), saved(false),
        savedOwner(0), savedRows(0) { pthread_mutex_init(&mutex, 0); }
};
static State state;
inline void log(const char* s) { if (state.log) state.log("[NativeMedia] %s", s); }

inline bool build66() {
    // Export addresses plus independent code/vtable anchors pin the complete adapter ABI.
    state.ctor = (StringCtor)dlsym(RTLD_DEFAULT, "_ZN10THmiStringItEC1EPKt");
    state.copy = (StringCopy)dlsym(RTLD_DEFAULT, "_ZN10THmiStringItEC1ERKS0_");
    state.dtor = (StringDtor)dlsym(RTLD_DEFAULT, "_ZN10THmiStringItED1Ev");
    state.equal = (StringEq)dlsym(RTLD_DEFAULT, "_ZeqItEbRK10THmiStringIT_ES4_");
    if ((uintptr_t)state.ctor != 0x006cee70 || (uintptr_t)state.copy != 0x0022715c ||
        (uintptr_t)state.dtor != 0x00226e90 || (uintptr_t)state.equal != 0x002604fc) return false;
    return word(0x02941dec) == 0xe92d4800 && word(0x02941df0) == 0xe28db004 &&
        word(0x02941e0c) == 0xe59cc040 && word(0x02ec01b8) == 0x0235b634 &&
        word(0x0235294c) == 0x038302c0 && word(0x023528dc) == 0xeb17bd42 &&
        word(0x0235a5a4) == 0xe3a03018 && word(0x0235a5b8) == 0xe2800010 &&
        word(0x00f85b2c) == 0xe584581c && word(0x00f830e0) == 0xe58543e8 &&
        word(0x00f83340) == 0xe30c1352 && word(0x00f88550) == 0x02c8e9d0 &&
        word(0x00f85adc) == 0xe30c1491 && word(0x00f85afc) == 0xe58457a8;
}

inline void clearSaved() {
    if (!state.saved) return;
    for (unsigned i = 0; i != 2; ++i) { state.dtor(&state.originalText[i]); state.dtor(&state.appliedText[i]); }
    state.saved = false;
}

inline void* context(void* content = 0) {
    void* owner = pointer(0x038302c0 + 0xd4);
    if (!owner || word((uintptr_t)owner) != 0x02c98cd0) return 0;
    uintptr_t p = (uintptr_t)owner;
    const uint32_t contentVtable = state.installed && owner == state.hookedOwner ?
        (uint32_t)(uintptr_t)(state.vtable + 2) : 0x02ec0178;
    if (word(p + 0x2ec) != 0x02ec0068 || word(p + 0x33c) != contentVtable ||
        word(p + 0x374) != 0x02ec0028 || (content && content != (void*)(p + 0x33c))) return 0;
    uintptr_t rows = (uintptr_t)pointer(p + 0x33c + 0x30);
    if (!rows || *(const unsigned char*)(p + 0x33c + 0x2c) != 2) return 0;
    RowShape shape[2];
    for (unsigned i = 0; i != 2; ++i) {
        uintptr_t r = rows + i * 24;
        shape[i].id = *(const uint16_t*)r; shape[i].type = word(r + 4);
        shape[i].format2 = word(r + 8); shape[i].format1 = word(r + 12); shape[i].symbol = word(r + 20);
    }
    // Info's presentation fields differ from PBI defaults; use the native current-window selector.
    void* app = state.unit ? pointer((uintptr_t)state.unit + 0x3e8) : 0;
    if (!app || word((uintptr_t)app) != 0x02c8e9d0 || !pointer((uintptr_t)app + 0x18) ||
        word((uintptr_t)app + 0x69c) != 0x02c8f858) return 0;
    return noAudioShape(((CurrentWindow)0x00f83334)(app), word((uintptr_t)app + 0x6d8), 2, shape) ? owner : 0;
}
inline bool sameContext(void* owner, void* rows) {
    return state.saved && owner == state.savedOwner && rows == state.savedRows;
}
inline bool isApplied(unsigned i, void* owner, void* rows) {
    return sameContext(owner, rows) && state.ownsRow[i] &&
        state.equal((char*)rows + i * 24 + 16, &state.appliedText[i]);
}
inline bool setTexts(void* owner, const NativeMediaSnapshot* record) {
    void* content = (char*)owner + 0x33c;
    void* rows = pointer((uintptr_t)content + 0x30);
    if (!record) {
        bool changed = false;
        const unsigned restore = restoreMask(sameContext(owner, rows),
            isApplied(0, owner, rows), isApplied(1, owner, rows));
        for (unsigned i = 0; i != 2; ++i)
            if (restore & (1u << i)) {
                changed |= !state.equal((char*)rows + i * 24 + 16, &state.originalText[i]);
                ((SetText)0x0235a558)(content, i, &state.originalText[i]);
            }
        clearSaved();
        return changed;
    }
    if (!sameContext(owner, rows)) {
        clearSaved();
        for (unsigned i = 0; i != 2; ++i) {
            state.copy(&state.originalText[i], (char*)rows + i * 24 + 16);
            state.copy(&state.appliedText[i], (char*)rows + i * 24 + 16);
            state.ownsRow[i] = false;
        }
        state.saved = true; state.savedOwner = owner; state.savedRows = rows;
    }
    bool changed = false;
    for (unsigned i = 0; i != 2; ++i) {
        // The factory can change one label while the other is still ours.
        if (!isApplied(i, owner, rows)) {
            state.dtor(&state.originalText[i]);
            state.copy(&state.originalText[i], (char*)rows + i * 24 + 16);
        }
        uint16_t text[26]; nativeMediaText(i ? record->artist : record->song, text);
        uint32_t nativeText = 0; state.ctor(&nativeText, text);
        if (!state.equal((char*)rows + i * 24 + 16, &nativeText)) {
            ((SetText)0x0235a558)(content, i, &nativeText); changed = true;
        }
        state.dtor(&state.appliedText[i]); state.copy(&state.appliedText[i], &nativeText);
        state.ownsRow[i] = true;
        state.dtor(&nativeText);
    }
    return changed;
}
inline bool snapshot(NativeMediaSnapshot& out) {
    if (state.stopping) return true;
    if (pthread_mutex_trylock(&state.mutex)) return false;
    out = state.record;
    pthread_mutex_unlock(&state.mutex);
    // Read the clock after copying; a newer heartbeat must not look like unsigned-clock wrap.
    out.active = out.active && (uint32_t)(nowMs() - out.updatedMs) < kMediaStaleMs;
    return true;
}

static void onTimer(void*, unsigned, unsigned, unsigned short);
inline void wake() {
    if (!state.timer || !__sync_bool_compare_and_swap(&state.pending, 0, 1)) return;
    if (state.setTimer(state.timer, &state, (void*)&onTimer, 0, 10, 0, 0) <= 0)
        __sync_lock_test_and_set(&state.pending, 0);
}
static int publishHook(void* content, unsigned first, unsigned count, unsigned mode, unsigned destination) {
    if (state.getUnit() == state.unit && first == 0 && count == 0 && mode == 1 && destination == 0xffff) {
        void* owner = context(content);
        NativeMediaSnapshot record;
        if (!snapshot(record)) wake();
        else if (owner && record.active && !state.stopping) {
            if (*((const unsigned char*)content + 0x1a)) setTexts(owner, &record);
            else wake();  // A new text needs the owner's generation transaction, not a same-generation C02.
        } else if (state.saved) wake();
    }
    return state.original(content, first, count, mode, destination);
}
static void onTimer(void*, unsigned, unsigned, unsigned short) {
    const int generation = state.generation;
    if (state.getUnit() != state.unit) { __sync_lock_test_and_set(&state.pending, 0); return; }
    if (state.installed && pointer(0x038302c0 + 0xd4) != state.hookedOwner) {
        clearSaved(); __sync_lock_test_and_set(&state.installed, 0);
    }
    if (!state.installed && !state.stopping && !state.installFailed) {
        void* owner = pointer(0x038302c0 + 0xd4);
        if (!owner) goto done;
        state.installFailed = true;
        uintptr_t p = (uintptr_t)owner;
        if (word(p) != 0x02c98cd0 || word(p + 0x2ec) != 0x02ec0068 ||
            word(p + 0x33c) != 0x02ec0178 || word(p + 0x374) != 0x02ec0028 ||
            !cloneContentVtable((const uint32_t*)0x02ec0170,
                               (uint32_t)(uintptr_t)&publishHook, state.vtable)) goto done;
        state.original = (Publish)0x0235b634; state.hookedOwner = owner;
        __sync_synchronize();
        *(volatile uint32_t*)(p + 0x33c) = (uint32_t)(uintptr_t)(state.vtable + 2);
        state.installFailed = false;
        __sync_lock_test_and_set(&state.installed, 1);
        __sync_lock_test_and_set(&state.statusNote, 1);
    }
    if (state.installed) {
        NativeMediaSnapshot record;
        if (!snapshot(record)) { __sync_lock_test_and_set(&state.pending, 0); wake(); return; }
        void* owner = context();
        if (owner && setTexts(owner, record.active && !state.stopping ? &record : 0))
            ((HighUpdate)0x0235206c)(); // Native counter transaction; hook decorates only its dirty C02.
        else if (!owner) clearSaved();
        if (state.stopping) {
            uintptr_t p = (uintptr_t)state.hookedOwner;
            if (pointer(0x038302c0 + 0xd4) == state.hookedOwner &&
                word(p + 0x33c) == (uint32_t)(uintptr_t)(state.vtable + 2)) {
                *(volatile uint32_t*)(p + 0x33c) = 0x02ec0178;
                __sync_synchronize();
                __sync_lock_test_and_set(&state.installed, 0); clearSaved();
            }
        }
    }
done:
    if (state.stopping && !state.installed) __sync_lock_test_and_set(&state.stopped, 1);
    __sync_lock_test_and_set(&state.pending, 0);
    if (state.generation != generation || (state.stopping && !state.stopped)) wake();
}

inline void* findIC() {
    void* found = 0;
    state.take((char*)state.dispatcher + 0x18);
    void* header = (char*)state.dispatcher + 4;
    void* node = pointer((uintptr_t)state.dispatcher + 0xc);
    for (unsigned n = 0; node && node != header && n < 256 && !found; ++n, node = state.next(node)) {
        void** it = (void**)pointer((uintptr_t)node + 0x14);
        void** end = (void**)pointer((uintptr_t)node + 0x18);
        if (!it || end < it || end - it > 64) continue;
        for (; it != end; ++it)
            if (*it && !strcmp(typeName(*it), "14CHmiICProcUnit")) { found = *it; break; }
    }
    state.give((char*)state.dispatcher + 0x18);
    return found;
}
inline NativeMediaSnapshot readRecord() {
    NativeMediaSnapshot out;
    int fd = open("/dev/shmem/nbt-aad-media", O_RDONLY | O_NONBLOCK);
    if (fd < 0) return out;
    char body[kMediaRecordMax + 1], head[40];
    ssize_t n = read(fd, body, sizeof(body));
    if (n > 0 && n <= (ssize_t)kMediaRecordMax && parseNativeMediaRecord(body, (size_t)n, nowMs(), out)) {
        uint32_t pid = 0, sequence = 0;
        if (lseek(fd, 0, SEEK_SET) < 0) out = NativeMediaSnapshot();
        else {
            ssize_t h = read(fd, head, sizeof(head));
            if (h <= 0 || !nativeMediaGeneration(head, (size_t)h, pid, sequence) ||
                pid != out.writerPid || sequence != out.sequence) out = NativeMediaSnapshot();
        }
    }
    close(fd);
    return out;
}
static void* reader(void*) {
    for (unsigned i = 0; i != 300 && !state.stopping && !state.unit; ++i) {
        state.unit = findIC();
        if (!state.unit) usleep(100000);
    }
    if (!state.unit) { log("IC processing unit unavailable"); return 0; }
    state.timer = ((GetTimer)(*(void***)state.unit)[3])(state.unit);
    if (!state.timer) { log("IC timer unavailable"); return 0; }
    wake();
    while (!state.stopping) {
        if (__sync_lock_test_and_set(&state.statusNote, 0)) log("Build66 private owner hook installed on IC thread");
        NativeMediaSnapshot record = readRecord();
        pthread_mutex_lock(&state.mutex);
        bool changed = record.active != state.record.active || strcmp(record.song, state.record.song) ||
                       strcmp(record.artist, state.record.artist);
        state.record = record;
        pthread_mutex_unlock(&state.mutex);
        if (changed) { __sync_add_and_fetch(&state.generation, 1); wake(); }
        else if (!state.installed && !state.installFailed) wake();
        usleep(100000);
    }
    return 0;
}
} // namespace detail

// Kept alive until HMI exit: a queued native timer must never call an unloaded library.
extern "C" __attribute__((visibility("default"))) inline void* nbtNativeMediaLibraryAnchor() { return &detail::state; }
#ifdef NBT_NATIVE_MEDIA_GUEST_TEST
inline void guestStart(void* appUnit);
#endif
inline bool start(LogFn logger) {
    using namespace detail;
    if (state.running) return true;
    if (state.stopping && (!state.stopped || state.pending)) {
        log("IC cleanup still pending: restart deferred"); return false;
    }
    state.log = logger;
    if (!build66()) { log("unsupported HMI: no hook installed"); return false; }
    state.getUnit = (GetUnit)dlsym(RTLD_DEFAULT, "_Z14getHmiProcUnitv");
    state.take = (MutexOp)dlsym(RTLD_DEFAULT, "_ZN8COSMutex4takeEv");
    state.give = (MutexOp)dlsym(RTLD_DEFAULT, "_ZN8COSMutex4giveEv");
    state.next = (NextNode)dlsym(RTLD_DEFAULT, "_ZSt18_Rb_tree_incrementPSt18_Rb_tree_node_base");
    state.setTimer = (SetTimer)dlsym(RTLD_DEFAULT, "_ZN9CHmiTimer9_setTimerEPvM15CDelegateTargetFvjjtEjjt");
    void* ctor = dlsym(RTLD_DEFAULT, "_ZN12CHmiProcUnitC1ER18CHmiMainDispatcherh");
    if (!state.getUnit || !state.take || !state.give || !state.next || !state.setTimer || !ctor ||
        word((uintptr_t)ctor + 0xa4) != 0xe58432e8) return false;
    void* current = state.getUnit();
    if (!current || strcmp(typeName(current), "15CHmiAppProcUnit")) return false;
    state.dispatcher = pointer((uintptr_t)current + 0x2e8);
    if (!state.dispatcher) return false;
    const char* paths[] = { "/opt/hmi/data/ro/Plugins/NbtAad/libNbtAad.so",
        "/opt/hmi/ID5/data/ro/Plugins/NbtAad/libNbtAad.so", "/var/opt/hmi/data/ID5/Plugins/NbtAad/libNbtAad.so" };
    for (unsigned i = 0; !state.pin && i != 3; ++i) {
        void* handle = dlopen(paths[i], RTLD_NOW | RTLD_LOCAL);
        if (handle && dlsym(handle, "nbtNativeMediaLibraryAnchor") == (void*)&nbtNativeMediaLibraryAnchor) state.pin = handle;
        else if (handle) dlclose(handle);
    }
    if (!state.pin) { log("cannot retain native callback library"); return false; }
    pthread_mutex_lock(&state.mutex);
    state.record = NativeMediaSnapshot();
    pthread_mutex_unlock(&state.mutex);
    state.unit = state.timer = 0;
    state.stopping = state.stopped = 0; state.installFailed = false;
    if (pthread_create(&state.worker, 0, &reader, 0)) return false;
    state.running = true;
#ifdef NBT_NATIVE_MEDIA_GUEST_TEST
    guestStart(current);
#endif
    return true;
}
inline void stop() {
    using namespace detail;
    if (!state.running) return;
    __sync_lock_test_and_set(&state.stopping, 1);
    pthread_join(state.worker, 0);
    if (!state.installed && !state.pending) {
        __sync_lock_test_and_set(&state.stopped, 1); state.running = false; return;
    }
    __sync_add_and_fetch(&state.generation, 1); wake();
    for (unsigned i = 0; i != 150 && !state.stopped; ++i) usleep(10000);
    if (!state.stopped) log("IC cleanup pending; library retained for queued callback");
    state.running = false;
}
#else
inline bool start(LogFn) { return false; }
inline void stop() {}
#endif
} }
#if defined(__QNXNTO__) && defined(NBT_NATIVE_MEDIA_GUEST_TEST)
#include "../tests/native_media_guest_control.hpp"
#endif
#endif
