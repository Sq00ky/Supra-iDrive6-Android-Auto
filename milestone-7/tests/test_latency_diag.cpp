// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#include "most/latency_diag.hpp"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
using namespace nbt::latencydiag;
void* producer(void*) {Meta m;m.fb=0x82;m.fkt=0xc04;m.handle=0x17;for(int i=0;i<1000;++i)record(Dispatch,m,0,i);return NULL;}
int main() {
    Meta m;assert(!record(Dispatch,m));assert(stamp()==0 && state().written==0);
    // A busy recorder must drop immediately instead of blocking.
    state().enabled=true;pthread_mutex_lock(&state().lock);
    assert(!record(Dispatch,m));pthread_mutex_unlock(&state().lock);
    for(int i=0;i<Capacity+10;++i)assert(record(Dispatch,m));
    assert(state().overwritten==10 && state().written-state().read==Capacity);
    pthread_t workers[4];for(int i=0;i<4;++i)assert(!pthread_create(&workers[i],NULL,producer,NULL));
    for(int i=0;i<4;++i)pthread_join(workers[i],NULL);
    assert(state().written-state().read<=Capacity);
    char path[]="/tmp/6wb-diag-XXXXXX";int fd=mkstemp(path);assert(fd>=0);close(fd);
    assert(start(true,path));stop();
    FILE* f=fopen(path,"r");assert(f);char line[512];bool header=false,footer=false;unsigned records=0;
    while(fgets(line,sizeof(line),f)) {if(strstr(line,"serial,ns"))header=true;if(strstr(line,"overwritten="))footer=true;if(line[0]>='0'&&line[0]<='9')++records;}
    fclose(f);unlink(path);assert(header && footer && records>0);puts("test_latency_diag OK");
}
