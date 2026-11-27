// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef NBT_NATIVE_MEDIA_COMPATIBILITY_HPP
#define NBT_NATIVE_MEDIA_COMPATIBILITY_HPP

#include "native_media_profiles.hpp"
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string>

namespace nbt { namespace native_media { namespace compatibility {

struct Identity {
    char firmware[64], sha256[65], executable[512], error[128];
    bool processBound, arm32Executable;
    Identity() : processBound(false), arm32Executable(false) { firmware[0]=sha256[0]=executable[0]=error[0]=0; }
};
inline bool cancelled(const volatile int* stopping) {
    return stopping && __atomic_load_n(stopping,__ATOMIC_ACQUIRE);
}

// Use the existing system crypto implementation without making it a new PluginApp load-time dependency.
struct Digest {
    void *library, *context;
    void* (*create)();
    void (*destroy)(void*);
    const void* (*sha256)();
    int (*init)(void*, const void*, void*);
    int (*update)(void*, const void*, size_t);
    int (*finish)(void*, unsigned char*, unsigned*);
    Digest() : library(0), context(0), create(0), destroy(0), sha256(0), init(0), update(0), finish(0) {}
    bool open() {
        library=dlopen("libcrypto.so.2",RTLD_NOW|RTLD_LOCAL);
        if (!library) library=dlopen("/opt/mm/lib/libcrypto.so.2",RTLD_NOW|RTLD_LOCAL);
        if (!library) library=dlopen("libcrypto.so",RTLD_NOW|RTLD_LOCAL);
        if (!library) return false;
        create=(void*(*)())dlsym(library,"EVP_MD_CTX_create");
        destroy=(void(*)(void*))dlsym(library,"EVP_MD_CTX_destroy");
        if (!create || !destroy) {
            create=(void*(*)())dlsym(library,"EVP_MD_CTX_new");
            destroy=(void(*)(void*))dlsym(library,"EVP_MD_CTX_free");
        }
        sha256=(const void*(*)())dlsym(library,"EVP_sha256");
        init=(int(*)(void*,const void*,void*))dlsym(library,"EVP_DigestInit_ex");
        update=(int(*)(void*,const void*,size_t))dlsym(library,"EVP_DigestUpdate");
        finish=(int(*)(void*,unsigned char*,unsigned*))dlsym(library,"EVP_DigestFinal_ex");
        if (!create || !destroy || !sha256 || !init || !update || !finish) return false;
        const void* algorithm=sha256();
        if (!algorithm) return false;
        context=create();
        return context && init(context,algorithm,0)==1;
    }
    ~Digest() { if (context) destroy(context); if (library) dlclose(library); }
private:
    Digest(const Digest&);
    Digest& operator=(const Digest&);
};

inline bool fingerprint(const char* path, char out[65], char error[128],
                         const volatile int* stopping=0, bool requireHmiElf=true, bool* arm32=0) {
    out[0]=0;
    if (arm32) *arm32=false;
    int fd=::open(path,O_RDONLY|O_NONBLOCK);
    if (fd<0) { snprintf(error,128,"Executable cannot be opened (errno %d)",errno); return false; }
    struct stat before,after;
    if (fstat(fd,&before)!=0) {
        snprintf(error,128,"Executable fstat failed (errno %d)",errno); close(fd); return false;
    }
    bool fileType=S_ISREG(before.st_mode);
#ifdef S_ISNAM
    // The car exposes the loaded /opt/hmi/bin/zipped/HmiMain3D as a QNX named executable.
    fileType=fileType || (S_ISNAM(before.st_mode) && before.st_size>0 &&
                         (before.st_mode & (S_IXUSR|S_IXGRP|S_IXOTH)));
#endif
    bool ok=fileType && before.st_size>=0 && (uint64_t)before.st_size<=256u*1024u*1024u;
    if (!ok) {
        snprintf(error,128,"Unsupported executable metadata (mode 0%o, size %lld)",
                 (unsigned)before.st_mode,(long long)before.st_size);
        close(fd); return false;
    }
    if (ok && (requireHmiElf || arm32)) {
        unsigned char elf[20];
        const bool supported=read(fd,elf,sizeof(elf))==(ssize_t)sizeof(elf) && !memcmp(elf,"\177ELF",4) &&
            elf[4]==1 && elf[5]==1 && elf[6]==1 && elf[16]==2 && elf[17]==0 && elf[18]==40 && elf[19]==0;
        if (arm32) *arm32=supported;
        ok=(!requireHmiElf || supported) && lseek(fd,0,SEEK_SET)==0;
    }
    if (!ok || cancelled(stopping)) {
        close(fd); snprintf(error,128,"Unsupported ELF, seek failure or fingerprint cancelled"); return false;
    }
    Digest digest;
    if (!digest.open()) { close(fd); snprintf(error,128,"System crypto SHA256 provider unavailable"); return false; }
    char buffer[32768]; uint64_t total=0;
    while (ok && !cancelled(stopping)) {
        const ssize_t n=read(fd,buffer,sizeof(buffer));
        if (n<0 && errno==EINTR) continue;
        if (n<0) { ok=false; break; }
        if (!n) break;
        total+=(size_t)n;
        ok=total<=256u*1024u*1024u && digest.update(digest.context,buffer,(size_t)n)==1;
    }
    unsigned char bytes[32]; unsigned size=0;
    ok=ok && !cancelled(stopping) && fstat(fd,&after)==0 && before.st_size==after.st_size &&
       before.st_mtime==after.st_mtime && total==(uint64_t)before.st_size &&
       digest.finish(digest.context,bytes,&size)==1 && size==32;
    close(fd);
    if (!ok) { snprintf(error,128,"Fingerprint read failed, executable changed or operation cancelled"); return false; }
    static const char hex[]="0123456789abcdef";
    for (unsigned i=0;i<32;++i) { out[2*i]=hex[bytes[i]>>4]; out[2*i+1]=hex[bytes[i]&15]; }
    out[64]=0; error[0]=0;
    return true;
}

inline bool readFirmware(const char* path, char out[64]) {
    out[0]=0; FILE* f=fopen(path,"r");
    if (!f) return false;
    char text[66]; const size_t n=fread(text,1,sizeof(text),f); const bool ok=!ferror(f); fclose(f);
    if (!ok || !n || n>=sizeof(text)) return false;
    size_t size=n;
    while (size && (text[size-1]=='\r' || text[size-1]=='\n')) --size;
    if (!size || size>=64) return false;
    for (size_t i=0;i<size;++i) if ((unsigned char)text[i]<0x20 || (unsigned char)text[i]>0x7e) return false;
    memcpy(out,text,size); out[size]=0;
    return true;
}

inline Identity identify(uintptr_t getUnit, const volatile int* stopping) {
    Identity result;
    if (!readFirmware("/opt/sys/etc/nbt_version.txt",result.firmware))
        readFirmware("/etc/nbt_version.txt",result.firmware);
    Dl_info info;
    memset(&info,0,sizeof(info));
    if (getUnit && dladdr((void*)getUnit,&info) && info.dli_fname) {
        const char* base=strrchr(info.dli_fname,'/'); base=base ? base+1 : info.dli_fname;
        result.processBound=!strcmp(base,"HmiMain3D") && strlen(info.dli_fname)<sizeof(result.executable);
        if (result.processBound) snprintf(result.executable,sizeof(result.executable),"%s",info.dli_fname);
    }
    if (!result.executable[0]) {
        // Diagnostics only: a file at a customary path is not proof of the running executable's identity.
        const char* paths[]={"/opt/hmi/bin/zipped/HmiMain3D","/opt/hmi/bin/HmiMain3D"};
        for (unsigned i=0;i<2;++i)
            if (access(paths[i],R_OK)==0) { snprintf(result.executable,sizeof(result.executable),"%s",paths[i]); break; }
    }
    if (!result.executable[0]) snprintf(result.error,sizeof(result.error),"HmiMain3D executable unavailable");
    else fingerprint(result.executable,result.sha256,result.error,stopping,false,&result.arm32Executable);
    return result;
}

inline void resolveExports(uintptr_t out[kExportCount], const Profile* profile=0) {
    const Export* names=profile ? profile->exports : kBuild66Exports;
    for (unsigned i=0;i<kExportCount;++i) out[i]=(uintptr_t)dlsym(RTLD_DEFAULT,names[i].name);
}
inline bool processWord(void* user, uintptr_t address, uint32_t& value) {
    const int fd=*(int*)user; // This descriptor is local to one diagnostic pass.
    if (lseek(fd,(off_t)address,SEEK_SET)!=(off_t)address) return false;
    ssize_t n;
    do { n=read(fd,&value,sizeof(value)); } while (n<0 && errno==EINTR);
    return n==(ssize_t)sizeof(value);
}
inline bool runtimeCheck(const Identity& identity, const Profile* profile,
                         const uintptr_t exports[kExportCount], char reason[96]) {
    if (!profile) { snprintf(reason,96,"Unknown firmware/fingerprint; adapter disabled"); return false; }
    if (!identity.processBound) { snprintf(reason,96,"Running executable binding not verified"); return false; }
    if (!identity.arm32Executable) { snprintf(reason,96,"Executable is not the supported ARM32 ABI"); return false; }
    char path[64]; snprintf(path,sizeof(path),"/proc/%u/as",(unsigned)getpid());
    int fd=::open(path,O_RDONLY|O_NONBLOCK);
    if (fd<0) { snprintf(reason,96,"Cannot read own process for runtime checks"); return false; }
    const bool ok=validateProfile(profile,exports,&processWord,&fd,reason);
    close(fd);
    return ok;
}

inline std::string report(const Identity& identity, const Profile* matched,
                           const uintptr_t exports[kExportCount], bool valid, const char* reason) {
    char line[1024];
    snprintf(line,sizeof(line),"Native Audio media compatibility v1\nfirmware=%s\nhmi_sha256=%s\nprocess_bound=%u\narm32_exec=%u\n"
        "matched_profile=%s\nruntime_verified=%u\nresult=%s\nfingerprint_error=%s\n",
        identity.firmware,identity.sha256,identity.processBound?1:0,identity.arm32Executable?1:0,
        matched?matched->id:"none",valid?1:0,reason,identity.error);
    std::string out=line;
    snprintf(line,sizeof(line),"executable=%s\n",identity.executable); out+=line;
    for (size_t i=0;i<kProfileCount;++i) {
        const Profile& p=*kProfiles[i]; unsigned available=0;
        for (unsigned j=0;j<kExportCount;++j) if (exports[j]) ++available;
        snprintf(line,sizeof(line),"profile=%s firmware=%s sha256=%s recognized=1 emulator=%u vehicle=%u\n"
            "verification_scope=%s\ncandidate_exports=%u/%u candidate_only=%u firmware_match=%u fingerprint_match=%u\n",
            p.id,p.firmware,p.hmiSha256,p.emulatorTested?1:0,p.vehicleConfirmed?1:0,p.verifiedVehicle,
            available,(unsigned)kExportCount,valid && matched==&p?0:1,
            !strcmp(identity.firmware,p.firmware)?1:0,!strcmp(identity.sha256,p.hmiSha256)?1:0);
        out+=line;
    }
    for (unsigned i=0;i<kExportCount;++i) {
        snprintf(line,sizeof(line),"export=%s address=0x%lx\n",kBuild66Exports[i].name,(unsigned long)exports[i]);
        out+=line;
    }
    out+="Unmatched exports are diagnostic candidates only; no guessed hook is enabled.\n";
    return out;
}

inline bool saveReport(const std::string& report) {
    const int fd=::open("/dev/shmem/nbt-aad-media-compatibility.txt",O_WRONLY|O_CREAT|O_TRUNC,0600);
    if (fd<0) return false;
    if (fchmod(fd,0600)!=0) { close(fd); return false; }
    size_t done=0;
    while (done<report.size()) {
        ssize_t n=write(fd,report.data()+done,report.size()-done);
        if (n<0 && errno==EINTR) continue;
        if (n<=0) break;
        done+=(size_t)n;
    }
    const bool ok=done==report.size(); close(fd); return ok;
}
} } }
#endif
