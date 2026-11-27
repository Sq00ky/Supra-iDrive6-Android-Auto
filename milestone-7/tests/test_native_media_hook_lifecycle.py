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
boundary("inline uint32_t nowMs(", "return __atomic_load_n(&fixtureNow, __ATOMIC_ACQUIRE);")
boundary("inline const char* typeName(", 'return p == fixtureApp ? "15CHmiAppProcUnit" : "14CHmiICProcUnit";')
boundary("inline compatibility::Identity identifyHmi(", r'''compatibility::Identity out;
    ++fixtureIdentityChecks;
    __atomic_store_n(&fixtureIdentityEntered,1,__ATOMIC_RELEASE);
    if (fixtureIdentityKind==3) while (!state.stopping) usleep(1000);
    snprintf(out.firmware,sizeof(out.firmware),"%s",fixtureIdentityKind==1 ? "unknown firmware" : kBuild66.firmware);
    snprintf(out.sha256,sizeof(out.sha256),"%s",fixtureIdentityKind==2 ? "unknown fingerprint" : kBuild66.hmiSha256);
    out.processBound=fixtureIdentityKind!=4; out.arm32Executable=true; return out;''')
boundary("inline bool runtimeProfile(", "++fixtureRuntimeChecks; return fixtureRuntimeValid && identity.processBound && identity.arm32Executable;")
boundary("inline bool saveCompatibilityReport(", "fixtureReport=report; return true;")
boundary("inline void* context(", "return fixtureContext(content);")
boundary("inline void* findIC(", "return fixtureIC;")
boundary("inline bool readRecord(", "out = fixtureRecord(); return __atomic_load_n(&fixtureInputValid, __ATOMIC_ACQUIRE);")
source = source.replace("((SetText)addresses().setText)", "fixtureSetText")
source = source.replace("((HighUpdate)addresses().highUpdate)", "fixtureHighUpdate")
source = source.replace("(const uint32_t*)(addresses().contentVtable-8)", "fixtureVtable")
source = source.replace("(const uint32_t*)(addresses().infoVtable-8)", "fixtureInfoVtable")
source = source.replace("(const uint32_t*)(addresses().positionVtable-8)", "fixturePositionVtable")
source = source.replace("(Publish)addresses().publishContent", "fixturePublish")
source = source.replace("((PublishInfo)addresses().publishPosition)", "fixturePublishPosition")
source = source.replace("((PublishInfo)addresses().publishInfo)", "fixturePublishInfo")

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

static unsigned char fixtureOwnerBytes[1024], fixtureRows[48], fixtureAppBytes[1024], fixtureICBytes[64];
static void* fixtureOwner = fixtureOwnerBytes;
static void* fixtureGlobalOwner = fixtureOwnerBytes;
static void* fixtureApp = fixtureAppBytes;
static void* fixtureIC = fixtureICBytes;
static void* fixtureCurrent = fixtureApp;
static uint32_t fixtureVtable[20], fixtureInfoVtable[16], fixturePositionVtable[16], fixtureNow = 1000;
static uint32_t fixtureWindow = 0xc495;
static pthread_t fixtureMain;
static bool fixturePumpEnabled = true;
static unsigned fixtureUpdates, fixturePublishes, fixturePins;
static unsigned fixtureReads;
static int fixtureIdentityKind;
static int fixtureIdentityEntered;
static unsigned fixtureIdentityChecks;
static unsigned fixtureRuntimeChecks;
static unsigned fixturePrivateReads;
static bool fixtureRuntimeValid=true;
static std::string fixtureReport;
static int fixtureInputValid = 1;
static uint16_t fixtureSubs[3][2] = {{0x1234,0x5678},{0x1234,0x5678},{0x1234,0x5678}};
static unsigned fixtureSubCount[3] = {2,2,2};
static unsigned fixtureFailKind, fixtureFailRemaining;
static bool fixtureSerializeFailure;
struct Publication {
    unsigned kind, first, selector, mode, destination, generation, count, focus;
    bool shadow, dirtyBefore, succeeded;
    std::vector<uint16_t> title, artist;
};
static std::vector<Publication> fixtureCalls, fixtureFrames;
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
static int fixturePublishPosition(void*, unsigned, unsigned);
static int fixturePublishInfo(void*, unsigned, unsigned);
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
    ++fixturePrivateReads;
    // Native memory is simulated independently of the selected profile.
    if (p < (uintptr_t)fixtureOwner || p >= (uintptr_t)fixtureOwner + sizeof(fixtureOwnerBytes)) return 0xe58432e8;
    uint32_t out; memcpy(&out, (void*)p, 4); return out;
}
static void* fixturePointer(uintptr_t p) {
    ++fixturePrivateReads;
    if (p == 0x038302c0 + 0xd4) return fixtureGlobalOwner;
    if (p == (uintptr_t)fixtureOwner + 0x33c + 0x30) return fixtureRows;
    if (p == (uintptr_t)fixtureApp + 0x2e8) return fixtureApp;
    const unsigned offsets[] = {0x33c,0x374,0x2ec};
    for (unsigned i=0;i!=3;++i) {
        if (p == (uintptr_t)fixtureOwner+offsets[i]+0x1c) return fixtureSubs[i];
        if (p == (uintptr_t)fixtureOwner+offsets[i]+0x20) return fixtureSubs[i]+fixtureSubCount[i];
    }
    assert(false); return 0;
}
static bool fixtureBuild() {
    state.profile=&kBuild66;
    state.ctor = fixtureCtor; state.copy = fixtureCopy;
    state.dtor = fixtureDtor; state.equal = fixtureEqual; return true;
}
static void* fixtureContext(void* content) {
    if (content && content != (char*)fixtureOwner + 0x33c) return 0;
    RowShape rows[2];
    for (unsigned i=0;i!=2;++i) {
        const uint32_t* r=(const uint32_t*)(fixtureRows+i*24);
        rows[i].id=(uint16_t)r[0]; rows[i].type=r[1]; rows[i].format2=r[2];
        rows[i].format1=r[3]; rows[i].symbol=r[5];
    }
    return state.installed && fixtureGlobalOwner == fixtureOwner && state.hookedOwner == fixtureOwner &&
        fixtureWord((uintptr_t)fixtureOwner+0x33c)==(uint32_t)(uintptr_t)(state.vtable+2) &&
        fixtureWord((uintptr_t)fixtureOwner+0x2ec)==(uint32_t)(uintptr_t)(state.infoVtable+2) &&
        fixtureWord((uintptr_t)fixtureOwner+0x374)==(uint32_t)(uintptr_t)(state.positionVtable+2) &&
        noAudioShape(fixtureWindow, fixtureWindow == 0xc495 ? 49 : 20, 2, rows, kBuild66) ? fixtureOwner : 0;
}
static nbt::NativeMediaSnapshot fixtureRecord() {
    pthread_mutex_lock(&fixtureMutex);
    const nbt::NativeMediaSnapshot record = fixtureInput;
    pthread_mutex_unlock(&fixtureMutex);
    __atomic_fetch_add(&fixtureReads,1,__ATOMIC_RELEASE);
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
static std::vector<uint16_t> fixtureString(uint32_t id) { return fixtureStrings.at(id).text; }
static void* fixtureProperty(unsigned kind) {
    return (char*)fixtureOwner+(kind==1 ? 0x33c : kind==2 ? 0x374 : 0x2ec);
}
static int fixturePublishProperty(void* property, unsigned kind, unsigned first,
                                  unsigned selector, unsigned mode, unsigned destination) {
    assert(fixtureCurrent==fixtureIC);
    Publication out = {};
    out.kind=kind; out.first=first; out.selector=selector; out.mode=mode; out.destination=destination;
    out.shadow=property!=fixtureProperty(kind); out.dirtyBefore=byte(property,0x1a)!=0; out.succeeded=true;
    out.generation=byte(property,kind==1 ? 0x34 : 0x28);
    if (kind==1) {
        out.count=byte(property,0x2c);
        // The production frame encodes an ARM32 pointer. Recover its actual host address from its container,
        // while still checking the low 32 bits that the native ABI serializes.
        const uint32_t* rows=out.shadow ? ((Frame*)property)->rows : (const uint32_t*)fixtureRows;
        assert(((const uint32_t*)property)[0x30/4]==(uint32_t)(uintptr_t)rows);
        if (out.shadow) {
            const uint32_t expected[18]={1,3,1,0,rows[4],2, 2,2,1,0,rows[10],0, 3,3,1,0,rows[16],1};
            assert(out.count==3 && !memcmp(rows,expected,sizeof(expected)));
            assert(fixtureString(rows[4]).size()==1 && fixtureString(rows[16]).size()==1);
        } else assert(out.count==2);
        out.title=fixtureString(rows[(out.shadow ? 1 : 0)*6+4]);
        // Preserve the native boundary's error return for unsupported selectors and absent row IDs.
        if (mode==0 && (selector==1 || selector>7 || first>out.count)) out.succeeded=false;
    } else if (kind==2) {
        out.count=((const uint16_t*)property)[0x2a/2];
        out.focus=((const uint16_t*)property)[0x2e/2];
        if (out.shadow) assert(out.count==3 && ((const uint16_t*)property)[0x2c/2]==0);
    } else {
        out.artist=fixtureString(((const uint32_t*)property)[0x38/4]);
        if (out.shadow) {
            assert(((const uint32_t*)property)[0x2c/4]==3 && ((const uint32_t*)property)[0x44/4]==4);
            assert(((const uint32_t*)property)[0x48/4]==3 && byte(property,0x4c)==1);
            assert(((const uint32_t*)property)[0x3c/4]==((const uint32_t*)fixtureProperty(3))[0x3c/4]);
        }
    }
    if (mode==1 && out.shadow) {
        // No original flag may be committed before the complete Content/Position/Info transaction succeeds.
        for (unsigned i=1;i!=4;++i) assert(byte(fixtureProperty(i),0x1a)==1);
    }
    if (out.shadow && mode==1 && fixtureFailKind==kind && fixtureFailRemaining) {
        --fixtureFailRemaining; out.succeeded=false;
    }
    fixtureCalls.push_back(out); ++fixturePublishes;
    const bool serializationFailed=!out.succeeded && (mode==0 || fixtureSerializeFailure);
    if (!serializationFailed && (mode==0 || (out.dirtyBefore && fixtureSubCount[kind-1]))) fixtureFrames.push_back(out);
    if (mode==1 && out.dirtyBefore && !serializationFailed) dirty(property,0); // Transport failure still consumes dirty.
    return out.succeeded ? 1 : 0;
}
static int fixturePublish(void* p, unsigned first, unsigned selector, unsigned mode, unsigned destination) {
    return fixturePublishProperty(p,1,first,selector,mode,destination);
}
static int fixturePublishPosition(void* p, unsigned mode, unsigned destination) {
    return fixturePublishProperty(p,2,0,0,mode,destination);
}
static int fixturePublishInfo(void* p, unsigned mode, unsigned destination) {
    return fixturePublishProperty(p,3,0,0,mode,destination);
}
static bool fixtureHighUpdate() {
    assert(fixtureCurrent == fixtureIC);
    ++fixtureUpdates;
    // Verified Build66 02352850..28C4: Content dirty advances the byte counter before syncing all properties.
    // Do not force it here: cursor-only changes must be preseeded by the production timer.
    unsigned char generation=byte(fixtureProperty(1),0x34);
    if (byte(fixtureProperty(1),0x1a)) ++generation;
    *((unsigned char*)fixtureProperty(1)+0x34)=generation;
    for (unsigned kind=2;kind!=4;++kind) {
        if (byte(fixtureProperty(kind),0x28)!=generation) dirty(fixtureProperty(kind),1);
        *((unsigned char*)fixtureProperty(kind)+0x28)=generation;
    }
    const bool content=publishHook(fixtureProperty(1),0,0,1,0xffff)!=0;
    fixturePublishPosition(fixtureProperty(2),1,0xffff);
    fixturePublishInfo(fixtureProperty(3),1,0xffff);
    return content;
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
    if (!strcmp(name,kBuild66Exports[kStringCtor].name)) return (void*)fixtureCtor;
    if (!strcmp(name,kBuild66Exports[kStringCopy].name)) return (void*)fixtureCopy;
    if (!strcmp(name,kBuild66Exports[kStringDtor].name)) return (void*)fixtureDtor;
    if (!strcmp(name,kBuild66Exports[kStringEqual].name)) return (void*)fixtureEqual;
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
    fixtureInput.writerPid=77; ++fixtureInput.sequence;
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
static void fixtureInit() {
    fixtureMain = pthread_self();
    uint32_t ownerVptr=0x02c98cd0, infoVptr=0x02ec0068, contentVptr=0x02ec0178, positionVptr=0x02ec0028;
    memcpy(fixtureOwnerBytes,&ownerVptr,4); memcpy(fixtureOwnerBytes+0x2ec,&infoVptr,4);
    memcpy(fixtureOwnerBytes+0x33c,&contentVptr,4); memcpy(fixtureOwnerBytes+0x374,&positionVptr,4);
    fixtureVtable[1]=0x02ec0284; fixtureVtable[18]=0x0235b634;
    fixtureInfoVtable[1]=0x02ec02b4; fixtureInfoVtable[14]=0x0235afc0;
    fixturePositionVtable[1]=0x02ec0250; fixturePositionVtable[8]=0x02359b54;
    fixturePositionVtable[14]=0x02359b6c; fixturePositionVtable[15]=0x01f7439c;
    static void* timers[4] = {0,0,0,(void*)fixtureGetTimer};
    *(void***)fixtureIC = timers;
    for (unsigned i=0;i!=2;++i) {
        uint32_t* r=(uint32_t*)(fixtureRows+i*24);
        r[0]=i+1; r[1]=2; r[2]=r[3]=1; r[5]=0;
    }
    ((uint32_t*)fixtureProperty(1))[0x30/4]=(uint32_t)(uintptr_t)fixtureRows;
    *((unsigned char*)fixtureProperty(1)+0x2c)=2;
    *((unsigned char*)fixtureProperty(1)+0x34)=19;
    *((unsigned char*)fixtureProperty(1)+0x35)=1;
    *((unsigned char*)fixtureProperty(2)+0x28)=19;
    ((uint16_t*)fixtureProperty(2))[0x2a/2]=2;
    *((unsigned char*)fixtureProperty(3)+0x28)=19;
    ((uint32_t*)fixtureProperty(3))[0x2c/4]=fixtureWindow==0xc495 ? 0 : 3;
    ((uint32_t*)fixtureProperty(3))[0x44/4]=1;
    ((uint32_t*)fixtureProperty(3))[0x48/4]=fixtureWindow==0xc495 ? 2 : 3;
    *((unsigned char*)fixtureProperty(3)+0x4c)=1;
    // Nonzero padding proves generation comparisons use bytes, rather than ARM word/halfword loads.
    *((unsigned char*)fixtureProperty(3)+0x29)=0xa1;
    *((unsigned char*)fixtureProperty(2)+0x29)=0xb1;
    *((unsigned char*)fixtureProperty(1)+0x36)=0xc1;
    for (unsigned k=1;k!=4;++k) {
        uint32_t* p=(uint32_t*)fixtureProperty(k);
        p[0x10/4]=0x12345678; p[0x1c/4]=0x22334455; p[0x20/4]=0x22334459;
    }
    uint16_t first[26], second[26]; nbt::nativeMediaText("Factory title",first); nbt::nativeMediaText("Factory artist",second);
    fixtureCtor(fixtureRows+16,first); fixtureCtor(fixtureRows+40,second);
    nbt::nativeMediaText("Factory device",first); fixtureCtor((char*)fixtureProperty(3)+0x38,first);
    nbt::nativeMediaText("Factory secondary",first); fixtureCtor((char*)fixtureProperty(3)+0x3c,first);
}
static void fixtureFinish() {
    fixtureDtor(fixtureRows+16); fixtureDtor(fixtureRows+40);
    fixtureDtor((char*)fixtureProperty(3)+0x38); fixtureDtor((char*)fixtureProperty(3)+0x3c);
    assert(fixtureStrings.empty());
}
static std::vector<uint16_t> fixtureExpected(const char* text) {
    uint16_t out[26]; const size_t n=nbt::nativeMediaText(text,out);
    return std::vector<uint16_t>(out,out+n+1);
}
static void fixtureDirectRecord(bool active=true) {
    fixtureSetInput(active);
    pthread_mutex_lock(&state.mutex); state.record=fixtureInput; pthread_mutex_unlock(&state.mutex);
}
static void fixtureInstallDirect() {
    assert(fixtureBuild()); state.getUnit=fixtureGetUnit; state.setTimer=fixtureSetTimer;
    state.unit=fixtureIC; state.timer=fixtureIC;
    fixtureCurrent=fixtureIC; wake(); assert(fixturePump());
    assert(state.installed && !state.pending);
    fixtureCurrent=fixtureIC;
}
static void fixtureStopDirect() {
    state.stopping=1; wake();
    for (unsigned n=0;fixturePump();++n) assert(n<8);
    assert(!state.installed && state.stopped && !state.pending && !state.saved && !state.frameActive);
    assert(fixtureStrings.size()==4);
}
static void fixtureSeed() {
    assert(setTexts(fixtureOwner,&state.record));
    for (unsigned k=1;k!=4;++k) dirty(fixtureProperty(k),1);
}
static void fixtureAssertFrame(unsigned start, unsigned focus, unsigned gen) {
    assert(fixtureFrames.size()==start+3);
    for (unsigned i=0;i!=3;++i) {
        const Publication& f=fixtureFrames[start+i];
        assert(f.kind==i+1 && f.shadow && f.mode==1 && f.destination==0xffff && f.generation==gen);
    }
    assert(fixtureFrames[start].title==fixtureExpected("Трек"));
    assert(fixtureFrames[start+1].focus==focus);
    assert(fixtureFrames[start+2].artist==fixtureExpected("Автор"));
}
static void fixtureTransaction() {
    fixtureInstallDirect(); fixtureDirectRecord(); fixtureSeed();
    unsigned char before[1024], rows[48]; memcpy(before,fixtureOwnerBytes,sizeof(before)); memcpy(rows,fixtureRows,48);
    assert(publishHook(fixtureProperty(1),0,0,1,0xffff)==1);
    fixtureAssertFrame(0,3,19);
    assert(state.frameActive && state.presentedGeneration==19);
    unsigned char expected[1024]; memcpy(expected,before,sizeof(expected));
    for (unsigned k=1;k!=4;++k) expected[(char*)fixtureProperty(k)-(char*)fixtureOwner+0x1a]=0;
    assert(!memcmp(fixtureOwnerBytes,expected,sizeof(expected)) && !memcmp(fixtureRows,rows,48));
    const unsigned sent=fixtureFrames.size();
    fixturePublishPosition(fixtureProperty(2),1,0xffff); fixturePublishInfo(fixtureProperty(3),1,0xffff);
    assert(fixtureFrames.size()==sent); // Natural HighUpdate tail cannot repaint the factory two-row view.
    fixtureStopDirect(); fixtureFinish();
    puts("PASS shadow transaction: native order/generation/rows/title/header/focus/factory bytes/dirty commit");
}
static void fixtureGet() {
    fixtureInstallDirect(); fixtureDirectRecord();
    assert(publishHook(fixtureProperty(1),2,0,0,0x2244) && !fixtureCalls.back().shadow);
    assert(infoPublishHook(fixtureProperty(3),0,0x3344) && !fixtureCalls.back().shadow);
    fixtureFrames.clear(); fixtureCalls.clear(); fixtureSeed();
    assert(publishHook(fixtureProperty(1),0,0,1,0xffff));
    // Pending metadata must not make independently requested fields disagree with the last committed frame.
    strcpy(state.record.song,"Pending title"); strcpy(state.record.artist,"Pending artist");
    unsigned char before[1024]; memcpy(before,fixtureOwnerBytes,sizeof(before));
    for (unsigned first=0;first!=4;++first) {
        for (unsigned selector=0;selector!=9;++selector) {
            const bool valid=selector!=1 && selector<=7;
            assert((publishHook(fixtureProperty(1),first,selector,0,0x2244)!=0)==valid);
            const Publication& f=fixtureCalls.back();
            assert(f.shadow && f.first==first && f.selector==selector && f.mode==0 && f.destination==0x2244);
            assert(f.title==fixtureExpected("Трек"));
        }
    }
    assert(!publishHook(fixtureProperty(1),4,0,0,0x2244) && fixtureCalls.back().shadow);
    assert(infoPublishHook(fixtureProperty(3),0,0x3344));
    assert(fixtureCalls.back().shadow && fixtureCalls.back().artist==fixtureExpected("Автор"));
    assert(fixtureCalls.back().destination==0x3344 && !memcmp(before,fixtureOwnerBytes,sizeof(before)));
    ++*((unsigned char*)fixtureProperty(3)+0x28);
    assert(infoPublishHook(fixtureProperty(3),0,0x3344) && !fixtureCalls.back().shadow);
    assert(!publishHook(fixtureProperty(1),3,6,0,0x2244) && !fixtureCalls.back().shadow);
    *((unsigned char*)fixtureProperty(3)+0x28)=state.presentedGeneration;
    assert(infoPublishHook(fixtureProperty(3),1,0xffff) && !fixtureCalls.back().shadow);
    // Only Position publication is hooked; public Get still reaches the unchanged error-11 receiver.
    assert(state.positionVtable[8]==0x02359b54);
    fixtureStopDirect(); fixtureFinish();
    puts("PASS committed Get frame: row ID/selector/requestor/generation forwarding and no dirty mutation");
}
static int fixtureSubscribePosition(unsigned destination) {
    // Build66 CommonNotification's 01F73F44 calls virtual+30 in mode0, even for a duplicate subscription.
    assert(fixtureWord((uintptr_t)fixtureOwner+0x374)==(uint32_t)(uintptr_t)(state.positionVtable+2));
    assert(state.positionVtable[14]==(uint32_t)(uintptr_t)&positionPublishHook);
    assert(state.positionVtable[8]==0x02359b54);
    return positionPublishHook(fixtureProperty(2),0,destination);
}
static void fixturePositionSubscription() {
    fixtureInstallDirect(); fixtureDirectRecord();
    assert(fixtureSubscribePosition(0x3344) && !fixtureCalls.back().shadow && fixtureCalls.back().count==2);
    fixtureFrames.clear(); fixtureCalls.clear();
    state.record.cursor=nbt::kNativeMediaCursorNext; state.record.cursorMs=fixtureNow;
    fixtureSeed(); assert(publishHook(fixtureProperty(1),0,0,1,0xffff));
    assert(state.frameActive);
    const unsigned generation=state.presentedGeneration, updates=fixtureUpdates;
    // Native subscription bookkeeping may already have changed, but no factory presentation field may change.
    for (unsigned i=0;i!=3;++i) fixtureSubs[i][1]=0x3344;
    dirty(fixtureProperty(3),1); // Mixed original/shadow dirty values expose an accidental mode0 dirty commit.
    unsigned char before[1024], rows[48]; memcpy(before,fixtureOwnerBytes,sizeof(before)); memcpy(rows,fixtureRows,48);
    for (unsigned request=0;request!=2;++request) {
        const unsigned frames=fixtureFrames.size();
        assert(fixtureSubscribePosition(0x3344));
        const Publication& p=fixtureCalls.back();
        assert(p.kind==2 && p.shadow && p.mode==0 && p.destination==0x3344 && p.dirtyBefore);
        assert(p.count==3 && p.focus==3 && p.generation==generation);
        assert(fixtureFrames.size()==frames+1 && fixtureUpdates==updates && !state.pending);
        assert(!memcmp(before,fixtureOwnerBytes,sizeof(before)) && !memcmp(rows,fixtureRows,48));
    }
    ++*((unsigned char*)fixtureProperty(2)+0x28);
    assert(fixtureSubscribePosition(0x3344) && !fixtureCalls.back().shadow && fixtureCalls.back().count==2);
    *((unsigned char*)fixtureProperty(2)+0x28)=(unsigned char)generation;
    assert(positionPublishHook(fixtureProperty(2),1,0xffff) && !fixtureCalls.back().shadow);
    state.record.active=false;
    assert(fixtureSubscribePosition(0x3344) && !fixtureCalls.back().shadow && fixtureCalls.back().count==2);
    fixtureStopDirect(); fixtureFinish();
    puts("PASS Position initial/resubscription: cached count/generation/focus, unchanged bytes/dirty, native public Get receiver");
}
static void fixtureSubscribers(bool mismatch) {
    fixtureInstallDirect(); fixtureDirectRecord(); fixtureSeed();
    if (mismatch) fixtureSubs[2][1]++;
    else fixtureSubCount[1]=0;
    assert(publishHook(fixtureProperty(1),0,0,1,0xffff));
    assert(!state.frameActive && fixtureCalls.size()==1 && !fixtureCalls[0].shadow);
    assert(fixtureEvents.empty() && !state.recovering);
    fixtureStopDirect(); fixtureFinish();
    puts("PASS subscriber preflight: native fallback, no synthetic frame or retry loop");
}
static void fixtureFailure(unsigned kind, bool inTimer, bool serialization) {
    fixtureInstallDirect(); fixtureDirectRecord(); wake(); assert(fixturePump()); assert(state.frameActive);
    const unsigned failureStart=fixtureFrames.size();
    const unsigned failedGeneration=(unsigned char)(byte(fixtureProperty(1),0x34)+1);
    state.record.cursor=nbt::kNativeMediaCursorPrev; state.record.cursorMs=fixtureNow;
    fixtureFailKind=kind; fixtureFailRemaining=1;
    fixtureSerializeFailure=serialization;
    if (inTimer) { wake(); assert(fixturePump()); }
    else {
        for (unsigned k=1;k!=4;++k) dirty(fixtureProperty(k),1);
        assert(fixtureHighUpdate());
    }
    assert(!state.frameActive && state.recovering && state.pending);
    assert(fixtureFailRemaining==0);
    // The native fallback is immediate, but complete repair must use one fresh native transaction.
    const unsigned updates=fixtureUpdates;
    assert(fixturePump());
    assert(fixtureUpdates==updates+1 && !state.recovering && !state.pending && !state.frameActive);
    assert(!fixturePump());
    unsigned nativeFresh=0;
    for (unsigned i=failureStart;i!=fixtureFrames.size();++i)
        if (!fixtureFrames[i].shadow && fixtureFrames[i].generation!=failedGeneration) ++nativeFresh;
    assert(nativeFresh==3);
    assert(!publishHook(fixtureProperty(1),3,0,0,0x2244) && !fixtureCalls.back().shadow);
    // A later external selection change can reactivate after the one-shot native repair.
    state.record.cursor=nbt::kNativeMediaCursorNext; state.record.cursorMs=fixtureNow;
    const unsigned start=fixtureFrames.size(); wake(); assert(fixturePump());
    assert(state.frameActive); fixtureAssertFrame(start,3,byte(fixtureProperty(1),0x34));
    fixtureStopDirect(); fixtureFinish();
    puts("PASS partial failure: no commit/cache, exactly one fresh native recovery, later reactivation");
}
static void fixtureCursor() {
    fixtureInstallDirect(); fixtureDirectRecord();
    const nbt::NativeMediaCursor values[]={nbt::kNativeMediaCursorNone,nbt::kNativeMediaCursorPrev,
        nbt::kNativeMediaCursorNext,nbt::kNativeMediaCursorNone};
    unsigned previous=byte(fixtureProperty(1),0x34);
    for (unsigned i=0;i!=4;++i) {
        state.record.cursor=values[i]; state.record.cursorMs=fixtureNow;
        const unsigned start=fixtureFrames.size(); wake(); assert(fixturePump());
        const unsigned gen=byte(fixtureProperty(1),0x34);
        assert(gen==(unsigned char)(previous+1)); previous=gen;
        fixtureAssertFrame(start,values[i]==nbt::kNativeMediaCursorPrev ? 1 : 3,gen);
        assert(!state.pending && !fixturePump());
    }
    fixtureStopDirect(); fixtureFinish();
    puts("PASS cursor-only changes: unchanged metadata, fresh native generation and no timer hot loop");
}
static void fixtureLifetime(const char* which) {
    fixtureInstallDirect(); fixtureDirectRecord(); wake(); assert(fixturePump()); assert(state.frameActive);
    if (!strcmp(which,"pause")) state.record.active=false;
    else if (!strcmp(which,"stale")) fixtureNow+=nbt::kMediaStaleMs;
    else if (!strcmp(which,"cursor-retained")) {
        state.record.cursor=nbt::kNativeMediaCursorNext; state.record.cursorMs=fixtureNow;
        wake(); assert(fixturePump()); assert(state.presented.cursor==nbt::kNativeMediaCursorNext);
        fixtureNow+=4001; state.record.updatedMs=fixtureNow; // The popup closed; playback and its remembered button remain.
    }
    const unsigned start=fixtureFrames.size(); wake(); assert(fixturePump());
    if (!strcmp(which,"cursor-retained")) {
        assert(state.frameActive && state.presented.cursor==nbt::kNativeMediaCursorNext);
        fixtureAssertFrame(start,3,byte(fixtureProperty(1),0x34));
    } else {
        assert(!state.frameActive && fixtureText(0,"Factory title") && fixtureText(1,"Factory artist"));
        for (unsigned i=start;i!=fixtureFrames.size();++i) assert(!fixtureFrames[i].shadow);
    }
    assert(!state.pending); fixtureStopDirect(); fixtureFinish();
    puts("PASS pause/stale/closed popup: conditional factory restore and remembered button");
}
static void fixtureUnhook(unsigned lostKind) {
    fixtureInstallDirect(); fixtureDirectRecord(); wake(); assert(fixturePump());
    const uint32_t foreign=0x44556677;
    memcpy(fixtureProperty(lostKind),&foreign,4);
    fixtureStopDirect();
    const uint32_t native[]={0,0x02ec0178,0x02ec0028,0x02ec0068};
    for (unsigned kind=1;kind!=4;++kind)
        assert(fixtureWord((uintptr_t)fixtureProperty(kind))==(kind==lostKind ? foreign : native[kind]));
    fixtureFinish(); puts("PASS independent vtable ownership: restore only our remaining hook");
}
static void fixtureSnapshotFailure() {
    fixtureInstallDirect(); fixtureDirectRecord();
    pthread_mutex_lock(&state.mutex); wake(); assert(fixturePump());
    // No committed frame exists yet: retry once after the native timer delay, never recursively.
    assert(fixtureUpdates==0 && state.pending && fixtureEvents.size()==1);
    assert(infoPublishHook(fixtureProperty(3),0,0x3344) && !fixtureCalls.back().shadow);
    pthread_mutex_unlock(&state.mutex);
    fixtureFrames.clear();
    assert(fixturePump()); assert(state.frameActive && !state.pending && fixtureEvents.empty());
    const unsigned updates=fixtureUpdates, frames=fixtureFrames.size();
    pthread_mutex_lock(&state.mutex);
    assert(infoPublishHook(fixtureProperty(3),0,0x3344) && fixtureCalls.back().shadow);
    assert(positionPublishHook(fixtureProperty(2),0,0x3344) && fixtureCalls.back().shadow);
    assert(fixtureCalls.back().focus==3);
    assert(publishHook(fixtureProperty(1),2,0,0,0x3344) && fixtureCalls.back().shadow);
    assert(fixtureCalls.back().title==fixtureExpected("Трек"));
    assert(fixtureUpdates==updates && state.pending && fixtureEvents.size()==1);
    // A native transaction can also coincide with the reader's lock, including its native tail.
    for (unsigned kind=1;kind!=4;++kind) dirty(fixtureProperty(kind),1);
    const unsigned cachedStart=fixtureFrames.size();
    assert(fixtureHighUpdate()); assert(state.frameActive);
    fixtureAssertFrame(cachedStart,3,byte(fixtureProperty(1),0x34));
    for (unsigned i=frames;i!=fixtureFrames.size();++i) assert(fixtureFrames[i].shadow);
    pthread_mutex_unlock(&state.mutex);
    const unsigned resumed=fixtureFrames.size(); assert(fixturePump());
    assert(fixtureUpdates==updates+2 && state.frameActive && !state.pending && fixtureEvents.empty());
    fixtureAssertFrame(resumed,3,byte(fixtureProperty(1),0x34));

    // Cached presentation is not a lease: its original TTL still expires while the reader is busy.
    fixtureNow=state.presented.updatedMs+nbt::kMediaStaleMs;
    pthread_mutex_lock(&state.mutex);
    assert(publishHook(fixtureProperty(1),2,0,0,0x3344) && !fixtureCalls.back().shadow);
    assert(positionPublishHook(fixtureProperty(2),0,0x3344) && !fixtureCalls.back().shadow);
    assert(infoPublishHook(fixtureProperty(3),0,0x3344) && !fixtureCalls.back().shadow);
    pthread_mutex_unlock(&state.mutex);
    assert(fixturePump()); assert(!state.frameActive && !state.pending);
    fixtureStopDirect(); fixtureFinish();
    puts("PASS busy snapshot: committed Get and broadcast frame without native flash; no cache before commit or after TTL");
}
static void fixtureOwnerMismatch() {
    fixtureInstallDirect(); fixtureDirectRecord(); wake(); assert(fixturePump());
    static unsigned char foreign[1024]; fixtureGlobalOwner=foreign;
    const unsigned sent=fixtureFrames.size(); wake(); assert(fixturePump());
    assert(!state.frameActive && !state.installed && fixtureFrames.size()==sent && !state.pending);
    // No cached native text is written through a stale owner. Discarded row references remain factory-owned.
    fixtureGlobalOwner=fixtureOwner;
    fixtureFactoryRow(0,"Factory title"); fixtureFactoryRow(1,"Factory artist");
    assert(fixtureStrings.size()==4); fixtureFinish(); puts("PASS owner replacement: no cached frame or stale native write");
}
static void fixtureRecoveryContext(const char* which) {
    fixtureInstallDirect(); fixtureDirectRecord(); wake(); assert(fixturePump()); assert(state.frameActive);
    fixtureFailKind=2; fixtureFailRemaining=1;
    state.record.cursor=nbt::kNativeMediaCursorPrev; state.record.cursorMs=fixtureNow;
    wake(); assert(fixturePump()); assert(state.recovering && state.pending && !state.frameActive);
    const unsigned frames=fixtureFrames.size(), updates=fixtureUpdates;
    const uint32_t window=fixtureWindow;
    const bool ownerLost=!strcmp(which,"recovery-owner");
    const bool stopping=!strcmp(which,"recovery-context-stop");
    static unsigned char foreign[1024];
    if (ownerLost) fixtureGlobalOwner=foreign;
    else fixtureWindow=0xc43d;
    if (stopping) state.stopping=1;
    assert(fixturePump());
    assert(!state.frameActive && !state.recovering && !state.pending && fixtureEvents.empty());
    assert(fixtureFrames.size()==frames && fixtureUpdates==updates && !fixturePump());
    if (ownerLost) {
        assert(!state.installed);
        // The previous native object was removed. Recreate its fixture storage, never restore through the lost owner.
        fixtureGlobalOwner=fixtureOwner;
        const uint32_t content=0x02ec0178, info=0x02ec0068, position=0x02ec0028;
        memcpy(fixtureOwnerBytes+0x33c,&content,4); memcpy(fixtureOwnerBytes+0x2ec,&info,4);
        memcpy(fixtureOwnerBytes+0x374,&position,4);
        fixtureFactoryRow(0,"Factory title"); fixtureFactoryRow(1,"Factory artist");
        assert(fixtureStrings.size()==4); fixtureFinish();
    } else if (stopping) {
        assert(state.stopped && !state.installed && !state.saved);
        fixtureWindow=window; fixtureCurrent=fixtureApp; fixtureSetInput(false);
        assert(start(0)); // A completed cleanup must not leave a pending retry blocking restart.
        for (unsigned i=0;i!=30;++i) { fixturePump(); usleep(1000); }
        stop(); assert(state.stopped && !state.installed && !state.pending);
        fixtureFinish();
    } else {
        fixtureWindow=window; fixtureStopDirect(); fixtureFinish();
    }
    puts("PASS recovery context loss: discard stale repair, no repeated timer, cleanup/restart remains available");
}
static unsigned fixtureReaderInput(uint32_t now, uint32_t eventMs, nbt::NativeMediaCursor cursor) {
    // Publish one complete input atomically; a separate fixtureSetInput()/cursor assignment could expose half a record.
    pthread_mutex_lock(&fixtureMutex);
    fixtureInput.active=true; fixtureInput.updatedMs=now; fixtureInput.writerPid=77;
    fixtureInput.cursor=cursor; fixtureInput.cursorMs=eventMs; ++fixtureInput.sequence;
    strcpy(fixtureInput.song,"Трек"); strcpy(fixtureInput.artist,"Автор");
    __atomic_store_n(&fixtureNow,now,__ATOMIC_RELEASE);
    const unsigned sequence=fixtureInput.sequence;
    pthread_mutex_unlock(&fixtureMutex);
    return sequence;
}
static void fixtureWaitReader(unsigned sequence, unsigned minimumReads, nbt::NativeMediaCursor effective) {
    unsigned completedAfter=0;
    for (unsigned i=0;i!=1000;++i) {
        fixturePump();
        pthread_mutex_lock(&state.mutex);
        const bool consumed=state.record.sequence==sequence && state.record.cursor==effective;
        pthread_mutex_unlock(&state.mutex);
        const unsigned reads=__atomic_load_n(&fixtureReads,__ATOMIC_ACQUIRE);
        if (consumed && !completedAfter) completedAfter=reads+2;
        // Wait for later passes after observing this exact record, even if old reads raced ahead before publication.
        if (consumed && reads>=minimumReads && reads>=completedAfter &&
            state.frameActive && !state.pending) return;
        usleep(1000);
    }
    assert(false);
}
static void fixtureReaderRefresh(bool selected) {
    const nbt::NativeMediaCursor cursor=selected ? nbt::kNativeMediaCursorNext : nbt::kNativeMediaCursorNone;
    unsigned sequence=fixtureReaderInput(1000,1000,cursor);
    assert(start(0)); fixtureWaitReader(sequence,3,nbt::kNativeMediaCursorNext);
    assert(state.presented.cursor==nbt::kNativeMediaCursorNext && state.presented.cursorMs==1000);

    unsigned reads=__atomic_load_n(&fixtureReads,__ATOMIC_ACQUIRE);
    unsigned updates=fixtureUpdates, frames=fixtureFrames.size();
    const unsigned previous=byte(fixtureProperty(1),0x34);
    // Repeating the same direction reopens the rich frame with its remembered selectable button.
    sequence=fixtureReaderInput(3500,3500,cursor);
    fixtureWaitReader(sequence,reads+3,nbt::kNativeMediaCursorNext); // No manual wake: only production reader() may schedule this transaction.
    assert(fixtureUpdates==updates+1 && state.presented.cursorMs==3500);
    const unsigned generation=byte(fixtureProperty(1),0x34);
    assert(generation==(unsigned char)(previous+1));
    fixtureAssertFrame(frames,3,generation);

    reads=__atomic_load_n(&fixtureReads,__ATOMIC_ACQUIRE);
    updates=fixtureUpdates; frames=fixtureFrames.size();
    sequence=fixtureReaderInput(6000,3500,cursor); // Playback heartbeat only: it must not reopen the popup.
    fixtureWaitReader(sequence,reads+3,nbt::kNativeMediaCursorNext);
    assert(fixtureUpdates==updates && fixtureFrames.size()==frames && state.presented.cursorMs==3500);

    reads=__atomic_load_n(&fixtureReads,__ATOMIC_ACQUIRE);
    sequence=fixtureReaderInput(7501,3500,cursor); // Popup timeout neither clears the button nor redraws.
    fixtureWaitReader(sequence,reads+3,nbt::kNativeMediaCursorNext);
    assert(fixtureUpdates==updates && fixtureFrames.size()==frames);
    assert(state.presented.cursor==nbt::kNativeMediaCursorNext && state.presented.cursorMs==3500);
    stop(); assert(state.stopped && !state.installed && !state.pending);
    assert(fixtureText(0,"Factory title") && fixtureText(1,"Factory artist"));
    fixtureFinish();
    puts("PASS actual reader: event reopens remembered button; heartbeat and popup timeout do not redraw");
}
static void fixtureIncompleteReader(bool validClear) {
    unsigned sequence=fixtureReaderInput(1000,1000,nbt::kNativeMediaCursorNone);
    assert(start(0)); fixtureWaitReader(sequence,3,nbt::kNativeMediaCursorNext);
    const unsigned frames=fixtureFrames.size(), updates=fixtureUpdates;
    unsigned reads=__atomic_load_n(&fixtureReads,__ATOMIC_ACQUIRE);
    __atomic_store_n(&fixtureInputValid,0,__ATOMIC_RELEASE);
    __atomic_store_n(&fixtureNow,1100,__ATOMIC_RELEASE);
    fixtureWaitReader(sequence,reads+4,nbt::kNativeMediaCursorNext);
    assert(state.frameActive && state.record.updatedMs==1000 && fixtureUpdates==updates && fixtureFrames.size()==frames);
    // The next complete title replaces the committed view without an intervening native placeholder.
    pthread_mutex_lock(&fixtureMutex);
    fixtureInput.updatedMs=1200; ++fixtureInput.sequence; sequence=fixtureInput.sequence;
    strcpy(fixtureInput.song,"Новый трек"); strcpy(fixtureInput.artist,"Новый автор");
    __atomic_store_n(&fixtureNow,1200,__ATOMIC_RELEASE);
    __atomic_store_n(&fixtureInputValid,1,__ATOMIC_RELEASE);
    pthread_mutex_unlock(&fixtureMutex);
    fixtureWaitReader(sequence,reads+6,nbt::kNativeMediaCursorNext);
    assert(!strcmp(state.presented.song,"Новый трек"));
    for(unsigned i=frames;i!=fixtureFrames.size();++i) assert(fixtureFrames[i].shadow);
    // Rejected reads cannot prolong freshness; a long interruption still restores factory rows.
    if (validClear) {
        fixtureSetInput(false);
        __atomic_store_n(&fixtureNow,1300,__ATOMIC_RELEASE);
    } else {
        __atomic_store_n(&fixtureInputValid,0,__ATOMIC_RELEASE);
        __atomic_store_n(&fixtureNow,1200+nbt::kMediaStaleMs,__ATOMIC_RELEASE);
    }
    for(unsigned i=0;i!=1000 && state.frameActive;++i) { fixturePump(); usleep(1000); }
    assert(!state.frameActive && fixtureText(0,"Factory title") && fixtureText(1,"Factory artist"));
    stop(); assert(state.stopped && !state.installed && !state.pending);
    fixtureFinish();
    puts("PASS actual reader: incomplete records retain frame without renewing TTL; fresh track updates directly; valid withdrawal remains immediate");
}
static unsigned fixtureCompatibilityRevision() {
    char summary[192]; return nbt::native_media::compatibilityStatus(summary,sizeof(summary));
}
static void fixtureWaitCompatibility(unsigned minimum) {
    for (unsigned i=0;i!=1000;++i) {
        fixturePump();
        if (fixtureCompatibilityRevision()>=minimum && !state.checkRequested) return;
        usleep(1000);
    }
    assert(false);
}
static void fixtureUnsupportedProfile(const char* which) {
    fixtureIdentityKind=!strcmp(which,"unknown-version") ? 1 : !strcmp(which,"unknown-hash") ? 2 : 4;
    fixtureSetInput(true); assert(start(0)); fixtureWaitCompatibility(2);
    assert(state.running && !state.profile && !state.unit && !state.installed && !state.pending);
    assert(fixtureFrames.empty() && fixtureUpdates==0 && fixtureRuntimeChecks==(fixtureIdentityKind==4?1u:0u));
    assert(fixturePrivateReads==0);
    assert(fixtureReport.find("runtime_verified=0")!=std::string::npos);
    if (fixtureIdentityKind!=4) assert(fixtureReport.find("matched_profile=none")!=std::string::npos);
    // Even a later successful diagnostic must not change the hook's startup decision.
    const unsigned revision=fixtureCompatibilityRevision(); fixtureIdentityKind=0;
    assert(requestCompatibilityCheck()); fixtureWaitCompatibility(revision+2);
    assert(!state.profile && !state.unit && !state.installed && !state.pending && fixtureFrames.empty());
    assert(fixtureReport.find("runtime_verified=1")!=std::string::npos);
    assert(fixtureReport.find("diagnostic-only; hook state unchanged")!=std::string::npos);
    assert(fixtureIdentityChecks==2);
    stop(); assert(state.stopped && !state.running); fixtureFinish();
    puts("PASS unknown fingerprint/version/binding: no private owner/timer/hook, later diagnostic cannot activate");
}
static void fixtureDiagnosticCheck() {
    fixtureSetInput(true); assert(start(0)); fixtureWait(true);
    for (unsigned i=0;i!=1000 && (state.pending || !state.frameActive);++i) {fixturePump();usleep(1000);}
    assert(state.installed && state.frameActive && !state.pending);
    const unsigned updates=fixtureUpdates, frames=fixtureFrames.size(), revision=fixtureCompatibilityRevision();
    unsigned char owner[1024];memcpy(owner,fixtureOwnerBytes,sizeof(owner));
    assert(requestCompatibilityCheck());fixtureWaitCompatibility(revision+2);
    assert(state.installed && state.frameActive && fixtureUpdates==updates && fixtureFrames.size()==frames);
    assert(!memcmp(owner,fixtureOwnerBytes,sizeof(owner)) && fixtureRuntimeChecks>=2);
    assert(fixtureReport.find("adapter_installed=1")!=std::string::npos);
    assert(fixtureIdentityChecks==1 && fixtureReport.find("identity_source=verified-startup")!=std::string::npos);
    stop();assert(state.stopped && !state.installed);fixtureFinish();
    puts("PASS diagnostic button: real worker report, no reinstall, generation, frame or key mutation");
}
static void fixtureCancelFingerprint() {
    fixtureIdentityKind=3;fixtureSetInput(true);assert(start(0));
    for (unsigned i=0;i!=1000 && !__atomic_load_n(&fixtureIdentityEntered,__ATOMIC_ACQUIRE);++i) usleep(1000);
    assert(fixtureIdentityEntered);stop();
    assert(state.stopped && !state.running && !state.installed && !state.pending && fixtureFrames.empty());
    fixtureFinish();puts("PASS fingerprint cancellation: app shutdown joins worker without private callbacks");
}
static void fixtureLifecycle() {
    fixtureSetInput(true);
    assert(start(0)); assert(start(0)); fixtureWait(true);
    assert(state.installed && state.saved && fixturePins == 1 && fixtureUpdates && fixturePublishes);
    stop();
    assert(!state.running && !state.installed && !state.pending && state.stopped && !state.saved);
    assert(fixtureText(0,"Factory title") && fixtureText(1,"Factory artist"));
    assert(fixtureWord((uintptr_t)fixtureOwner + 0x33c)==0x02ec0178);
    assert(fixtureWord((uintptr_t)fixtureOwner + 0x2ec)==0x02ec0068);
    assert(fixtureWord((uintptr_t)fixtureOwner + 0x374)==0x02ec0028);
    assert(fixtureStrings.size()==4); // Only two native rows and two Info strings retain references.
    stop(); // Idempotent.

    assert(start(0)); fixtureWait(true);
    fixtureFactoryRow(0,"OEM replacement");
    stop();
    assert(fixtureText(0,"OEM replacement") && fixtureText(1,"Factory artist"));
    assert(fixtureStrings.size()==4);

    assert(start(0)); fixtureWait(true);
    fixtureFactoryRow(1,"OEM artist");
    // Another update recaches only the changed factory row, not our title as an original.
    fixtureCurrent=fixtureIC; assert(setTexts(fixtureOwner,&fixtureInput)); fixtureCurrent=fixtureApp;
    stop();
    assert(fixtureText(0,"OEM replacement") && fixtureText(1,"OEM artist"));
    assert(fixtureStrings.size()==4);

    assert(start(0)); fixtureWait(true);
    fixturePumpEnabled=false; wake(); stop();
    assert(!state.running && state.stopping && !state.stopped && state.pending);
    assert(!start(0)); // A queued old cleanup must finish before a new lifecycle starts.
    fixturePumpEnabled=true; while(fixturePump()) {}
    assert(state.stopped && !state.installed && !state.pending);
    assert(fixtureWord((uintptr_t)fixtureOwner+0x33c)==0x02ec0178 &&
           fixtureWord((uintptr_t)fixtureOwner+0x2ec)==0x02ec0068 &&
           fixtureWord((uintptr_t)fixtureOwner+0x374)==0x02ec0028);
    fixtureSetInput(false);
    assert(start(0));
    for(unsigned i=0;i!=30;++i) { fixturePump(); usleep(1000); }
    assert(fixtureText(0,"OEM replacement") && fixtureText(1,"OEM artist"));
    stop(); assert(fixtureStrings.size()==4 && fixturePins==1);
    fixtureFinish();
    puts("PASS actual hook lifecycle: install, stop/unhook, restart, queued cleanup and per-row restore");
}
int main(int argc, char** argv) {
    fixtureWindow=argc>2 ? 0xc491 : 0xc495;
    fixtureInit(); const char* test=argc>1 ? argv[1] : "lifecycle";
    if (!strcmp(test,"lifecycle")) fixtureLifecycle();
    else if (!strcmp(test,"unknown-version") || !strcmp(test,"unknown-hash") || !strcmp(test,"unbound-process")) fixtureUnsupportedProfile(test);
    else if (!strcmp(test,"diagnostic")) fixtureDiagnosticCheck();
    else if (!strcmp(test,"cancel-fingerprint")) fixtureCancelFingerprint();
    else if (!strcmp(test,"reader-next")) fixtureReaderRefresh(true);
    else if (!strcmp(test,"reader-none")) fixtureReaderRefresh(false);
    else if (!strcmp(test,"reader-incomplete")) fixtureIncompleteReader(false);
    else if (!strcmp(test,"reader-incomplete-clear")) fixtureIncompleteReader(true);
    else if (!strcmp(test,"transaction")) fixtureTransaction();
    else if (!strcmp(test,"get")) fixtureGet();
    else if (!strcmp(test,"subscription")) fixturePositionSubscription();
    else if (!strcmp(test,"empty")) fixtureSubscribers(false);
    else if (!strcmp(test,"mismatch")) fixtureSubscribers(true);
    else if (!strcmp(test,"cursor")) fixtureCursor();
    else if (!strcmp(test,"pause") || !strcmp(test,"stale") || !strcmp(test,"cursor-retained")) fixtureLifetime(test);
    else if (!strcmp(test,"unhook-content")) fixtureUnhook(1);
    else if (!strcmp(test,"unhook-position")) fixtureUnhook(2);
    else if (!strcmp(test,"unhook-info")) fixtureUnhook(3);
    else if (!strcmp(test,"snapshot")) fixtureSnapshotFailure();
    else if (!strcmp(test,"owner")) fixtureOwnerMismatch();
    else if (!strncmp(test,"recovery-",9)) fixtureRecoveryContext(test);
    else if (!strncmp(test,"failure",7)) fixtureFailure((unsigned)(test[7]-'0'),strchr(test+8,'t')!=0,strchr(test+8,'s')!=0);
    else assert(false);
}
'''

compiler = shutil.which("g++") or shutil.which("clang++")
assert compiler, "A POSIX C++ compiler is required"
with tempfile.TemporaryDirectory(prefix="native-hook-lifecycle-") as temp:
    cpp, exe = Path(temp) / "lifecycle.cpp", Path(temp) / "lifecycle"
    cpp.write_text(prefix + source + suffix)
    subprocess.run([compiler, "-std=c++98", "-Wall", "-pthread", "-I" + str(ROOT / "src"),
                    "-I" + str(ROOT / "include"), "-I" + str(ROOT / "rhmiapp"), str(cpp), "-o", str(exe)], check=True)
    cases = ["unknown-version","unknown-hash","unbound-process","diagnostic","cancel-fingerprint","lifecycle", "reader-next", "reader-none", "reader-incomplete", "reader-incomplete-clear", "transaction", "get", "subscription", "empty", "mismatch", "cursor", "pause", "stale",
             "cursor-retained", "unhook-content", "unhook-position", "unhook-info", "snapshot", "owner"]
    cases += ["recovery-context", "recovery-context-stop", "recovery-owner"]
    cases += [f"failure{stage}{where}" for stage in (1, 2, 3) for where in ("", "t")]
    cases += [f"failure{stage}{where}s" for stage in (1, 2, 3) for where in ("", "t")]
    failures = []
    for window in ([], ["external-media"]):
        for case in cases:
            print(f"Testing {case} in {'C491' if window else 'C495'}", flush=True)
            result = subprocess.run([str(exe), case, *window], timeout=10)
            if result.returncode:
                failures.append((case, "C491" if window else "C495", result.returncode))
    assert not failures, failures
