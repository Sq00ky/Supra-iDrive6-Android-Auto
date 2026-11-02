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

    // RAM log: fresh file, hard cap (here 1000 bytes), one "full" line, then nothing.
    {
        const char* rp = "/tmp/nbt_usblog_mnt/ram.log";
        f = fopen(rp, "w"); assert(f); fputs("stale from the last run\n", f); fclose(f);
        nbt::ramlog::Capped c;
        char line[100];
        memset(line, 'x', 99); line[99] = '\n';
        int ok = 0;
        for (int i = 0; i < 50; ++i) ok += nbt::ramlog::append(c, rp, line, 100, 1000) ? 1 : 0;
        assert(c.full);
        assert(ok == 8);                                            // 8 * 100 <= 1000 - 160 < 9 * 100
        assert(stat(rp, &st) == 0 && st.st_size <= 1000);
        assert(st.st_size == 800 + (long)strlen(nbt::ramlog::kFullLine));
        long before = st.st_size;
        assert(!nbt::ramlog::append(c, rp, line, 100, 1000));        // stays off
        assert(stat(rp, &st) == 0 && st.st_size == before);
        FILE* r = fopen(rp, "r"); char first[8] = {0}; assert(fread(first, 1, 5, r) == 5); fclose(r);
        assert(strcmp(first, "xxxxx") == 0);                        // truncated at the first write
        // real cap: 5 MB
        nbt::ramlog::Capped c2;
        static char big[64 * 1024];
        memset(big, 'y', sizeof(big));
        while (nbt::ramlog::append(c2, rp, big, sizeof(big))) {}
        assert(c2.full && stat(rp, &st) == 0 && st.st_size <= 5L * 1024 * 1024);
        assert(nbt::ramlog::kMaxBytes == 5L * 1024 * 1024);
        assert(!nbt::ramlog::daemonMustStop(1024 * 1024));
        assert(nbt::ramlog::daemonMustStop(5L * 1024 * 1024 - 256 * 1024));
    }
    // head-unit software version file (CRLF like the car's)
    {
        const char* vp = "/tmp/nbt_usblog_mnt/nbt_version.txt";
        f = fopen(vp, "w"); assert(f); fputs("NBTevo_T21424I\r\n", f); fclose(f);
        char v[64];
        nbt::huVersion(v, sizeof(v), vp);
        assert(strcmp(v, "NBTevo_T21424I") == 0);
        nbt::huVersion(v, sizeof(v), "/tmp/nbt_usblog_mnt/missing.txt");
        assert(strcmp(v, "unknown") == 0);
    }

    rc = system("rm -rf /tmp/nbt_usblog_mnt");
    (void)rc;
    printf("test_usb_log OK\n");
    return 0;
}
