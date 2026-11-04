// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
//
// lmrestore -- put screen 0's layer render order back to what the car shipped with.
//
// Why: osdprobe called ilm_displaySetRenderOrder(0, {projection, a freshly created layer, 0}) to try to
// get an overlay layer on screen. The service accepted it (commit rc=0) and the screen's layer render
// order came back EMPTY on the next dump -- a list of one known-good layer plus one layer the compositor
// does not know is not a valid order, and it costs you the whole list. This tool writes the original
// order back, read from the car's own "LayerManagerControl get screen 0x0" dump.
//
// Run: cp /mnt/umass01200t7/lmrestore /tmp/lmrestore; /tmp/lmrestore
// Prints the return code of every step. Reboot if the panel still looks wrong afterwards.

#include <stdio.h>
#include <string.h>
#include <dlfcn.h>

typedef unsigned int u32;
typedef int          i32;
typedef u32 t_ilm_layer;
typedef u32 t_ilm_uint;
typedef i32 t_ilm_bool;

// The car's own order, as reported by "get screen 0x0" before anything of ours touched it.
static t_ilm_layer kOrder[] = {
    2147483648u, 2164260864u, 2181038080u, 2147549184u, 2164326400u, 2181103616u,
    2147680256u, 2147614720u, 2164391936u, 2181169152u, 2147745792u, 2164523008u,
    2181300224u, 2147524608u, 2164301824u, 2181079040u, 2147942400u, 2164719616u,
    2181496832u, 2147876864u, 2164654080u, 2181431296u, 2148007936u, 2164785152u,
    2181562368u, 2147528704u, 2164305920u, 2181083136u, 2147811328u, 2148532224u,
    2148532225u, 2148597760u, 2148597761u, 2148663296u, 2148663297u, 2148728832u,
    2148728833u, 2148794368u, 2148794369u, 2148859904u, 2148859905u, 2148925440u,
    2148925441u, 2148990976u, 2148990977u, 2149056512u, 2149056513u, 2149122048u,
    2149122049u, 2149187584u, 2149187585u, 2149253120u, 2149253121u, 2149318656u,
    2149318657u, 2149384192u, 2149384193u, 2149449728u, 2149449729u, 2149515264u,
    2149515265u, 2149580800u, 2149580801u, 2149646336u, 2149646337u, 2149711872u,
    2149711873u, 2149777408u, 2149777409u, 2149842944u, 2149842945u, 2149908480u,
    2149908481u,
    0u   // zero-terminated: this library takes (display, layers[]) with no count
};

typedef struct {
    void* lib;
    int (*init)(void);
    int (*destroy)(void);
    int (*commit)(void);
    int (*displaySetRenderOrder)(t_ilm_uint, t_ilm_layer*);
    int (*getScreenIDs)(t_ilm_uint*, t_ilm_uint*);
} Ilm;

static void dm(void** slot, void* lib, const char* name) { *slot = dlsym(lib, name); }

int main(void) {
    printf("lmrestore -- restore screen 0 layer render order (%u layers)\n",
           (unsigned)(sizeof(kOrder) / sizeof(kOrder[0]) - 1));
    Ilm g;
    memset(&g, 0, sizeof(g));
    const char* paths[] = { "/opt/sys/lib/libilmClient.gpp.so.1", "/opt/sys/lib/libilmClient.cpp-ne.so.1" };
    for (unsigned i = 0; i < 2 && !g.lib; ++i) {
        g.lib = dlopen(paths[i], RTLD_NOW);
        printf("[restore] dlopen(%s) -> %s\n", paths[i], g.lib ? "OK" : dlerror());
    }
    if (!g.lib) return 1;
    dm(&(void*&)g.init, g.lib, "ilm_init");
    dm(&(void*&)g.destroy, g.lib, "ilm_destroy");
    dm(&(void*&)g.commit, g.lib, "ilm_commitChanges");
    dm(&(void*&)g.displaySetRenderOrder, g.lib, "ilm_displaySetRenderOrder");
    dm(&(void*&)g.getScreenIDs, g.lib, "ilm_getScreenIDs");
    if (!g.init || !g.commit || !g.displaySetRenderOrder) {
        printf("[restore] missing symbols\n");
        return 1;
    }
    int rc = g.init();
    printf("[restore] ilm_init rc=%d\n", rc);
    if (rc != 0) return 1;

    if (g.getScreenIDs) {
        u32 ids[8];
        u32 n = 0;
        memset(ids, 0, sizeof(ids));
        rc = g.getScreenIDs(&n, ids);
        printf("[restore] screens rc=%d count=%u\n", rc, n);
    }

    rc = g.displaySetRenderOrder(0, kOrder);
    printf("[restore] displaySetRenderOrder(0, %u layers) rc=%d\n",
           (unsigned)(sizeof(kOrder) / sizeof(kOrder[0]) - 1), rc);
    rc = g.commit();
    printf("[restore] commit rc=%d\n", rc);

    g.destroy();
    printf("[restore] done: now check 'LayerManagerControl get screen 0x0'\n");
    return 0;
}
