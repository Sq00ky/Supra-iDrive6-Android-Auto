// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
// Reproduce the car's QNX named executable while reading real bytes through the normal reader.
#ifndef S_ISNAM
#define S_IFNAM 0050000
#define S_ISNAM(mode) (((mode) & S_IFMT) == S_IFNAM)
#endif
static mode_t fixtureMode=0;
static unsigned fixtureStatCalls=0;
static bool fixtureStatFailure=false;
static int fingerprintStat(int fd,struct stat* info) {
    if (fixtureStatFailure) { errno=EOVERFLOW; return -1; }
    const int result=::fstat(fd,info);
    if (fixtureMode && result==0) { info->st_mode=fixtureMode; ++fixtureStatCalls; }
    return result;
}
#define fstat fingerprintStat
#include "../rhmiapp/native_media_compatibility.hpp"
#undef fstat
#include <assert.h>
#include <stdlib.h>
#include <map>

using namespace nbt::native_media;
struct Memory {
    std::map<uintptr_t,uint32_t> words;
    unsigned reads;
    Memory() : reads(0) {}
    static bool read(void* user,uintptr_t address,uint32_t& value) {
        Memory& m=*(Memory*)user; ++m.reads;
        if (!m.words.count(address)) return false;
        value=m.words[address]; return true;
    }
};

int main() {
    const Profile* p=selectProfile("NBTevo_N18032I","084d0d3236d69bcfbacb2cb73827e2e88c7559e1eca5c1c03ab952805f2b0748");
    assert(p==&kBuild66 && p->emulatorTested && p->vehicleConfirmed);
    assert(!selectProfile(0,p->hmiSha256) && !selectProfile(p->firmware,0));
    assert(!selectProfile("different firmware",p->hmiSha256));
    assert(!selectProfile(p->firmware,"different fingerprint"));
    uintptr_t exports[kExportCount]; Memory memory; char reason[96];
    for (unsigned i=0;i<kExportCount;++i) exports[i]=p->exports[i].expected?p->exports[i].expected:0x100000+i*0x100;
    for (size_t i=0;i<p->anchorCount;++i) memory.words[p->anchors[i].address]=p->anchors[i].value;
    memory.words[exports[kUnitCtor]+p->layout.unitCtorDispatcherCheck]=p->layout.unitCtorInstruction;
    assert(!validateProfile(0,exports,&Memory::read,&memory,reason) && !memory.reads);
    Profile unverified=*p; unverified.emulatorTested=unverified.vehicleConfirmed=false;
    assert(!validateProfile(&unverified,exports,&Memory::read,&memory,reason) && !memory.reads);
    unverified=*p; unverified.abi="different-ABI";
    assert(!validateProfile(&unverified,exports,&Memory::read,&memory,reason) && !memory.reads);
    for (unsigned i=0;i<kExportCount;++i) {
        const uintptr_t saved=exports[i]; exports[i]=0;
        assert(!validateProfile(p,exports,&Memory::read,&memory,reason) && !memory.reads);
        exports[i]=saved;
        if (p->exports[i].expected) {
            ++exports[i]; assert(!validateProfile(p,exports,&Memory::read,&memory,reason) && !memory.reads); exports[i]=saved;
        }
    }
    assert(validateProfile(p,exports,&Memory::read,&memory,reason));
    assert(memory.reads==p->anchorCount+1);
    for (size_t i=0;i<p->anchorCount;++i) {
        memory.reads=0; ++memory.words[p->anchors[i].address];
        assert(!validateProfile(p,exports,&Memory::read,&memory,reason) && memory.reads==i+1);
        --memory.words[p->anchors[i].address];
    }
    memory.words.erase(p->anchors[0].address); memory.reads=0;
    assert(!validateProfile(p,exports,&Memory::read,&memory,reason) && memory.reads==1);

    compatibility::Identity identity;
    assert(!compatibility::runtimeCheck(identity,p,exports,reason)); // No binding to the running ELF: no private reads.
    identity.processBound=true;
    assert(!compatibility::runtimeCheck(identity,p,exports,reason)); // Bound module, unsupported architecture.
    const std::string report=compatibility::report(identity,0,exports,false,"unknown firmware");
    assert(report.find("matched_profile=none")!=std::string::npos);
    assert(report.find("candidate_only=1")!=std::string::npos);
    assert(report.find("emulator=1 vehicle=1")!=std::string::npos);

    char name[]="/tmp/nbt-media-identity-XXXXXX"; const int fd=mkstemp(name); assert(fd>=0);
    char digest[65],error[128];
    assert(compatibility::fingerprint(name,digest,error,0,false));
    assert(!strcmp(digest,"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    assert(write(fd,"abc",3)==3);
    assert(compatibility::fingerprint(name,digest,error,0,false));
    assert(!strcmp(digest,"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    fixtureMode=S_IFNAM|0775; fixtureStatCalls=0;
    assert(compatibility::fingerprint(name,digest,error,0,false));
    assert(!strcmp(digest,"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    assert(fixtureStatCalls==2); // Both pre/post metadata checks still run for named executables.
    const mode_t rejected[]={S_IFNAM|0644,S_IFDIR|0775,S_IFIFO|0775,S_IFCHR|0775,S_IFBLK|0775,S_IFSOCK|0775};
    for (unsigned i=0;i<sizeof(rejected)/sizeof(*rejected);++i) {
        fixtureMode=rejected[i];
        assert(!compatibility::fingerprint(name,digest,error,0,false) && !digest[0]);
    }
    fixtureMode=S_IFNAM|0775;
    assert(ftruncate(fd,0)==0 && lseek(fd,0,SEEK_SET)==0);
    assert(!compatibility::fingerprint(name,digest,error,0,false) && !digest[0]);
    assert(write(fd,"abc",3)==3);
    fixtureMode=0;
    fixtureStatFailure=true;
    assert(!compatibility::fingerprint(name,digest,error,0,false) && !digest[0]);
    assert(strstr(error,"fstat failed")); fixtureStatFailure=false;
    // Exact car metadata size, real seekable bytes, and a synthetic ARM32 ELF header.
    assert(ftruncate(fd,56918708)==0 && lseek(fd,0,SEEK_SET)==0);
    const unsigned char elf[20]={0x7f,'E','L','F',1,1,1,0,0,0,0,0,0,0,0,0,2,0,40,0};
    assert(write(fd,elf,sizeof(elf))==(ssize_t)sizeof(elf));
    char regularDigest[65]; bool namedArm32=false;
    assert(compatibility::fingerprint(name,regularDigest,error));
    fixtureMode=S_IFNAM|0775; fixtureStatCalls=0;
    assert(compatibility::fingerprint(name,digest,error,0,true,&namedArm32));
    assert(namedArm32 && fixtureStatCalls==2 && !strcmp(digest,regularDigest));
    assert(!selectProfile(p->firmware,digest)); // Accepting a node type never makes an unknown hash a profile.
    assert(ftruncate(fd,256u*1024u*1024u+1)==0);
    assert(!compatibility::fingerprint(name,digest,error,0,false) && !digest[0]);
    fixtureMode=0;
    assert(ftruncate(fd,0)==0 && lseek(fd,0,SEEK_SET)==0 && write(fd,"abc",3)==3);
    assert(!compatibility::fingerprint(name,digest,error) && !digest[0]); // Text is not the supported ARM32 executable.
    bool arm32=true;
    assert(compatibility::fingerprint(name,digest,error,0,false,&arm32) && !arm32); // Unknown formats still get a reportable hash.
    assert(lseek(fd,128,SEEK_SET)==128);
    const uint32_t expectedWord=0x12345678; assert(write(fd,&expectedWord,sizeof(expectedWord))==(ssize_t)sizeof(expectedWord));
    uint32_t word=0; int ownFd=fd;
    assert(compatibility::processWord(&ownFd,128,word) && word==expectedWord);
    assert(!compatibility::processWord(&ownFd,256,word));
    volatile int stopping=1;
    assert(!compatibility::fingerprint(name,digest,error,&stopping,false) && !digest[0]);
    assert(ftruncate(fd,256u*1024u*1024u+1)==0);
    assert(!compatibility::fingerprint(name,digest,error,0,false) && !digest[0]);
    assert(ftruncate(fd,0)==0 && lseek(fd,0,SEEK_SET)==0);
    assert(write(fd,"NBTevo_N18032I\r\n",16)==16);
    char firmware[64]; assert(compatibility::readFirmware(name,firmware) && !strcmp(firmware,"NBTevo_N18032I"));
    assert(ftruncate(fd,0)==0 && lseek(fd,0,SEEK_SET)==0 && write(fd,"bad\nforged",10)==10);
    assert(!compatibility::readFirmware(name,firmware) && !firmware[0]);
    close(fd); unlink(name);
    assert(!compatibility::fingerprint(name,digest,error,0,false) && !digest[0]);
    puts("native media compatibility: exact identity, all runtime guards, no unknown private reads, SHA256 and cancellation PASS");
}
