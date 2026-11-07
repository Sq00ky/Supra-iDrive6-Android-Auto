// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test: mm-renderer path matching, exefile reading and the pid marker (src/mmr_launch.hpp).
#include "mmr_launch.hpp"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
using namespace nbt::mmr;

int main() {
    // path matching
    assert(exePathIsRenderer("/opt/conn/bin/carplay/mm-renderer"));
    assert(exePathIsRenderer("/opt/conn/bin/carplay/mm-renderer\n"));
    assert(exePathIsRenderer("/fs/sda0/repository/istep/opt/conn/bin/carplay/mm-renderer"));
    assert(!exePathIsRenderer("/usr/sbin/mm-renderer"));            // a different mm-renderer is not ours to stop
    assert(!exePathIsRenderer("/opt/conn/bin/carplay/mm-renderer2"));
    assert(!exePathIsRenderer("mm-renderer"));
    assert(!exePathIsRenderer(""));
    assert(!exePathIsRenderer(NULL));

    // exefile from a fake /proc
    const char* root = "/tmp/nbt_mmr_proc";
    mkdir(root, 0755);
    mkdir("/tmp/nbt_mmr_proc/4242", 0755);
    FILE* f = fopen("/tmp/nbt_mmr_proc/4242/exefile", "w");
    assert(f);
    fputs("/opt/conn/bin/carplay/mm-renderer", f);
    fclose(f);
    char buf[256];
    assert(readExeFile(root, 4242, buf, sizeof(buf)) && exePathIsRenderer(buf));
    assert(!readExeFile(root, 4243, buf, sizeof(buf)));
    assert(!readExeFile(root, 0, buf, sizeof(buf)));
    unlink("/tmp/nbt_mmr_proc/4242/exefile");
    rmdir("/tmp/nbt_mmr_proc/4242");
    rmdir(root);

    // marker
    const char* m = "/tmp/nbt_mmr_marker_test";
    unlink(m);
    assert(markerRead(m) == 0);
    assert(markerWrite(m, 123456) && markerRead(m) == 123456);
    f = fopen(m, "w");
    fputs("garbage\n", f);
    fclose(f);
    assert(markerRead(m) == 0);
    f = fopen(m, "w");
    fputs("-5\n", f);
    fclose(f);
    assert(markerRead(m) == 0);
    unlink(m);

    // CarPlay coding: any of the listed carplay.ini files present
    const char* ini = "/tmp/nbt_mmr_carplay_ini_test";
    unlink(ini);
    const char* none[] = { "/tmp/does-not-exist-carplay.ini", ini, NULL };
    assert(!carPlayCoded(none));
    f = fopen(ini, "w");
    fputs("#Coded is: def,1280_480,touch_screen\n", f);
    fclose(f);
    assert(carPlayCoded(none));
    unlink(ini);
    const char* empty[] = { NULL };
    assert(!carPlayCoded(empty));
    // the default list is car_config's
    assert(nbt::kCarPlayIniPaths[0] && strcmp(nbt::kCarPlayIniPaths[0], "/var/opt/conn/carplay/carplay.ini") == 0);

    // CONN_CARPLAY's other processes (acpd, mdnsd) and the generic tail match
    assert(exePathIsCarPlayHelper("/opt/conn/bin/carplay/acpd"));
    assert(exePathIsCarPlayHelper("/fs/sda0/repository/istep/opt/conn/bin/carplay/mdnsd\n"));
    assert(!exePathIsCarPlayHelper("/opt/conn/bin/carplay/mm-renderer"));
    assert(!exePathIsCarPlayHelper("/usr/sbin/mdnsd"));               // not CarPlay's
    assert(!exePathIsCarPlayHelper(NULL));
    assert(!exePathEndsWith("/x/acpd", ""));
    assert(!exePathEndsWith("acpd", "/conn/bin/carplay/acpd"));

    // spawnStep (issue #118): start mm-renderer, or keep waiting?
    //   not coded, none running: at once, whatever the uptime
    assert(spawnStep(1, false, false, false, 5.0, 60) == kSpawnStart);
    //   NBT_MMR_SPAWN=0: never
    assert(spawnStep(0, false, false, false, 500.0, 60) == kSpawnWait);
    assert(spawnStep(0, true, false, false, 500.0, 60) == kSpawnWait);
    //   one is running (the head unit's or an earlier one): wait for it, never a second one
    assert(spawnStep(1, false, true, false, 500.0, 60) == kSpawnWait);
    assert(spawnStep(1, true, true, false, 500.0, 60) == kSpawnWait);
    //   already started (or tried) in this run: not again
    assert(spawnStep(1, false, false, true, 500.0, 60) == kSpawnWait);
    assert(spawnStep(1, true, false, true, 500.0, 60) == kSpawnWait);
    //   coded: leave it to the head unit until boot_s after boot, then start (carplay.ini without active CarPlay)
    assert(spawnStep(1, true, false, false, 20.0, 60) == kSpawnWait);
    assert(spawnStep(1, true, false, false, 59.9, 60) == kSpawnWait);
    assert(spawnStep(1, true, false, false, 60.0, 60) == kSpawnStart);
    assert(spawnStep(1, true, false, false, 900.0, 60) == kSpawnStart);
    //   NBT_MMR_BOOT_S=0: at once on a coded car too
    assert(spawnStep(1, true, false, false, 3.0, 0) == kSpawnStart);

    printf("test_mmr_launch OK\n");
    return 0;
}
