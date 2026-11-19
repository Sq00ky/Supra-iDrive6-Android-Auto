#include <dlfcn.h>
#include <assert.h>
#include <string.h>
#include <vector>
static std::vector<unsigned> ids;
static int commits=0, destroyed=0, visible=-1, failOrder=0;
static int fakeInit() { return 0; }
static int fakeDestroy() { ++destroyed; return 0; }
static int fakeCommit() { assert(visible>=0 && !ids.empty()); ++commits; return 0; }
static int fakeVisibility(unsigned id, unsigned value) { assert(id==256); visible=value; return 0; }
static int fakeOrder(unsigned layer, const unsigned* p, unsigned n) {
    assert(layer==0x80000000u); ids.assign(p,p+n); return failOrder;
}
static void* fakeOpen(const char*, int) { return (void*)1; }
static int fakeClose(void*) { return 0; }
static void* fakeSymbol(void*,const char* n) {
    if(!strcmp(n,"ilm_init"))return (void*)fakeInit;
    if(!strcmp(n,"ilm_destroy"))return (void*)fakeDestroy;
    if(!strcmp(n,"ilm_commitChanges"))return (void*)fakeCommit;
    if(!strcmp(n,"ilm_surfaceSetVisibility"))return (void*)fakeVisibility;
    if(!strcmp(n,"ilm_layerSetRenderOrder"))return (void*)fakeOrder;
    return 0;
}
#define dlopen fakeOpen
#define dlsym fakeSymbol
#define dlclose fakeClose
#include "favourite_composition.hpp"
int main() {
    {
        nbt::FavouriteComposition c;
        assert(c.apply(1282,1281,true));
        const unsigned expected[]={1281,1283,1282,256};
        assert(ids.size()==4 && !memcmp(&ids[0],expected,sizeof(expected)));
        assert(visible==1 && commits==1);
        assert(c.apply(1282,1281,false));
        assert(ids.size()==2 && ids[0]==1281 && ids[1]==1282);
        assert(visible==0 && commits==2);
        assert(c.apply(1282,0,true));
        assert(ids.size()==3 && ids[0]==1283 && ids[1]==1282 && ids[2]==256);
        failOrder=5;
        assert(!c.apply(1282,0,false));
        assert(commits==3 && destroyed==1); // Failed composition is never committed.
        failOrder=0;
        assert(c.apply(1282,0,false));
        assert(commits==4);
    }
    assert(destroyed==2);
}
