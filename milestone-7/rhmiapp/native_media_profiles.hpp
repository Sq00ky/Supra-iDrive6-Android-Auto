// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef NBT_NATIVE_MEDIA_PROFILES_HPP
#define NBT_NATIVE_MEDIA_PROFILES_HPP

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace nbt { namespace native_media {

enum ExportId { kStringCtor, kStringCopy, kStringDtor, kStringEqual, kGetUnit,
    kMutexTake, kMutexGive, kNextNode, kSetTimer, kUnitCtor, kExportCount };
struct Export { const char* name; uintptr_t expected; };
struct Anchor { uintptr_t address; uint32_t value; };
struct Addresses {
    uintptr_t ownerGlobal, ownerType, appType, appMemberType;
    uintptr_t contentVtable, infoVtable, positionVtable;
    uintptr_t contentType, infoType, positionType;
    uintptr_t publishContent, publishInfo, publishPosition, receivePosition;
    uintptr_t setText, highUpdate, currentWindow;
};
// ARM32 high-list ABI: fixed native property sizes and virtual slots belong to this adapter family.
struct Layout {
    unsigned ownerSlot, content, info, position, rows, rowCount, rowSize, rowText;
    unsigned rowType, rowFormat2, rowFormat1, rowSymbol;
    unsigned dirty, subscribersBegin, subscribersEnd, contentGeneration, otherGeneration;
    unsigned appFromUnit, appHandle, appMemberType, audioMode, dispatcherFromUnit;
    unsigned dispatcherMutex, dispatcherHeader, dispatcherRoot, nodeBegin, nodeEnd;
    unsigned infoCount, infoFormat, infoColour, infoLabel, positionCount, positionOffset, positionFocus;
    unsigned unitCtorDispatcherCheck;
    uint32_t unitCtorInstruction;
    unsigned unitTimerSlot;
};
struct Profile {
    const char *id, *displayName, *firmware, *hmiSha256, *abi;
    unsigned supportTextId;
    const char* verifiedVehicle;
    bool emulatorTested, vehicleConfirmed;
    uint32_t noAudioWindow, btUnavailableWindow, browserAudioMode;
    Addresses address;
    Layout layout;
    const Export* exports;
    const Anchor* anchors;
    size_t anchorCount;
};

static const Export kBuild66Exports[kExportCount] = {
    {"_ZN10THmiStringItEC1EPKt", 0x006cee70},
    {"_ZN10THmiStringItEC1ERKS0_", 0x0022715c},
    {"_ZN10THmiStringItED1Ev", 0x00226e90},
    {"_ZeqItEbRK10THmiStringIT_ES4_", 0x002604fc},
    {"_Z14getHmiProcUnitv", 0}, {"_ZN8COSMutex4takeEv", 0}, {"_ZN8COSMutex4giveEv", 0},
    {"_ZSt18_Rb_tree_incrementPSt18_Rb_tree_node_base", 0},
    {"_ZN9CHmiTimer9_setTimerEPvM15CDelegateTargetFvjjtEjjt", 0},
    {"_ZN12CHmiProcUnitC1ER18CHmiMainDispatcherh", 0}
};
static const Anchor kBuild66Anchors[] = {
    {0x02941dec,0xe92d4800}, {0x02941df0,0xe28db004}, {0x02941e0c,0xe59cc040},
    {0x02ec01b8,0x0235b634}, {0x0235294c,0x038302c0}, {0x023528dc,0xeb17bd42},
    {0x0235a5a4,0xe3a03018}, {0x0235a5b8,0xe2800010}, {0x00f85b2c,0xe584581c},
    {0x00f830e0,0xe58543e8}, {0x00f83340,0xe30c1352}, {0x00f88550,0x02c8e9d0},
    {0x00f85adc,0xe30c1491}, {0x00f85afc,0xe58457a8}, {0x02359b6c,0xe92d48f0},
    {0x0235afc0,0xe92d49f0}, {0x0235afdc,0xe5d0204c}, {0x023528f4,0xeb001c9c},
    {0x0235290c,0xeb0021ab}, {0x02359fd4,0xe5933030}, {0x02ec0098,0x0235afc0},
    {0x02ec0058,0x02359b6c}, {0x02352868,0xebf08417}, {0x02352884,0xe2822001},
    {0x02352888,0xe5c320d8}
};
static const Profile kBuild66 = {
    "build66-high-arm32", "Build66", "NBTevo_N18032I",
    "084d0d3236d69bcfbacb2cb73827e2e88c7559e1eca5c1c03ab952805f2b0748", "high-arm32-v1",602,
    "BMW F30 / 6WA / HUD; owner accepted multimedia on 2026-11-26", true, true,
    0xc495, 0xc491, 20,
    {0x038302c0,0x02c98cd0,0x02c8e9d0,0x02c8f858,
     0x02ec0178,0x02ec0068,0x02ec0028,0x02ec0284,0x02ec02b4,0x02ec0250,
     0x0235b634,0x0235afc0,0x02359b6c,0x02359b54,0x0235a558,0x0235206c,0x00f83334},
    {0xd4,0x33c,0x2ec,0x374,0x30,0x2c,24,16,4,8,12,20,
     0x1a,0x1c,0x20,0x34,0x28,0x3e8,0x18,0x69c,0x6d8,0x2e8,
     0x18,4,0xc,0x14,0x18,0x2c,0x44,0x48,0x38,0x2a,0x2c,0x2e,0xa4,0xe58432e8,3},
    kBuild66Exports,kBuild66Anchors,sizeof(kBuild66Anchors)/sizeof(*kBuild66Anchors)
};
static const Profile* const kProfiles[] = { &kBuild66 };
static const size_t kProfileCount = sizeof(kProfiles)/sizeof(*kProfiles);

inline const Profile* selectProfile(const char* firmware, const char* sha256) {
    if (!firmware || !sha256) return 0;
    for (size_t i=0;i<kProfileCount;++i)
        if (!strcmp(firmware,kProfiles[i]->firmware) && !strcmp(sha256,kProfiles[i]->hmiSha256)) return kProfiles[i];
    return 0;
}

typedef bool (*ProbeWord)(void*, uintptr_t, uint32_t&);
inline bool validateProfile(const Profile* profile, const uintptr_t resolved[kExportCount],
                            ProbeWord probe, void* user, char reason[96]) {
    if (!profile) { snprintf(reason,96,"Unknown firmware/fingerprint; no private memory inspected"); return false; }
    if (!profile->emulatorTested && !profile->vehicleConfirmed) {
        snprintf(reason,96,"Profile recognized but its private adapter has not been verified"); return false;
    }
    if (!profile->abi || strcmp(profile->abi,"high-arm32-v1")) {
        snprintf(reason,96,"Profile requires an unsupported private adapter ABI"); return false;
    }
    if (!resolved || !probe) { snprintf(reason,96,"Runtime validation unavailable"); return false; }
    for (unsigned i=0;i<kExportCount;++i)
        if (!resolved[i] || (profile->exports[i].expected && resolved[i]!=profile->exports[i].expected)) {
            snprintf(reason,96,"Required export %u missing or mismatched",i); return false;
        }
    for (size_t i=0;i<profile->anchorCount;++i) {
        uint32_t value=0;
        if (!probe(user,profile->anchors[i].address,value) || value!=profile->anchors[i].value) {
            snprintf(reason,96,"Runtime anchor %u unreadable or mismatched",(unsigned)i); return false;
        }
    }
    uint32_t value=0;
    if (!probe(user,resolved[kUnitCtor]+profile->layout.unitCtorDispatcherCheck,value) ||
        value!=profile->layout.unitCtorInstruction) {
        snprintf(reason,96,"Processing-unit dispatcher layout mismatch"); return false;
    }
    snprintf(reason,96,"Known fingerprint and runtime ABI verified");
    return true;
}
} }
#endif
