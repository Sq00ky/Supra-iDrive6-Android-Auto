// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// plugin_check - loads libhmiaad.so the way HmiMain3D's PluginFramework does (dlopen, dlsym "<Name>Struct",
// compiler-string check, create, start, stop, destroy) so the library can be validated on the car BEFORE it is
// installed into hmiplugins. Usage: plugin_check [path/to/libhmiaad.so]
#include "plugin_abi.hpp"
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

static int checkOne(void* lib, const char* name) {
    char sym[128];
    snprintf(sym, sizeof(sym), "%sStruct", name);
    nbt::PluginStruct* ps = static_cast<nbt::PluginStruct*>(dlsym(lib, sym));
    if (!ps) { printf("FAIL dlsym %s: %s\n", sym, dlerror()); return 1; }
    printf("ok   %s at %p: create=%p destroy=%p compiler='%s'\n", sym, (void*)ps, (void*)ps->create,
           (void*)ps->destroy, ps->compiler ? ps->compiler : "(null)");
    if (!ps->compiler || strcmp(ps->compiler, NBT_AAD_PLUGIN_COMPILER) != 0) {
        printf("FAIL compiler string does not match the HMI's '%s'\n", NBT_AAD_PLUGIN_COMPILER);
        return 1;
    }
    void *iface = 0, *plugin = 0, *object = 0;
    ps->create(&iface, &plugin, &object);
    printf("ok   create -> iface=%p plugin=%p object=%p\n", iface, plugin, object);
    if (!plugin || !object) { printf("FAIL create returned NULL\n"); return 1; }
    nbt::IHmiPluginLike* p = static_cast<nbt::IHmiPluginLike*>(plugin);
    int rc = p->start(NULL);
    printf("%s start -> %d\n", rc == 0 ? "ok  " : "FAIL", rc);
    int rc2 = p->stop(NULL);
    printf("%s stop -> %d\n", rc2 == 0 ? "ok  " : "FAIL", rc2);
    ps->destroy(object);
    printf("ok   destroy\n");
    return (rc == 0 && rc2 == 0) ? 0 : 1;
}

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "./libhmiaad.so";
    printf("plugin_check: dlopen(%s)\n", path);
    void* lib = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!lib) { printf("FAIL dlopen: %s\n", dlerror()); return 1; }
    int bad = checkOne(lib, "NbtAadBundle") + checkOne(lib, "CNbtAadGate");
    printf("%s (log also in /dev/shmem/nbt-aad-plugin.log)\n", bad ? "plugin_check FAILED" : "plugin_check OK");
    return bad ? 1 : 0;
}
