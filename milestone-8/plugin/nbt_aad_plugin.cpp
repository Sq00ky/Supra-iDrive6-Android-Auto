// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// libhmiaad.so - NBT-AAD plugin for HmiMain3D.
//
// Stage 1: log that the framework created and started the bundle and the plugin.
// Stage 3: "Android Auto" entry in the Communication menu. Selecting it starts /opt/aa/nbt-aad (or brings a
// running daemon to the front - the daemon forwards to the running instance).
//
// Hook-free: nothing in HmiMain3D is modified. The plugin only calls iDrive's own services (see plugin_abi.hpp):
//   1. start(): resolve "CHmiMainDispatcher" through the plugin framework, start a helper thread.
//   2. helper: wait until the HMI processing unit (RTTI "15CHmiAppProcUnit") is listed in the main dispatcher,
//      then arm a one-shot timer on it through its own IHmiTimer.
//   3. timer callback (runs ON the HMI thread): get CHmiMenuProviderRegistry, look up the "ST_Com_Selection"
//      provider by name; once it exists (Communication has built its menu), create a CHmiGenericMenuEntry with
//      iDrive's constructor and add it through the provider's own addEntry. Otherwise re-arm the timer.
//   4. The entry is our own subclass of CHmiGenericMenuEntry (own vtable, execute = start the daemon).
// The few non-exported HmiMain3D functions used are checked against the expected code first; on any mismatch the
// plugin only logs.
//
// Off switch: touch /opt/aa/menu.disable (or remove /opt/aa/nbt-aad), then reboot.
// Optional /opt/aa/menu.cfg lines: text=0x.. icon=0x.. focus=0x.. order=0x.. (resource handles / ids).
// Optional /opt/aa/nbt-aad.env: one NAME=value per line for the daemon (default NBT_AUDIO_BT=1).
// Logs: /dev/shmem/nbt-aad-plugin.log (+ slog major 0x4e41), daemon output appends to /dev/shmem/nbt-aad.log.
#include "plugin_abi.hpp"
#include "hmi_services.hpp"
#include "hmi_menu.hpp"
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <dlfcn.h>
#include <pthread.h>
#include <sys/stat.h>
#ifdef __QNX__
#include <spawn.h>
#include <sys/slog.h>
#include <sys/slogcodes.h>
#else
#include <spawn.h>
#endif

#define NBT_AAD_PLUGIN_VERSION "plugin-s4 (" __DATE__ " " __TIME__ ")"

extern char** environ;

namespace {

const char* const kDaemon     = "/opt/aa/nbt-aad";
const char* const kDisable    = "/opt/aa/menu.disable";
const char* const kMenuCfg    = "/opt/aa/menu.cfg";
const char* const kDaemonEnv  = "/opt/aa/nbt-aad.env";
const char* const kDaemonLog  = "/dev/shmem/nbt-aad.log";

void plog(const char* fmt, ...) {
    char msg[384];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    char line[448];
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    int n = snprintf(line, sizeof(line), "[%5ld.%03ld] [NbtAadPlugin] %s\n", (long)ts.tv_sec, (long)(ts.tv_nsec / 1000000), msg);
    if (n < 0) return;
    if (n >= (int)sizeof(line)) n = (int)sizeof(line) - 1;
    int fd = open("/dev/shmem/nbt-aad-plugin.log", O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (fd < 0) fd = open("/tmp/nbt-aad-plugin.log", O_WRONLY | O_CREAT | O_APPEND, 0666);   // host tests
    if (fd >= 0) {
        ssize_t w = write(fd, line, (size_t)n);
        (void)w;
        close(fd);
    }
#ifdef __QNX__
    slogf(_SLOG_SETCODE(0x4e41, 0), _SLOG_INFO, "[NbtAadPlugin] %s", msg);
#endif
}

bool fileExists(const char* p) {
    struct stat st;
    return stat(p, &st) == 0;
}

// Default: Media/Radio source list ("ST_Ent_Selection"), between Bluetooth audio (order 0x1bae) and USB (0x1d7d).
// menu=ST_Com_Selection with order=0x590 puts it in Communication next to Mobile devices instead.
struct MenuCfgFile { uint32_t text, icon, focus, order; char menu[32]; };
MenuCfgFile g_cfg = { 0x200148d0u, 0x50000513u, 0x7aa0u, 0x1c00u, "ST_Ent_Selection" };

void readMenuCfg() {
    FILE* f = fopen(kMenuCfg, "r");
    if (!f) return;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        char key[32];
        long v = 0;
        char menu[32];
        if (sscanf(line, "menu=%31s", menu) == 1) { strcpy(g_cfg.menu, menu); continue; }
        if (sscanf(line, "%31[a-z]=%li", key, &v) != 2) continue;
        if (!strcmp(key, "text")) g_cfg.text = (uint32_t)v;
        else if (!strcmp(key, "icon")) g_cfg.icon = (uint32_t)v;
        else if (!strcmp(key, "focus")) g_cfg.focus = (uint32_t)v;
        else if (!strcmp(key, "order")) g_cfg.order = (uint32_t)v;
    }
    fclose(f);
}

time_t g_lastLaunch = 0;

// ---- launching the daemon ---------------------------------------------------------------------
void launchDaemon() {
    time_t now = time(NULL);
    if (g_lastLaunch && now - g_lastLaunch < 3) { plog("launch ignored (pressed again within 3 s)"); return; }
    g_lastLaunch = now;
    if (!fileExists(kDaemon)) { plog("launch: %s not found", kDaemon); return; }

    // Environment: ours first (getenv takes the first match), then the HMI's.
    static char envbuf[16][160];
    char* envp[16 + 64];
    int ne = 0;
    strcpy(envbuf[0], "PATH=/bin:/usr/bin:/opt/os/bin:/opt/sys/bin:/opt/core/bin:/opt/car/bin:/fs/sda0/opt/mm/bin");
    envp[ne++] = envbuf[0];
    int nb = 1;
    FILE* f = fopen(kDaemonEnv, "r");
    if (f) {
        char line[160];
        while (nb < 16 && fgets(line, sizeof(line), f)) {
            size_t l = strlen(line);
            while (l && (line[l - 1] == '\n' || line[l - 1] == '\r' || line[l - 1] == ' ')) line[--l] = 0;
            if (!l || line[0] == '#' || !strchr(line, '=')) continue;
            strcpy(envbuf[nb], line);
            envp[ne++] = envbuf[nb++];
        }
        fclose(f);
    } else {
        strcpy(envbuf[1], "NBT_AUDIO_BT=1");
        envp[ne++] = envbuf[1];
        nb = 2;
    }
    for (char** e = environ; e && *e && ne < (int)(sizeof(envp) / sizeof(envp[0])) - 1; ++e) {
        if (strncmp(*e, "PATH=", 5) == 0) continue;
        envp[ne++] = *e;
    }
    envp[ne] = NULL;

    char* argv[] = { const_cast<char*>(kDaemon), NULL };
    int in  = open("/dev/null", O_RDONLY);
    int out = open(kDaemonLog, O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (in < 0 || out < 0) {
        plog("launch: cannot open /dev/null or %s (errno %d)", kDaemonLog, errno);
        if (in >= 0) close(in);
        if (out >= 0) close(out);
        return;
    }
#ifdef __QNX__
    int fdmap[3] = { in, out, out };          // only these three fds reach the daemon
    struct inheritance inh;
    memset(&inh, 0, sizeof(inh));
    inh.flags = SPAWN_SETGROUP | SPAWN_SETSIGDEF | SPAWN_SETSIGMASK | SPAWN_NOZOMBIE;
    inh.pgroup = SPAWN_NEWPGROUP;
    sigfillset(&inh.sigdefault);              // default signal handlers in the daemon, nothing blocked
    sigemptyset(&inh.sigmask);
    pid_t pid = spawn(kDaemon, 3, fdmap, &inh, argv, envp);
#else
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, in, 0);
    posix_spawn_file_actions_adddup2(&fa, out, 1);
    posix_spawn_file_actions_adddup2(&fa, out, 2);
    pid_t pid = -1;
    if (posix_spawn(&pid, kDaemon, &fa, NULL, argv, envp) != 0) pid = -1;
    posix_spawn_file_actions_destroy(&fa);
#endif
    int e = errno;
    close(in);
    close(out);
    if (pid < 0) plog("launch: spawn %s failed (errno %d)", kDaemon, e);
    else plog("launch: started %s pid %d (%d env overrides, output -> %s)", kDaemon, (int)pid, nb, kDaemonLog);
}

void onEntrySelected(void*) { launchDaemon(); }

// ---- HMI-thread scheduling --------------------------------------------------------------------
void* g_dispatcher = NULL;
void* g_appUnit = NULL;
int   g_tries = 0;
int   g_timerTarget = 0;                     // address used as the timer target

void onHmiTimer(void*, unsigned timerId, unsigned, unsigned) {
    ++g_tries;
    nbt::hmi::MenuEntryCfg cfg;
    cfg.menuName = g_cfg.menu;
    cfg.fallbackState = "ST_MyV_DeviceList";
    cfg.text = g_cfg.text; cfg.icon = g_cfg.icon; cfg.focus = g_cfg.focus; cfg.order = g_cfg.order;
    int r = nbt::hmi::addMenuEntry(cfg, &onEntrySelected);
    if (r == 0 && g_tries < 240) {
        if (g_tries == 1 || g_tries % 20 == 0) plog("HMI timer %u: menu %s not built yet (try %d)", timerId, g_cfg.menu, g_tries);
        nbt::hmi::armTimer(g_appUnit, &g_timerTarget, &onHmiTimer, 500);
    } else if (r == 0) {
        plog("gave up: menu %s not found after %d tries", g_cfg.menu, g_tries);
    } else if (r < 0) {
        plog("menu entry not added (see above)");
    }
}

void* helperThread(void*) {
    for (int i = 0; i < 1200 && !g_appUnit; ++i) {        // up to 2 minutes
        g_appUnit = nbt::hmi::findAppProcUnit(g_dispatcher);
        if (!g_appUnit) usleep(100000);
    }
    if (!g_appUnit) { plog("HMI processing unit not found - no menu entry"); return NULL; }
    int id = nbt::hmi::armTimer(g_appUnit, &g_timerTarget, &onHmiTimer, 200);
    plog("HMI processing unit %p found, timer armed (id %d)", g_appUnit, id);
    return NULL;
}

void startMenuEntry(void* resolver) {
    if (fileExists(kDisable)) { plog("menu entry disabled (%s exists)", kDisable); return; }
    if (!fileExists(kDaemon)) { plog("menu entry off: %s not installed", kDaemon); return; }
    if (!nbt::hmi::loadSymbols()) return;
    if (!dlsym(RTLD_DEFAULT, "_ZN10THmiStringItEC1ItvEEPKc")) { plog("not inside HmiMain3D - menu entry skipped"); return; }
    if (!nbt::hmi::checkMenuFirmware()) return;
    readMenuCfg();
    g_dispatcher = nbt::hmi::resolvePlugin(resolver, "CHmiMainDispatcher");
    if (!g_dispatcher) { plog("CHmiMainDispatcher not available - no menu entry"); return; }
    pthread_t t;
    if (pthread_create(&t, NULL, &helperThread, NULL) == 0) pthread_detach(t);
}

class NbtAadBundle : public nbt::IHmiPluginLike {
public:
    NbtAadBundle() { plog("bundle constructed (%p), %s", (void*)this, NBT_AAD_PLUGIN_VERSION); }
    virtual ~NbtAadBundle() { plog("bundle destroyed (%p)", (void*)this); }
    virtual int start(void* resolver) {
        plog("bundle start(resolver %p) pid %d", resolver, (int)getpid());
        startMenuEntry(resolver);
        return 0;
    }
    virtual int stop(void* resolver) {
        plog("bundle stop(resolver %p) - timer callbacks %d", resolver, g_tries);
        return 0;
    }
};

class NbtAadGate : public nbt::IHmiPluginLike {
public:
    NbtAadGate() { plog("gate plugin constructed (%p)", (void*)this); }
    virtual ~NbtAadGate() { plog("gate plugin destroyed (%p)", (void*)this); }
    virtual int start(void* resolver) {
        plog("gate plugin start(resolver %p) - input gate not implemented, iDrive input untouched", resolver);
        return 0;
    }
    virtual int stop(void* resolver) {
        plog("gate plugin stop(resolver %p)", resolver);
        return 0;
    }
};

void createBundle(void** iface, void** plugin, void** object) {
    NbtAadBundle* b = new NbtAadBundle();
    nbt::IHmiPluginLike* p = b;
    if (iface) *iface = p;
    if (plugin) *plugin = p;
    if (object) *object = p;
    plog("createFunctionNbtAadBundle -> %p", (void*)p);
}

void destroyBundle(void* object) {
    plog("destroyFunctionNbtAadBundle(%p)", object);
    delete static_cast<nbt::IHmiPluginLike*>(object);
}

void createGate(void** iface, void** plugin, void** object) {
    NbtAadGate* g = new NbtAadGate();
    nbt::IHmiPluginLike* p = g;
    if (iface) *iface = p;
    if (plugin) *plugin = p;
    if (object) *object = p;
    plog("createFunctionCNbtAadGate -> %p", (void*)p);
}

void destroyGate(void* object) {
    plog("destroyFunctionCNbtAadGate(%p)", object);
    delete static_cast<nbt::IHmiPluginLike*>(object);
}

} // namespace

// Runs when the library is dlopen()ed (before the framework calls create), so the log shows whether iDrive
// loaded the file at all.
__attribute__((constructor)) static void nbtAadLibraryLoaded() {
    plog("library loaded into pid %d, %s", (int)getpid(), NBT_AAD_PLUGIN_VERSION);
}

void nbtPluginLog(const char* fmt, ...) {
    char msg[384];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    plog("%s", msg);
}

// Looked up by name (exportedSymbolName + "Struct") from NbtAadPlugin.xml.
extern "C" {
__attribute__((visibility("default"))) nbt::PluginStruct NbtAadBundleStruct = { &createBundle, &destroyBundle, NBT_AAD_PLUGIN_COMPILER };
__attribute__((visibility("default"))) nbt::PluginStruct CNbtAadGateStruct  = { &createGate,   &destroyGate,   NBT_AAD_PLUGIN_COMPILER };
}
