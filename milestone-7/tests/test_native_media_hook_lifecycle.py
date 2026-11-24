# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
"""Execute the real QNX hook with native ABI, file, clock and timer fixtures.

Only platform boundaries are replaced. start/stop, reader, onTimer, publishHook,
setTexts and native-string ownership are the production implementations.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "rhmiapp/native_media_hook.hpp").read_text()


def boundary(signature, body):
    global source
    start = source.index(signature)
    opening = source.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    source = source[:opening + 1] + "\n" + body + "\n" + source[end - 1:]


source = source.replace('#include "../src/audio/native_media_record.hpp"', '#include "audio/native_media_record.hpp"')
source = source.replace("#ifdef __QNXNTO__", "#if 1")
boundary("inline uint32_t word(", "return fixtureWord(p);")
boundary("inline void* pointer(", "return fixturePointer(p);")
boundary("inline uint32_t nowMs(", "return fixtureNow;")
boundary("inline const char* typeName(", 'return p == fixtureApp ? "15CHmiAppProcUnit" : "14CHmiICProcUnit";')
boundary("inline bool build66(", "return fixtureBuild();")
boundary("inline void* context(", "return fixtureContext(content);")
boundary("inline void* findIC(", "return fixtureIC;")
boundary("inline NativeMediaSnapshot readRecord(", "return fixtureRecord();")
source = source.replace("((SetText)0x0235a558)", "fixtureSetText")
source = source.replace("((HighUpdate)0x0235206c)", "fixtureHighUpdate")
source = source.replace("(const uint32_t*)0x02ec0170", "fixtureVtable")
source = source.replace("(Publish)0x0235b634", "fixturePublish")

prefix = r'''
#include "audio/native_media_record.hpp"
#include <assert.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <map>
#include <string>
#include <vector>

static unsigned char fixtureOwnerBytes[1024], fixtureRows[48], fixtureAppBytes[1024];
static void* fixtureOwner = fixtureOwnerBytes;
static void* fixtureApp = fixtureAppBytes;
static void* fixtureIC = fixtureRows;
static void* fixtureCurrent = fixtureApp;
static uint32_t fixtureVtable[20], fixtureNow = 1000;
static uint32_t fixtureWindow = 0xc495;
static pthread_t fixtureMain;
static bool fixturePumpEnabled = true;
static unsigned fixtureUpdates, fixturePublishes, fixturePins;
static pthread_mutex_t fixtureMutex = PTHREAD_MUTEX_INITIALIZER;
static nbt::NativeMediaSnapshot fixtureInput;
static uint32_t fixtureWord(uintptr_t);
static void* fixturePointer(uintptr_t);
static bool fixtureBuild();
static void* fixtureContext(void*);
static nbt::NativeMediaSnapshot fixtureRecord();
static bool fixtureSetText(void*, unsigned, const void*);
static bool fixtureHighUpdate();
static int fixturePublish(void*, unsigned, unsigned, unsigned, unsigned);
static void* fixtureDlsym(void*, const char*);
static void* fixtureDlopen(const char*, int);
static int fixtureDlclose(void*);
static int fixtureSleep(unsigned);
#define dlsym fixtureDlsym
#define dlopen fixtureDlopen
#define dlclose fixtureDlclose
#define usleep fixtureSleep
'''

suffix = r'''
#undef dlsym
#undef dlopen
#undef dlclose
#undef usleep
using namespace nbt::native_media;
using namespace nbt::native_media::detail;

struct String { std::vector<uint16_t> text; unsigned refs; };
static std::map<uint32_t, String> fixtureStrings;
static uint32_t fixtureNextString = 0;
static void fixtureCtor(void* p, const uint16_t* s) {
    String value; value.refs = 1;
    while (*s) value.text.push_back(*s++);
    value.text.push_back(0);
    fixtureStrings[++fixtureNextString] = value;
    *(uint32_t*)p = fixtureNextString;
}
static void fixtureCopy(void* to, const void* from) {
    const uint32_t id = *(const uint32_t*)from;
    assert(fixtureStrings.count(id));
    ++fixtureStrings[id].refs; *(uint32_t*)to = id;
}
static void fixtureDtor(void* p) {
    uint32_t& id = *(uint32_t*)p;
    assert(fixtureStrings.count(id) && fixtureStrings[id].refs);
    if (!--fixtureStrings[id].refs) fixtureStrings.erase(id);
    id = 0;
}
static bool fixtureEqual(const void* a, const void* b) {
    return fixtureStrings.at(*(const uint32_t*)a).text == fixtureStrings.at(*(const uint32_t*)b).text;
}
static uint32_t fixtureWord(uintptr_t p) {
    // The ctor anchor is the only native instruction examined outside build66().
    if (p < (uintptr_t)fixtureOwner || p >= (uintptr_t)fixtureOwner + sizeof(fixtureOwnerBytes)) return 0xe58432e8;
    uint32_t out; memcpy(&out, (void*)p, 4); return out;
}
static void* fixturePointer(uintptr_t p) {
    if (p == 0x038302c0 + 0xd4) return fixtureOwner;
    if (p == (uintptr_t)fixtureOwner + 0x33c + 0x30) return fixtureRows;
    if (p == (uintptr_t)fixtureApp + 0x2e8) return fixtureApp;
    assert(false); return 0;
}
static bool fixtureBuild() {
    state.ctor = fixtureCtor; state.copy = fixtureCopy;
    state.dtor = fixtureDtor; state.equal = fixtureEqual; return true;
}
static void* fixtureContext(void* content) {
    if (content && content != (char*)fixtureOwner + 0x33c) return 0;
    const RowShape rows[] = {{1,2,1,1,0}, {2,2,1,1,0}};
    return state.installed &&
        noAudioShape(fixtureWindow, fixtureWindow == 0xc495 ? 49 : 20, 2, rows) ? fixtureOwner : 0;
}
static nbt::NativeMediaSnapshot fixtureRecord() {
    pthread_mutex_lock(&fixtureMutex);
    const nbt::NativeMediaSnapshot record = fixtureInput;
    pthread_mutex_unlock(&fixtureMutex);
    return record;
}
static bool fixtureSetText(void* content, unsigned index, const void* text) {
    assert(content == (char*)fixtureOwner + 0x33c && index < 2 && fixtureCurrent == fixtureIC);
    void* row = fixtureRows + index * 24 + 16;
    if (fixtureEqual(row, text)) return false;
    fixtureDtor(row); fixtureCopy(row, text);
    *((unsigned char*)content + 0x1a) = 1;
    return true;
}
static int fixturePublish(void* content, unsigned first, unsigned count, unsigned mode, unsigned destination) {
    assert(content == (char*)fixtureOwner + 0x33c);
    assert(first == 0 && count == 0 && mode == 1 && destination == 0xffff);
    ++fixturePublishes; *((unsigned char*)content + 0x1a) = 0; return 1;
}
static bool fixtureHighUpdate() {
    assert(fixtureCurrent == fixtureIC);
    ++fixtureUpdates;
    return publishHook((char*)fixtureOwner + 0x33c, 0, 0, 1, 0xffff) != 0;
}
typedef void (*Callback)(void*, unsigned, unsigned, unsigned short);
struct Event { void* object; Callback fn; };
static std::vector<Event> fixtureEvents;
static int fixtureSetTimer(void*, void* object, void* fn, int adjustment, unsigned, unsigned, unsigned short) {
    assert(!adjustment);
    Event event = { object, (Callback)fn };
    pthread_mutex_lock(&fixtureMutex); fixtureEvents.push_back(event); pthread_mutex_unlock(&fixtureMutex);
    return 1;
}
static bool fixturePump() {
    pthread_mutex_lock(&fixtureMutex);
    if (fixtureEvents.empty()) { pthread_mutex_unlock(&fixtureMutex); return false; }
    const Event event = fixtureEvents.front(); fixtureEvents.erase(fixtureEvents.begin());
    pthread_mutex_unlock(&fixtureMutex);
    void* old = fixtureCurrent; fixtureCurrent = fixtureIC;
    event.fn(event.object, 0, 0, 0); fixtureCurrent = old; return true;
}
static int fixtureSleep(unsigned) {
    if (pthread_equal(pthread_self(), fixtureMain) && fixturePumpEnabled) fixturePump();
    return usleep(1000);
}
static void* fixtureGetUnit() { return fixtureCurrent; }
static void fixtureNativeCtor() {}
static void fixtureMutexOp(void*) {}
static void* fixtureNext(void*) { return 0; }
static void* fixtureGetTimer(void*) { return fixtureIC; }
static void* fixtureDlsym(void*, const char* name) {
    if (!strcmp(name, "_Z14getHmiProcUnitv")) return (void*)fixtureGetUnit;
    if (!strcmp(name, "_ZN8COSMutex4takeEv") || !strcmp(name, "_ZN8COSMutex4giveEv")) return (void*)fixtureMutexOp;
    if (!strcmp(name, "_ZSt18_Rb_tree_incrementPSt18_Rb_tree_node_base")) return (void*)fixtureNext;
    if (!strcmp(name, "_ZN9CHmiTimer9_setTimerEPvM15CDelegateTargetFvjjtEjjt")) return (void*)fixtureSetTimer;
    if (!strcmp(name, "_ZN12CHmiProcUnitC1ER18CHmiMainDispatcherh")) return (void*)fixtureNativeCtor;
    if (!strcmp(name, "nbtNativeMediaLibraryAnchor")) return (void*)nbtNativeMediaLibraryAnchor;
    assert(false); return 0;
}
static void* fixtureDlopen(const char*, int) { ++fixturePins; return fixtureIC; }
static int fixtureDlclose(void*) { assert(false); return 0; }
static void fixtureSetInput(bool active, const char* song = "Трек", const char* artist = "Автор") {
    pthread_mutex_lock(&fixtureMutex);
    fixtureInput.active = active; fixtureInput.updatedMs = fixtureNow;
    strcpy(fixtureInput.song, song); strcpy(fixtureInput.artist, artist);
    pthread_mutex_unlock(&fixtureMutex);
}
static bool fixtureText(unsigned row, const char* text) {
    uint16_t out[26]; const size_t n = nbt::nativeMediaText(text,out);
    return fixtureStrings.at(*(uint32_t*)(fixtureRows + row*24 + 16)).text == std::vector<uint16_t>(out,out+n+1);
}
static void fixtureWait(bool active) {
    for (unsigned i = 0; i != 1000; ++i) {
        fixturePump();
        if (active ? fixtureText(0,"Трек") && fixtureText(1,"Автор") : fixtureText(0,"Factory title") && fixtureText(1,"Factory artist")) return;
        usleep(1000);
    }
    assert(false);
}
static void fixtureFactoryRow(unsigned row, const char* value) {
    uint16_t out[26]; nbt::nativeMediaText(value,out);
    uint32_t text; fixtureCtor(&text,out);
    void* old = fixtureCurrent; fixtureCurrent = fixtureIC;
    fixtureSetText((char*)fixtureOwner + 0x33c,row,&text);
    fixtureCurrent = old; fixtureDtor(&text);
}
int main(int argc, char**) {
    fixtureWindow = argc > 1 ? 0xc491 : 0xc495;
    fixtureMain = pthread_self();
    uint32_t ownerVptr=0x02c98cd0, infoVptr=0x02ec0068, contentVptr=0x02ec0178, positionVptr=0x02ec0028;
    memcpy(fixtureOwnerBytes,&ownerVptr,4); memcpy(fixtureOwnerBytes+0x2ec,&infoVptr,4);
    memcpy(fixtureOwnerBytes+0x33c,&contentVptr,4); memcpy(fixtureOwnerBytes+0x374,&positionVptr,4);
    fixtureVtable[1]=0x02ec0284; fixtureVtable[18]=0x0235b634;
    void* timers[4] = {0,0,0,(void*)fixtureGetTimer};
    *(void***)fixtureIC = timers;
    uint16_t first[26], second[26]; nbt::nativeMediaText("Factory title",first); nbt::nativeMediaText("Factory artist",second);
    fixtureCtor(fixtureRows+16,first); fixtureCtor(fixtureRows+40,second);
    fixtureSetInput(true);
    assert(start(0)); assert(start(0)); fixtureWait(true);
    assert(state.installed && state.saved && fixturePins == 1 && fixtureUpdates && fixturePublishes);
    stop();
    assert(!state.running && !state.installed && !state.pending && state.stopped && !state.saved);
    assert(fixtureText(0,"Factory title") && fixtureText(1,"Factory artist"));
    assert(fixtureWord((uintptr_t)fixtureOwner + 0x33c)==0x02ec0178);
    assert(fixtureStrings.size()==2); // Only the two native rows retain strings.
    stop(); // Idempotent.

    assert(start(0)); fixtureWait(true);
    fixtureFactoryRow(0,"OEM replacement");
    stop();
    assert(fixtureText(0,"OEM replacement") && fixtureText(1,"Factory artist"));
    assert(fixtureStrings.size()==2);

    assert(start(0)); fixtureWait(true);
    fixtureFactoryRow(1,"OEM artist");
    // Another update recaches only the changed factory row, not our title as an original.
    fixtureCurrent=fixtureIC; assert(setTexts(fixtureOwner,&fixtureInput)); fixtureCurrent=fixtureApp;
    stop();
    assert(fixtureText(0,"OEM replacement") && fixtureText(1,"OEM artist"));
    assert(fixtureStrings.size()==2);

    assert(start(0)); fixtureWait(true);
    fixturePumpEnabled=false; wake(); stop();
    assert(!state.running && state.stopping && !state.stopped && state.pending);
    assert(!start(0)); // A queued old cleanup must finish before a new lifecycle starts.
    fixturePumpEnabled=true; while(fixturePump()) {}
    assert(state.stopped && !state.installed && !state.pending);
    fixtureSetInput(false);
    assert(start(0));
    for(unsigned i=0;i!=30;++i) { fixturePump(); usleep(1000); }
    assert(fixtureText(0,"OEM replacement") && fixtureText(1,"OEM artist"));
    stop(); assert(fixtureStrings.size()==2 && fixturePins==1);
    fixtureDtor(fixtureRows+16); fixtureDtor(fixtureRows+40); assert(fixtureStrings.empty());
    puts("PASS actual hook lifecycle: install, stop/unhook, restart, queued cleanup and per-row restore");
}
'''

compiler = shutil.which("g++") or shutil.which("clang++")
assert compiler, "A POSIX C++ compiler is required"
with tempfile.TemporaryDirectory(prefix="native-hook-lifecycle-") as temp:
    cpp, exe = Path(temp) / "lifecycle.cpp", Path(temp) / "lifecycle"
    cpp.write_text(prefix + source + suffix)
    subprocess.run([compiler, "-std=c++98", "-Wall", "-pthread", "-I" + str(ROOT / "src"),
                    "-I" + str(ROOT / "include"), str(cpp), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
    subprocess.run([str(exe), "external-media"], check=True)
