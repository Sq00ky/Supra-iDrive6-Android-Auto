#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

namespace aa_cluster {
static const char* const ActiveMarker="/dev/shmem/nbt-aad-kombi-video.active";
// A two-byte preference, never a video buffer. An explicit 0 also overrides
// the legacy trial's NBT_G30_CLUSTER_VIDEO=1 startup default.
inline const char* setting_path() {
    const char* p=getenv("NBT_G30_CLUSTER_SETTING");
    return p&&*p?p:"/var/opt/aa/kombi-video";
}
inline bool setting_enabled() {
    FILE* f=fopen(setting_path(),"rb");
    if(f) { int c=fgetc(f);fclose(f);return c=='1'; }
    const char* p=getenv("NBT_G30_CLUSTER_VIDEO");
    return p&&!strcmp(p,"1");
}
inline bool setting_write(bool on) {
    const char* path=setting_path();
    if(!getenv("NBT_G30_CLUSTER_SETTING"))mkdir("/var/opt/aa",0755);
    char tmp[512];int n=snprintf(tmp,sizeof(tmp),"%s.new.%ld",path,(long)getpid());
    if(n<=0||unsigned(n)>=sizeof(tmp))return false;
    int fd=open(tmp,O_WRONLY|O_CREAT|O_EXCL,0644);
    if(fd<0)return false;
    bool ok=write(fd,on?"1\n":"0\n",2)==2;
    if(close(fd))ok=false;
    if(ok)ok=rename(tmp,path)==0;
    if(!ok)unlink(tmp);
    return ok;
}
}
