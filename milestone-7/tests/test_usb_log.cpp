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

static const char* const kKeep = "/tmp/nbt_usblog_mnt/umass_keep";

static const char* at(const char* name) {
    static char p[192];
    snprintf(p, sizeof(p), "%s/%s", kKeep, name);
    return p;
}

static void put(const char* name, const char* text) {
    FILE* f = fopen(at(name), "w");
    assert(f);
    fputs(text, f);
    fclose(f);
}

// First character of the file, 0 if it is missing.
static char peek(const char* name) {
    FILE* f = fopen(at(name), "r");
    if (!f) return 0;
    int c = fgetc(f);
    fclose(f);
    return c == EOF ? 0 : (char)c;
}

static void rotate(const char* name, int keep) {
    char path[192];
    nbt::usblog::logPath(path, sizeof(path), kKeep, name, 10, keep);
}

static void testKeep() {
    using nbt::usblog::keepCount;
    assert(keepCount(NULL) == 1);   assert(keepCount("") == 1);    assert(keepCount("0") == 1);
    assert(keepCount("-1") == 1);   assert(keepCount("abc") == 1); assert(keepCount("3x") == 1);
    assert(keepCount("1") == 1);    assert(keepCount("3") == 3);   assert(keepCount("99") == 99);
    assert(keepCount("100") == 99); assert(keepCount("99999999999999999999") == 99);
    assert(keepCount("3\r\n") == 3); assert(keepCount(" 7 ") == 7);

    assert(mkdir(kKeep, 0755) == 0);
    const char* gens[] = { "A-0123456789\n", "B-0123456789\n", "C-0123456789\n", "D-0123456789\n" };
    for (int i = 0; i < 4; ++i) { put("nbt-aad.log", gens[i]); rotate("nbt-aad.log", 3); }
    assert(!peek("nbt-aad.log"));
    assert(peek("nbt-aad.old.log") == 'D' && peek("nbt-aad.old2.log") == 'C' && peek("nbt-aad.old3.log") == 'B');
    assert(!peek("nbt-aad.old4.log"));

    // gap: old2 deleted by hand
    assert(unlink(at("nbt-aad.old2.log")) == 0);
    put("nbt-aad.log", "E-0123456789\n");
    rotate("nbt-aad.log", 3);
    assert(peek("nbt-aad.old.log") == 'E' && peek("nbt-aad.old2.log") == 'D');
    assert(!peek("nbt-aad.old3.log"));

    // exactly maxBytes: not rotated
    put("nbt-aad.log", "0123456789");
    rotate("nbt-aad.log", 3);
    assert(peek("nbt-aad.log") == '0' && peek("nbt-aad.old.log") == 'E');

    // lowered to 2: everything from old3 up to old99 goes, old100 stays
    put("nbt-aad.old4.log", "x");
    put("nbt-aad.old7.log", "x");
    put("nbt-aad.old100.log", "x");
    put("nbt-aad.log", "F-0123456789\n");
    rotate("nbt-aad.log", 2);
    assert(peek("nbt-aad.old.log") == 'F' && peek("nbt-aad.old2.log") == 'E');
    assert(!peek("nbt-aad.old3.log") && !peek("nbt-aad.old4.log") && !peek("nbt-aad.old7.log"));
    assert(peek("nbt-aad.old100.log") == 'x');

    // name without .log
    put("x.txt", "G-0123456789\n");
    rotate("x.txt", 3);
    put("x.txt", "H-0123456789\n");
    rotate("x.txt", 3);
    assert(peek("x.txt.old") == 'H' && peek("x.txt.old2") == 'G');
}

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
    testKeep();

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
