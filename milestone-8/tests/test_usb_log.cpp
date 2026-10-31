// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test for src/usb_log.hpp: USB stick lookup under a fake /mnt and log rotation.
#include "usb_log.hpp"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int main() {
    const char* root = "/tmp/nbt_usblog_mnt";
    int rc = system("rm -rf /tmp/nbt_usblog_mnt && mkdir -p /tmp/nbt_usblog_mnt/other /tmp/nbt_usblog_mnt/io-net");
    assert(rc == 0);
    char stick[128];
    assert(!nbt::usblog::findStick(stick, sizeof(stick), root) && stick[0] == 0);   // no umass* entry
    assert(!nbt::usblog::findStick(stick, sizeof(stick), "/tmp/nbt_usblog_missing"));
    FILE* f = fopen("/tmp/nbt_usblog_mnt/umass_file", "w");                          // a file, not a mount
    assert(f);
    fclose(f);
    assert(!nbt::usblog::findStick(stick, sizeof(stick), root));
    assert(mkdir("/tmp/nbt_usblog_mnt/umass01200t7", 0755) == 0);
    assert(nbt::usblog::findStick(stick, sizeof(stick), root));
    assert(strcmp(stick, "/tmp/nbt_usblog_mnt/umass01200t7") == 0);

    char path[192];
    nbt::usblog::logPath(path, sizeof(path), stick, nbt::usblog::kDaemonName, 10);
    assert(strcmp(path, "/tmp/nbt_usblog_mnt/umass01200t7/nbt-aad.log") == 0);
    f = fopen(path, "w");
    assert(f);
    fputs("0123456789abcdef\n", f);                                                   // 17 bytes > 10
    fclose(f);
    nbt::usblog::logPath(path, sizeof(path), stick, nbt::usblog::kDaemonName, 10);
    struct stat st;
    assert(stat(path, &st) != 0);                                                     // moved away
    assert(stat("/tmp/nbt_usblog_mnt/umass01200t7/nbt-aad.old.log", &st) == 0 && st.st_size == 17);
    nbt::usblog::logPath(path, sizeof(path), stick, nbt::usblog::kAppName, 10);
    assert(strcmp(path, "/tmp/nbt_usblog_mnt/umass01200t7/nbt-aad-app.log") == 0);

    rc = system("rm -rf /tmp/nbt_usblog_mnt");
    (void)rc;
    printf("test_usb_log OK\n");
    return 0;
}
