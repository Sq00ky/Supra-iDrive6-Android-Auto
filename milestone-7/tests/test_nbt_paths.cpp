// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test for src/nbt_paths.hpp: hand install vs NBTApps package, decided from the daemon's argv[0] or (iDrive app)
// from an executable nbt-aad in the package's app dir; the app, data and conf dirs of each.
#include "nbt_paths.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)
#define SAME(a, b) CHECK(strcmp((a), (b)) == 0)

#define PKG_APP  "/var/opt/nbtapp/apps/com.github.sq00ky.android-auto"
#define PKG_DATA "/var/opt/nbtapp/data/com.github.sq00ky.android-auto"

int main() {
    // The literals the packaging job looks for in both programs.
    SAME(nbt::kPackageId, "com.github.sq00ky.android-auto");
    SAME(nbt::kPackageAppDir, PKG_APP);
    SAME(nbt::kPackageDataDir, PKG_DATA);

    // ---- pathIsPackaged: only a path inside the package's app dir ------------------------------------------------
    CHECK(nbt::pathIsPackaged(PKG_APP "/nbt-aad"));
    CHECK(nbt::pathIsPackaged(PKG_APP "/bin/nbt-aad"));
    CHECK(!nbt::pathIsPackaged("/opt/aa/nbt-aad"));                // hand install
    CHECK(!nbt::pathIsPackaged("/fs/sda0/opt/aa/nbt-aad"));
    CHECK(!nbt::pathIsPackaged("nbt-aad"));                        // started by hand from a shell
    CHECK(!nbt::pathIsPackaged("./nbt-aad"));
    CHECK(!nbt::pathIsPackaged(""));
    CHECK(!nbt::pathIsPackaged(NULL));
    CHECK(!nbt::pathIsPackaged(PKG_APP));                          // the dir itself, not a program in it
    CHECK(!nbt::pathIsPackaged(PKG_APP "/"));
    CHECK(!nbt::pathIsPackaged(PKG_APP "2/nbt-aad"));              // a sibling package that starts the same way
    CHECK(!nbt::pathIsPackaged("/var/opt/nbtapp/apps/com.github.sq00ky/nbt-aad"));
    CHECK(!nbt::pathIsPackaged(PKG_DATA "/nbt-aad"));              // the data dir is not the app dir
    CHECK(!nbt::pathIsPackaged("/tmp" PKG_APP "/nbt-aad"));        // only at the start

    // ---- before any decision: the hand install ----------------------------------------------------------------
    CHECK(!nbt::packaged());
    SAME(nbt::appDir(), "/opt/aa");

    // ---- hand install: every path exactly as before nbt_paths.hpp ------------------------------------------------
    nbt::initPaths("/opt/aa/nbt-aad");
    CHECK(!nbt::packaged());
    SAME(nbt::appDir(), "/opt/aa");
    SAME(nbt::dataDir(), "/var/opt/aa");
    SAME(nbt::confDir(), "/opt/aa");
    SAME(nbt::daemonPath(), "/opt/aa/nbt-aad");
    SAME(nbt::installName(), "hand install");
    SAME(nbt::confFile(nbt::kDaemonEnvName).c_str(), "/opt/aa/nbt-aad.env");
    SAME(nbt::confFile(nbt::kPfBackupName).c_str(), "/opt/aa/pf.bak");
    SAME(nbt::dataFile("usb-aa").c_str(), "/var/opt/aa/usb-aa");
    SAME(nbt::appFile("usb-aa").c_str(), "/opt/aa/usb-aa");
    SAME(nbt::dataFile("bootguard").c_str(), "/var/opt/aa/bootguard");
    SAME(nbt::appFile("mic.disable").c_str(), "/opt/aa/mic.disable");

    // ---- package install ----------------------------------------------------------------------------------------
    nbt::initPaths(PKG_APP "/nbt-aad");
    CHECK(nbt::packaged());
    SAME(nbt::appDir(), PKG_APP);
    SAME(nbt::dataDir(), PKG_DATA);
    SAME(nbt::confDir(), PKG_DATA);                                // nbt-aad.env and pf.bak go with the settings
    SAME(nbt::daemonPath(), PKG_APP "/nbt-aad");
    SAME(nbt::installName(), "package install");
    SAME(nbt::confFile(nbt::kDaemonEnvName).c_str(), PKG_DATA "/nbt-aad.env");
    SAME(nbt::confFile(nbt::kPfBackupName).c_str(), PKG_DATA "/pf.bak");
    SAME(nbt::dataFile("usb-aa").c_str(), PKG_DATA "/usb-aa");
    SAME(nbt::appFile("usb-aa").c_str(), PKG_APP "/usb-aa");
    SAME(nbt::appFile("app.disable").c_str(), PKG_APP "/app.disable");
    // The daemon's own path decides it again: a package daemon's argv[0] is a package path, so it agrees.
    CHECK(nbt::pathIsPackaged(nbt::daemonPath()));

    // A relative start goes back to the hand install.
    nbt::initPaths("./nbt-aad");
    CHECK(!nbt::packaged());
    SAME(nbt::dataDir(), "/var/opt/aa");
    nbt::setPackaged(true);
    CHECK(nbt::packaged());
    nbt::setPackaged(false);
    CHECK(!nbt::packaged());

    // ---- iDrive app: an executable nbt-aad in the dir -------------------------------------------------------------
    char dir[] = "/tmp/nbt_paths_testXXXXXX";
    CHECK(mkdtemp(dir) != NULL);
    char daemon[96];
    snprintf(daemon, sizeof(daemon), "%s/nbt-aad", dir);
    CHECK(!nbt::daemonExecutableIn(dir));                          // nothing there
    CHECK(mkdir(daemon, 0755) == 0);
    CHECK(!nbt::daemonExecutableIn(dir));                          // a directory of that name
    CHECK(rmdir(daemon) == 0);
    FILE* f = fopen(daemon, "w");
    CHECK(f != NULL);
    if (f) fclose(f);
    CHECK(chmod(daemon, 0644) == 0);
    CHECK(!nbt::daemonExecutableIn(dir));                          // not executable
    CHECK(chmod(daemon, 0755) == 0);
    CHECK(nbt::daemonExecutableIn(dir));
    unlink(daemon);
    rmdir(dir);
    CHECK(!nbt::daemonExecutableIn("/tmp/nbt_paths_test_missing"));

    // flagSet: neither file there (the dirs are the real ones, so only a name nobody makes).
    CHECK(!nbt::flagSet("nbt-paths-test-no-such-flag"));

    printf(fails ? "test_nbt_paths: %d FAILED\n" : "test_nbt_paths: all passed\n", fails);
    return fails ? 1 : 0;
}
