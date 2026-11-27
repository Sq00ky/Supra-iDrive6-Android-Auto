// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef NBT_NATIVE_MEDIA_HOOK_HPP
#define NBT_NATIVE_MEDIA_HOOK_HPP

#include "../src/audio/native_media_record.hpp"
#include "native_media_profiles.hpp"
#include <stdint.h>
#include <string.h>

namespace nbt { namespace native_media {
typedef void (*LogFn)(const char*, ...);

struct RowShape { uint16_t id; uint32_t type, format2, format1, symbol; };
inline bool noAudioShape(uint32_t window, uint32_t audioMode, unsigned count, const RowShape* rows, const Profile& p) {
    // Build66: No Audio (C495), or Bluetooth unavailable (C491) while Browser Audio plays.
    // Generic media/radio notices can share the row shape and must retain their native text.
    if ((window != p.noAudioWindow && window != p.btUnavailableWindow) || count != 2 || !rows) return false;
    if (window == p.btUnavailableWindow && audioMode != p.browserAudioMode) return false;
    for (unsigned i = 0; i != 2; ++i)
        if (rows[i].id != i + 1 || rows[i].type != 2 || rows[i].format2 != 1 ||
            rows[i].format1 != 1 || rows[i].symbol != 0) return false;
    return true;
}
inline bool cloneContentVtable(const uint32_t original[20], uint32_t replacement, uint32_t out[20], const Profile& p) {
    if (!original || !out || !replacement || original[0] != 0 || original[1] != p.address.contentType ||
        original[18] != p.address.publishContent) return false;
    memcpy(out, original, 20 * sizeof(*out));
    out[18] = replacement; // Address point+0x40; retain RTTI and every other native method.
    return true;
}
inline bool cloneInfoVtable(const uint32_t original[16], uint32_t replacement, uint32_t out[16], const Profile& p) {
    if (!original || !out || !replacement || original[0] != 0 || original[1] != p.address.infoType ||
        original[14] != p.address.publishInfo) return false;
    memcpy(out, original, 16 * sizeof(*out));
    out[14] = replacement;
    return true;
}
inline bool clonePositionVtable(const uint32_t original[16], uint32_t replacement, uint32_t out[16], const Profile& p) {
    if (!original || !out || !replacement || original[0] != 0 || original[1] != p.address.positionType ||
        original[14] != p.address.publishPosition || original[8] != p.address.receivePosition) return false;
    memcpy(out, original, 16 * sizeof(*out));
    out[14] = replacement; // Retain the native receiver, including its unsupported public Get response.
    return true;
}
inline bool sameSubscribers(const uint16_t* a, size_t count, const uint16_t* b, size_t otherCount) {
    return a && b && count && count <= 64 && count == otherCount &&
        !memcmp(a, b, count * sizeof(*a));
}
inline unsigned restoreMask(bool sameOwner, bool row0StillOurs, bool row1StillOurs) {
    return sameOwner ? (row0StillOurs ? 1u : 0u) | (row1StillOurs ? 2u : 0u) : 0;
}

#ifdef __QNXNTO__
} }
#include <dlfcn.h>
#include "native_media_compatibility.hpp"
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
typedef int (*PublishInfo)(void*, unsigned, unsigned);
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
    const Profile* profile;
    void* appUnit;
    volatile int checkRequested;
    unsigned compatibilityRevision;
    char compatibilitySummary[192];
    compatibility::Identity identity;
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
    uint32_t infoVtable[16];
    uint32_t positionVtable[16];
    void* hookedOwner;
    NativeMediaSnapshot presented;
    unsigned char presentedGeneration;
    bool frameActive, recovering, bypass;
    volatile int stopping, stopped, pending, generation, installed, statusNote;
    bool installFailed, ownsRow[2];
    bool running, saved;
    void *savedOwner, *savedRows;
    uint32_t originalText[2], appliedText[2];
    State() : profile(0), appUnit(0), checkRequested(0), compatibilityRevision(0), log(0), dispatcher(0), unit(0), timer(0), pin(0), getUnit(0), take(0), give(0),
        next(0), setTimer(0), ctor(0), copy(0), dtor(0), equal(0), original(0), hookedOwner(0),
        presentedGeneration(0), frameActive(false), recovering(false), bypass(false),
        stopping(0), stopped(0), pending(0), generation(0), installed(0), statusNote(0), installFailed(false),
        running(false), saved(false),
        savedOwner(0), savedRows(0) { pthread_mutex_init(&mutex, 0); snprintf(compatibilitySummary,sizeof(compatibilitySummary),"Media compatibility not checked"); }
};
static State state;
inline void log(const char* s) { if (state.log) state.log("[NativeMedia] %s", s); }

inline const Addresses& addresses() { return state.profile->address; }
inline const Layout& layout() { return state.profile->layout; }
inline compatibility::Identity identifyHmi(uintptr_t getUnit) {
    return compatibility::identify(getUnit,&state.stopping);
}
inline bool runtimeProfile(const compatibility::Identity& identity, const Profile* candidate,
                            const uintptr_t exports[kExportCount], char reason[96]) {
    return compatibility::runtimeCheck(identity,candidate,exports,reason);
}
inline void compatibilityStatus(const char* text) {
    pthread_mutex_lock(&state.mutex);
    snprintf(state.compatibilitySummary,sizeof(state.compatibilitySummary),"%s",text);
    ++state.compatibilityRevision;
    pthread_mutex_unlock(&state.mutex);
}
inline bool saveCompatibilityReport(const std::string& report) { return compatibility::saveReport(report); }
inline bool inspectCompatibility(bool activate) {
    uintptr_t exports[kExportCount]; compatibility::resolveExports(exports);
    compatibility::Identity identity;
    const bool cached=!activate && state.profile;
    if (cached) {
        // The running image is unchanged: do not stall media polling to hash it again for a UI check.
        pthread_mutex_lock(&state.mutex); identity=state.identity; pthread_mutex_unlock(&state.mutex);
    } else identity=identifyHmi(exports[kGetUnit]);
    if (state.stopping) return false;
    const Profile* candidate=selectProfile(identity.firmware,identity.sha256);
    if (candidate) compatibility::resolveExports(exports,candidate);
    char reason[96]="Unknown firmware/fingerprint; adapter disabled";
    bool valid=candidate && runtimeProfile(identity,candidate,exports,reason);
    if (activate && valid) {
        state.profile=candidate;
        state.ctor=(StringCtor)exports[kStringCtor]; state.copy=(StringCopy)exports[kStringCopy];
        state.dtor=(StringDtor)exports[kStringDtor]; state.equal=(StringEq)exports[kStringEqual];
        state.getUnit=(GetUnit)exports[kGetUnit]; state.take=(MutexOp)exports[kMutexTake];
        state.give=(MutexOp)exports[kMutexGive]; state.next=(NextNode)exports[kNextNode];
        state.setTimer=(SetTimer)exports[kSetTimer];
        if (!state.pin || !state.appUnit || strcmp(typeName(state.appUnit),"15CHmiAppProcUnit")) {
            valid=false; snprintf(reason,sizeof(reason),"Callback retention or application processing unit unavailable");
        } else {
            state.dispatcher=pointer((uintptr_t)state.appUnit+layout().dispatcherFromUnit);
            if (!state.dispatcher) { valid=false; snprintf(reason,sizeof(reason),"Application dispatcher unavailable"); }
        }
        if (!valid) state.profile=0;
    }
    std::string report=compatibility::report(identity,candidate,exports,valid,reason);
    report+=cached ? "identity_source=verified-startup\n" : "identity_source=fresh-file\n";
    report+=state.installed ? "adapter_installed=1\n" : "adapter_installed=0\n";
    report+=activate ? "check=startup\n" : "check=diagnostic-only; hook state unchanged\n";
    const bool saved=saveCompatibilityReport(report);
    if (!activate) {
        // The existing app logger also exports these diagnostics when Save logs to USB is enabled.
        size_t begin=0;
        while (begin<report.size()) {
            const size_t end=report.find('\n',begin);
            const size_t length=end==std::string::npos ? report.size()-begin : end-begin;
            if (state.log) state.log("[NativeMedia] %.*s",(int)length,report.c_str()+begin);
            if (end==std::string::npos) break;
            begin=end+1;
        }
    }
    char summary[192];
    if (!saved) snprintf(summary,sizeof(summary),"Compatibility report unavailable");
    else if (!identity.firmware[0] || !identity.sha256[0]) snprintf(summary,sizeof(summary),"Firmware identity unavailable");
    else if (!candidate) snprintf(summary,sizeof(summary),"Unknown firmware: adapter off");
    else if (!valid) snprintf(summary,sizeof(summary),"%s: checks failed",candidate->displayName);
    else snprintf(summary,sizeof(summary),"%s: %s",candidate->displayName,
        state.profile?"verified profile":"compatible; adapter off");
    pthread_mutex_lock(&state.mutex); state.identity=identity; pthread_mutex_unlock(&state.mutex);
    compatibilityStatus(summary);
    log(reason);
    return valid;
}

inline void clearSaved() {
    if (!state.saved) return;
    for (unsigned i = 0; i != 2; ++i) { state.dtor(&state.originalText[i]); state.dtor(&state.appliedText[i]); }
    state.saved = false;
}

inline void* context(void* content = 0) {
    if (!state.profile) return 0;
    void* owner = pointer(addresses().ownerGlobal + layout().ownerSlot);
    if (!owner || word((uintptr_t)owner) != addresses().ownerType) return 0;
    uintptr_t p = (uintptr_t)owner;
    const uint32_t contentVtable = state.installed && owner == state.hookedOwner ?
        (uint32_t)(uintptr_t)(state.vtable + 2) : addresses().contentVtable;
    const uint32_t infoVtable = state.installed && owner == state.hookedOwner ?
        (uint32_t)(uintptr_t)(state.infoVtable + 2) : addresses().infoVtable;
    const uint32_t positionVtable = state.installed && owner == state.hookedOwner ?
        (uint32_t)(uintptr_t)(state.positionVtable + 2) : addresses().positionVtable;
    if (word(p + layout().info) != infoVtable || word(p + layout().content) != contentVtable ||
        word(p + layout().position) != positionVtable || (content && content != (void*)(p + layout().content))) return 0;
    uintptr_t rows = (uintptr_t)pointer(p + layout().content + layout().rows);
    if (!rows || *(const unsigned char*)(p + layout().content + layout().rowCount) != 2) return 0;
    RowShape shape[2];
    for (unsigned i = 0; i != 2; ++i) {
        uintptr_t r = rows + i * layout().rowSize;
        shape[i].id = *(const uint16_t*)r; shape[i].type = word(r + layout().rowType);
        shape[i].format2 = word(r + layout().rowFormat2); shape[i].format1 = word(r + layout().rowFormat1);
        shape[i].symbol = word(r + layout().rowSymbol);
    }
    // Info's presentation fields differ from PBI defaults; use the native current-window selector.
    void* app = state.unit ? pointer((uintptr_t)state.unit + layout().appFromUnit) : 0;
    if (!app || word((uintptr_t)app) != addresses().appType || !pointer((uintptr_t)app + layout().appHandle) ||
        word((uintptr_t)app + layout().appMemberType) != addresses().appMemberType) return 0;
    return noAudioShape(((CurrentWindow)addresses().currentWindow)(app), word((uintptr_t)app + layout().audioMode), 2, shape, *state.profile) ? owner : 0;
}
inline bool sameContext(void* owner, void* rows) {
    return state.saved && owner == state.savedOwner && rows == state.savedRows;
}
inline bool isApplied(unsigned i, void* owner, void* rows) {
    return sameContext(owner, rows) && state.ownsRow[i] &&
        state.equal((char*)rows + i * layout().rowSize + layout().rowText, &state.appliedText[i]);
}
inline bool setTexts(void* owner, const NativeMediaSnapshot* record) {
    void* content = (char*)owner + layout().content;
    void* rows = pointer((uintptr_t)content + layout().rows);
    if (!record) {
        bool changed = false;
        const unsigned restore = restoreMask(sameContext(owner, rows),
            isApplied(0, owner, rows), isApplied(1, owner, rows));
        for (unsigned i = 0; i != 2; ++i)
            if (restore & (1u << i)) {
                changed |= !state.equal((char*)rows + i * layout().rowSize + layout().rowText, &state.originalText[i]);
                ((SetText)addresses().setText)(content, i, &state.originalText[i]);
            }
        clearSaved();
        return changed;
    }
    if (!sameContext(owner, rows)) {
        clearSaved();
        for (unsigned i = 0; i != 2; ++i) {
            state.copy(&state.originalText[i], (char*)rows + i * layout().rowSize + layout().rowText);
            state.copy(&state.appliedText[i], (char*)rows + i * layout().rowSize + layout().rowText);
            state.ownsRow[i] = false;
        }
        state.saved = true; state.savedOwner = owner; state.savedRows = rows;
    }
    bool changed = false;
    for (unsigned i = 0; i != 2; ++i) {
        // The factory can change one label while the other is still ours.
        if (!isApplied(i, owner, rows)) {
            state.dtor(&state.originalText[i]);
            state.copy(&state.originalText[i], (char*)rows + i * layout().rowSize + layout().rowText);
        }
        uint16_t text[26]; nativeMediaText(i ? record->artist : record->song, text);
        uint32_t nativeText = 0; state.ctor(&nativeText, text);
        if (!state.equal((char*)rows + i * layout().rowSize + layout().rowText, &nativeText)) {
            ((SetText)addresses().setText)(content, i, &nativeText); changed = true;
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
    out.cursor = nativeMediaCursor(out, nowMs());
    return true;
}

inline unsigned char byte(const void* p, unsigned offset) {
    return *((const unsigned char*)p + offset);
}
inline void dirty(void* p, unsigned char value) { *((unsigned char*)p + layout().dirty) = value; }
inline bool coherent(void* owner) {
    const char* p = (const char*)owner;
    return byte(p + layout().content, layout().contentGeneration) == byte(p + layout().info, layout().otherGeneration) &&
           byte(p + layout().content, layout().contentGeneration) == byte(p + layout().position, layout().otherGeneration);
}
inline bool subscribers(void* owner) {
    const uintptr_t c = (uintptr_t)owner + layout().content, i = (uintptr_t)owner + layout().info, p = (uintptr_t)owner + layout().position;
    const uint16_t* first = (const uint16_t*)pointer(c + layout().subscribersBegin);
    const uintptr_t start = (uintptr_t)first, end = (uintptr_t)pointer(c + layout().subscribersEnd);
    const uintptr_t is = (uintptr_t)pointer(i + layout().subscribersBegin), ie = (uintptr_t)pointer(i + layout().subscribersEnd);
    const uintptr_t ps = (uintptr_t)pointer(p + layout().subscribersBegin), pe = (uintptr_t)pointer(p + layout().subscribersEnd);
    if (end < start || ie < is || pe < ps || (((end - start) | (ie - is) | (pe - ps)) & 1)) return false;
    return sameSubscribers(first, (end - start) / 2, (const uint16_t*)is, (ie - is) / 2) &&
           sameSubscribers(first, (end - start) / 2, (const uint16_t*)ps, (pe - ps) / 2);
}

// Borrow native property/row references only for synchronous serialization; the factory model stays two-row.
struct Frame {
    uint32_t content[14], position[12], info[20], rows[18];
    uint32_t title, artist, empty;
    Frame(void* owner, const NativeMediaSnapshot& record) {
        memcpy(content, (char*)owner + layout().content, sizeof(content));
        memcpy(position, (char*)owner + layout().position, sizeof(position));
        memcpy(info, (char*)owner + layout().info, sizeof(info));
        uint16_t text[26];
        nativeMediaText(record.song, text); state.ctor(&title, text);
        nativeMediaText(record.artist, text); state.ctor(&artist, text);
        text[0] = 0; state.ctor(&empty, text);
        const uint32_t rowLayout[18] = {1,3,1,0,empty,2, 2,2,1,0,title,0, 3,3,1,0,empty,1};
        memcpy(rows, rowLayout, sizeof(rows));
        content[0] = addresses().contentVtable; info[0] = addresses().infoVtable; position[0] = addresses().positionVtable;
        *((unsigned char*)content + layout().rowCount) = 3;
        content[layout().rows/4] = (uint32_t)(uintptr_t)rows;
        info[layout().infoCount/4] = 3; info[layout().infoFormat/4] = 4; info[layout().infoColour/4] = 3;
        info[layout().infoLabel/4] = artist;
        ((uint16_t*)position)[layout().positionCount/2] = 3;
        ((uint16_t*)position)[layout().positionOffset/2] = 0;
        ((uint16_t*)position)[layout().positionFocus/2] = record.cursor == kNativeMediaCursorPrev ? 1 : 3;
        dirty(content, 1); dirty(position, 1); dirty(info, 1);
    }
    ~Frame() { state.dtor(&empty); state.dtor(&artist); state.dtor(&title); }
private:
    Frame(const Frame&);
    Frame& operator=(const Frame&);
};

inline bool presentedFrame(void* owner, const NativeMediaSnapshot& current) {
    return owner && current.active && state.frameActive && !state.stopping && !state.bypass &&
        coherent(owner) && byte((char*)owner + layout().content, layout().contentGeneration) == state.presentedGeneration;
}

static void onTimer(void*, unsigned, unsigned, unsigned short);
inline void wake() {
    if (!state.timer || !__sync_bool_compare_and_swap(&state.pending, 0, 1)) return;
    if (state.setTimer(state.timer, &state, (void*)&onTimer, 0, 10, 0, 0) <= 0)
        __sync_lock_test_and_set(&state.pending, 0);
}
inline bool publicationSnapshot(NativeMediaSnapshot& out) {
    if (snapshot(out)) return true;
    wake();
    // A busy reader must not repaint the committed BT frame as a native "no devices" frame.
    out = state.presented; // IC-thread-owned; never renew its original playback freshness.
    out.active = out.active && state.frameActive && !state.stopping && !state.bypass &&
                 (uint32_t)(nowMs() - out.updatedMs) < kMediaStaleMs;
    return out.active;
}
static int publishHook(void* content, unsigned first, unsigned selector, unsigned mode, unsigned destination) {
    if (state.getUnit() == state.unit && !state.bypass) {
        void* owner = context(content);
        NativeMediaSnapshot record;
        if (!publicationSnapshot(record)) wake();
        else if (mode == 0 && presentedFrame(owner, record)) {
            Frame frame(owner, state.presented);
            return state.original(frame.content, first, selector, mode, destination);
        } else if (first == 0 && selector == 0 && mode == 1 && destination == 0xffff &&
                   owner && record.active && !state.stopping) {
            if (*((const unsigned char*)content + layout().dirty)) setTexts(owner, &record);
            else wake();  // A new text needs the owner's generation transaction, not a same-generation C02.
            if (byte(content, layout().dirty)) state.frameActive = false;
            if (byte(content, layout().dirty) && coherent(owner) && subscribers(owner)) {
                Frame frame(owner, record);
                if (state.original(frame.content, 0, 0, 1, 0xffff) &&
                    ((PublishInfo)addresses().publishPosition)(frame.position, 1, 0xffff) &&
                    ((PublishInfo)addresses().publishInfo)(frame.info, 1, 0xffff)) {
                    dirty(content, byte(frame.content, layout().dirty));
                    dirty((char*)owner + layout().position, byte(frame.position, layout().dirty));
                    dirty((char*)owner + layout().info, byte(frame.info, layout().dirty));
                    state.presented = record;
                    state.presentedGeneration = byte(content, layout().contentGeneration);
                    state.frameActive = true;
                    return 1;
                }
                // Some recipients may already have seen a partial frame: repair with a fresh native generation.
                state.frameActive = false; state.recovering = true; wake();
            }
        } else if (state.saved || state.frameActive) wake();
    }
    return state.original(content, first, selector, mode, destination);
}
static int infoPublishHook(void* info, unsigned mode, unsigned destination) {
    if (state.getUnit() == state.unit && mode == 0) {
        void* owner = context();
        NativeMediaSnapshot record;
        if (owner && info == (char*)owner + layout().info) {
            if (!publicationSnapshot(record)) wake();
            else if (presentedFrame(owner, record)) {
                Frame frame(owner, state.presented);
                return ((PublishInfo)addresses().publishInfo)(frame.info, mode, destination);
            }
        }
    }
    return ((PublishInfo)addresses().publishInfo)(info, mode, destination);
}
static int positionPublishHook(void* position, unsigned mode, unsigned destination) {
    if (state.getUnit() == state.unit && mode == 0) {
        void* owner = context();
        NativeMediaSnapshot record;
        if (owner && position == (char*)owner + layout().position) {
            if (!publicationSnapshot(record)) wake();
            else if (presentedFrame(owner, record)) {
                Frame frame(owner, state.presented);
                return ((PublishInfo)addresses().publishPosition)(frame.position, mode, destination);
            }
        }
    }
    return ((PublishInfo)addresses().publishPosition)(position, mode, destination);
}
static void onTimer(void*, unsigned, unsigned, unsigned short) {
    if (!state.profile) { __sync_lock_test_and_set(&state.pending,0); return; }
    const int generation = state.generation;
    if (state.getUnit() != state.unit) { __sync_lock_test_and_set(&state.pending, 0); return; }
    if (state.installed && pointer(addresses().ownerGlobal + layout().ownerSlot) != state.hookedOwner) {
        state.frameActive = state.recovering = false;
        clearSaved(); __sync_lock_test_and_set(&state.installed, 0);
    }
    if (!state.installed && !state.stopping && !state.installFailed) {
        void* owner = pointer(addresses().ownerGlobal + layout().ownerSlot);
        if (!owner) goto done;
        state.installFailed = true;
        uintptr_t p = (uintptr_t)owner;
        if (word(p) != addresses().ownerType || word(p + layout().info) != addresses().infoVtable ||
            word(p + layout().content) != addresses().contentVtable || word(p + layout().position) != addresses().positionVtable ||
            !cloneContentVtable((const uint32_t*)(addresses().contentVtable-8),
                               (uint32_t)(uintptr_t)&publishHook, state.vtable, *state.profile) ||
            !cloneInfoVtable((const uint32_t*)(addresses().infoVtable-8),
                            (uint32_t)(uintptr_t)&infoPublishHook, state.infoVtable, *state.profile) ||
            !clonePositionVtable((const uint32_t*)(addresses().positionVtable-8),
                                (uint32_t)(uintptr_t)&positionPublishHook, state.positionVtable, *state.profile)) goto done;
        state.original = (Publish)addresses().publishContent; state.hookedOwner = owner;
        __sync_synchronize();
        *(volatile uint32_t*)(p + layout().content) = (uint32_t)(uintptr_t)(state.vtable + 2);
        *(volatile uint32_t*)(p + layout().info) = (uint32_t)(uintptr_t)(state.infoVtable + 2);
        *(volatile uint32_t*)(p + layout().position) = (uint32_t)(uintptr_t)(state.positionVtable + 2);
        state.installFailed = false;
        __sync_lock_test_and_set(&state.installed, 1);
        __sync_lock_test_and_set(&state.statusNote, 1);
    }
    if (state.installed) {
        NativeMediaSnapshot record;
        if (!snapshot(record)) { __sync_lock_test_and_set(&state.pending, 0); wake(); return; }
        void* owner = context();
        const bool active = record.active && !state.stopping;
        const bool previousFrame = state.frameActive;
        if (!active || !owner) state.frameActive = false;
        if (owner) {
            const bool changed = setTexts(owner, active ? &record : 0);
            if (changed || active || previousFrame || state.recovering) {
                state.bypass = state.recovering;
                state.recovering = false;
                // The native transaction advances all three generations when Content is dirty.
                dirty((char*)owner + layout().content, 1);
                ((HighUpdate)addresses().highUpdate)();
                state.bypass = false;
            }
        } else { state.recovering = false; clearSaved(); }
        if (state.stopping) {
            uintptr_t p = (uintptr_t)state.hookedOwner;
            if (pointer(addresses().ownerGlobal + layout().ownerSlot) == state.hookedOwner) {
                if (word(p + layout().content) == (uint32_t)(uintptr_t)(state.vtable + 2))
                    *(volatile uint32_t*)(p + layout().content) = addresses().contentVtable;
                if (word(p + layout().info) == (uint32_t)(uintptr_t)(state.infoVtable + 2))
                    *(volatile uint32_t*)(p + layout().info) = addresses().infoVtable;
                if (word(p + layout().position) == (uint32_t)(uintptr_t)(state.positionVtable + 2))
                    *(volatile uint32_t*)(p + layout().position) = addresses().positionVtable;
                __sync_synchronize();
                __sync_lock_test_and_set(&state.installed, 0); clearSaved();
            }
        }
    }
done:
    if (state.stopping && !state.installed) __sync_lock_test_and_set(&state.stopped, 1);
    __sync_lock_test_and_set(&state.pending, 0);
    if (state.generation != generation || state.recovering || (state.stopping && !state.stopped)) wake();
}

inline void* findIC() {
    void* found = 0;
    state.take((char*)state.dispatcher + layout().dispatcherMutex);
    void* header = (char*)state.dispatcher + layout().dispatcherHeader;
    void* node = pointer((uintptr_t)state.dispatcher + layout().dispatcherRoot);
    for (unsigned n = 0; node && node != header && n < 256 && !found; ++n, node = state.next(node)) {
        void** it = (void**)pointer((uintptr_t)node + layout().nodeBegin);
        void** end = (void**)pointer((uintptr_t)node + layout().nodeEnd);
        if (!it || end < it || end - it > 64) continue;
        for (; it != end; ++it)
            if (*it && !strcmp(typeName(*it), "14CHmiICProcUnit")) { found = *it; break; }
    }
    state.give((char*)state.dispatcher + layout().dispatcherMutex);
    return found;
}
inline bool readRecord(NativeMediaSnapshot& out) {
    out = NativeMediaSnapshot();
    int fd = open("/dev/shmem/nbt-aad-media", O_RDONLY | O_NONBLOCK);
    if (fd < 0) return true; // A removed record withdraws the source immediately.
    bool complete = false;
    char body[kMediaRecordMax + 1], head[40];
    ssize_t n = read(fd, body, sizeof(body));
    if (n > 0 && n <= (ssize_t)kMediaRecordMax && parseNativeMediaRecord(body, (size_t)n, nowMs(), out)) {
        uint32_t pid = 0, sequence = 0;
        if (lseek(fd, 0, SEEK_SET) >= 0) {
            ssize_t h = read(fd, head, sizeof(head));
            complete = h > 0 && nativeMediaGeneration(head, (size_t)h, pid, sequence) &&
                pid == out.writerPid && sequence == out.sequence;
        }
    }
    close(fd);
    return complete;
}
static void* reader(void*) {
    const bool supported=inspectCompatibility(true);
    if (supported) {
        for (unsigned i=0;i!=300 && !state.stopping && !state.unit;++i) {
            state.unit=findIC(); if (!state.unit) usleep(100000);
        }
        if (state.unit) state.timer=((GetTimer)(*(void***)state.unit)[layout().unitTimerSlot])(state.unit);
        if (state.timer) wake();
        else compatibilityStatus("Profile known; adapter unavailable");
    }
    while (!state.stopping) {
        if (__sync_lock_test_and_set(&state.checkRequested,0)) inspectCompatibility(false);
        if (!state.unit || !state.timer) { usleep(100000); continue; }
        if (__sync_lock_test_and_set(&state.statusNote,0)) {
            char message[96];snprintf(message,sizeof(message),"%s private owner hook installed on IC thread",state.profile->displayName);
            log(message);
        }
        NativeMediaSnapshot record;
        if (!readRecord(record)) {
            // An in-place write can be incomplete; retain only the last complete record's original freshness.
            pthread_mutex_lock(&state.mutex);
            record = state.record;
            pthread_mutex_unlock(&state.mutex);
            record.active = record.active && (uint32_t)(nowMs() - record.updatedMs) < kMediaStaleMs;
        }
        record.cursor = nativeMediaCursor(record, nowMs());
        pthread_mutex_lock(&state.mutex);
        bool changed = record.active != state.record.active || strcmp(record.song, state.record.song) ||
                       strcmp(record.artist, state.record.artist) || record.cursor != state.record.cursor ||
                       record.cursorMs != state.record.cursorMs ||
                       record.writerPid != state.record.writerPid;
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
    uintptr_t exports[kExportCount]; compatibility::resolveExports(exports);
    state.getUnit=(GetUnit)exports[kGetUnit];
    state.appUnit=state.getUnit ? state.getUnit() : 0;
    Dl_info library; memset(&library,0,sizeof(library));
    dladdr((void*)&nbtNativeMediaLibraryAnchor,&library);
    const char* paths[] = { library.dli_fname, "/opt/hmi/data/ro/Plugins/NbtAad/libNbtAad.so",
        "/opt/hmi/ID5/data/ro/Plugins/NbtAad/libNbtAad.so", "/var/opt/hmi/data/ID5/Plugins/NbtAad/libNbtAad.so" };
    for (unsigned i = 0; !state.pin && i != 4; ++i) {
        if (!paths[i]) continue;
        void* handle = dlopen(paths[i], RTLD_NOW | RTLD_LOCAL);
        if (handle && dlsym(handle, "nbtNativeMediaLibraryAnchor") == (void*)&nbtNativeMediaLibraryAnchor) state.pin = handle;
        else if (handle) dlclose(handle);
    }
    if (!state.pin) log("cannot retain native callback library; adapter will remain disabled");
    pthread_mutex_lock(&state.mutex);
    state.record = NativeMediaSnapshot();
    pthread_mutex_unlock(&state.mutex);
    state.profile=0; state.dispatcher=0; state.checkRequested=0;
    state.unit = state.timer = 0;
    state.frameActive = state.recovering = state.bypass = false;
    state.stopping = state.stopped = 0; state.installFailed = false;
    compatibilityStatus("Checking media compatibility...");
    if (pthread_create(&state.worker, 0, &reader, 0)) { compatibilityStatus("Compatibility worker unavailable"); return false; }
    state.running = true;
#ifdef NBT_NATIVE_MEDIA_GUEST_TEST
    guestStart(state.appUnit);
#endif
    return true;
}
inline bool requestCompatibilityCheck() {
    if (!detail::state.running || detail::state.stopping) return false;
    detail::compatibilityStatus("Checking media compatibility...");
    __sync_lock_test_and_set(&detail::state.checkRequested,1);
    return true;
}
inline unsigned compatibilityStatus(char* out, size_t size) {
    pthread_mutex_lock(&detail::state.mutex);
    snprintf(out,size,"%s",detail::state.compatibilitySummary);
    const unsigned revision=detail::state.compatibilityRevision;
    pthread_mutex_unlock(&detail::state.mutex);
    return revision;
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
inline bool requestCompatibilityCheck() { return false; }
inline unsigned compatibilityStatus(char* out,size_t size) { snprintf(out,size,"Compatibility unavailable"); return 0; }
inline void stop() {}
#endif
} }
#if defined(__QNXNTO__) && defined(NBT_NATIVE_MEDIA_GUEST_TEST)
#include "../tests/native_media_guest_control.hpp"
#endif
#endif
