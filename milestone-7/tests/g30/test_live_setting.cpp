#include "cluster/video_settings.hpp"
#include <assert.h>
#include <stdio.h>
int main() {
    char dir[]="/tmp/aa-map-setting-XXXXXX";assert(mkdtemp(dir));
    char path[256];snprintf(path,sizeof(path),"%s/setting",dir);
    setenv("NBT_G30_CLUSTER_SETTING",path,1);unsetenv("NBT_G30_CLUSTER_VIDEO");
    assert(!aa_cluster::setting_enabled());assert(aa_cluster::setting_write(true));
    assert(aa_cluster::setting_enabled());assert(aa_cluster::setting_write(false));
    setenv("NBT_G30_CLUSTER_VIDEO","1",1);assert(!aa_cluster::setting_enabled());
    unlink(path);assert(aa_cluster::setting_enabled());
    FILE* f=fopen(path,"wb");assert(f);fputs("invalid",f);fclose(f);
    assert(!aa_cluster::setting_enabled());unlink(path);rmdir(dir);
    assert(!aa_cluster::setting_write(true));
    puts("AA_LIVE_SETTING_OK default off, persisted on/off, explicit off wins, malformed/write failure");
}
