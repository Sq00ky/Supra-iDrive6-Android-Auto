// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
// Host test: AudioRoute build retries (and builds without them) and the "granted" callback (drives NBT_BT_PLAY).
// A fake audioconn (shell script) refuses the first FAILS calls like the car's "AMCommand error", then succeeds.
#include "audio/audio_route.hpp"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

static volatile int g_granted = 0;
static void onGranted(void* user) { g_granted = *(int*)user; }

// removeFocus() completion: the tag, negated when audioconn did not run.
static volatile int g_removed = 0;
static void onRemoved(void* user, bool ran) { g_removed = ran ? *(int*)user : -*(int*)user; }

// sleep_s: the fake holds the one audioconn slot this long after counting the call.
static void writeFake(const char* path, const char* counter, int fails, int sleep_s = 0) {
    FILE* f = fopen(path, "w");
    assert(f);
    fprintf(f, "#!/bin/sh\nn=$(cat %s 2>/dev/null || echo 0)\nn=$((n+1))\necho $n > %s\nsleep %d\n"
               "echo \"requestFocus($2,$3)\"\nif [ $n -le %d ]; then echo 'AMCommand error in execution!!!'; exit 255; fi\n"
               "echo \"makeAudible($2,$3)\"\necho 'AMCommand successfully executed'\nexit 0\n", counter, counter, sleep_s,
            fails);
    fclose(f);
    chmod(path, 0755);
    unlink(counter);
}

static int calls(const char* counter) {
    FILE* f = fopen(counter, "r");
    int n = 0;
    if (f) { if (fscanf(f, "%d", &n) != 1) n = 0; fclose(f); }
    return n;
}

static bool waitFor(volatile int* v, int want, int seconds) {
    for (int i = 0; i < seconds * 10; ++i) { if (*v == want) return true; usleep(100000); }
    return *v == want;
}

int main() {
    const char* fake = "/tmp/nbt_fake_audioconn.sh";
    const char* counter = "/tmp/nbt_fake_audioconn.n";
    setenv("NBT_AUDIOCONN", fake, 1);

    // 1) refused once, granted on attempt 2: callback runs once with our user pointer.
    writeFake(fake, counter, 1);
    nbt::AudioRoute r;
    assert(r.configure());
    int tag = 7;
    g_granted = 0;
    assert(r.buildFocus(0x3a, 0x1, &onGranted, &tag));
    assert(waitFor(&g_granted, 7, 8));
    assert(calls(counter) == 2);

    // 2) always refused, NBT_AUDIOCONN_RETRIES=1: no callback.
    writeFake(fake, counter, 99);
    setenv("NBT_AUDIOCONN_RETRIES", "1", 1);
    g_granted = 0;
    tag = 9;
    assert(r.buildFocus(0x3a, 0x1, &onGranted, &tag));
    sleep(7);   // the one retry runs after NBT_AUDIOCONN_RETRY_S (5) s: waiting exactly 5 s raced it
    assert(g_granted == 0 && calls(counter) == 2);

    // 3) ConnectionMaster form `-b <connId>` granted at once; no callback = no crash.
    writeFake(fake, counter, 0);
    tag = 3;
    g_granted = 0;
    assert(r.build(0xc7, &onGranted, &tag));
    assert(waitFor(&g_granted, 3, 4));
    assert(r.build(0xc7));
    sleep(1);
    assert(calls(counter) == 2);

    // 4) remove() completion callback: runs once the remove is over, also when audioconn refuses it.
    writeFake(fake, counter, 0);
    tag = 5;
    g_granted = 0;
    assert(r.remove(0xb8, &onGranted, &tag));
    assert(waitFor(&g_granted, 5, 4));
    writeFake(fake, counter, 99);
    tag = 6;
    g_granted = 0;
    assert(r.remove(0xb8, &onGranted, &tag));
    assert(waitFor(&g_granted, 6, 4));
    assert(calls(counter) == 1);   // removes are not retried
    assert(r.remove(0xb8));        // no callback = no crash
    sleep(1);

    // 5) removeFocus() completion: ran = audioconn ran (any exit status), not when cancelled before it could run.
    writeFake(fake, counter, 0);
    tag = 11;
    g_removed = 0;
    assert(r.removeFocus(0x1a, 0x1, &onRemoved, &tag));
    assert(waitFor(&g_removed, 11, 4));
    writeFake(fake, counter, 99);
    tag = 12;
    g_removed = 0;
    assert(r.removeFocus(0x1a, 0x1, &onRemoved, &tag));
    assert(waitFor(&g_removed, 12, 4));   // refused, but it ran
    assert(calls(counter) == 1);
    writeFake(fake, counter, 0, 2);
    assert(r.removeFocus(0x1a, 0x1));     // no callback = no crash; holds the slot for 2 s
    for (int i = 0; i < 40 && calls(counter) < 1; ++i) usleep(100000);
    assert(calls(counter) == 1);
    tag = 13;
    g_removed = 0;
    assert(r.removeFocus(0x1a, 0x1, &onRemoved, &tag));   // waits for the slot
    r.cancelPending();
    assert(waitFor(&g_removed, -13, 4));
    sleep(2);   // the running one finishes; the cancelled one never ran
    assert(calls(counter) == 1);

    // 6) buildFocus(retry = false): one attempt only, and the build slot is free again afterwards.
    setenv("NBT_AUDIOCONN_RETRY_S", "1", 1);   // a retrying build would try again after 1 s
    writeFake(fake, counter, 1);
    tag = 14;
    g_granted = 0;
    assert(r.buildFocus(0x1a, 0x1, &onGranted, &tag, 0, false));
    sleep(3);
    assert(g_granted == 0 && calls(counter) == 1);
    writeFake(fake, counter, 0);
    tag = 15;
    assert(r.buildFocus(0x1a, 0x1, &onGranted, &tag, 0, false));
    assert(waitFor(&g_granted, 15, 4));
    assert(calls(counter) == 1);

    unlink(fake);
    unlink(counter);
    printf("test_audio_route OK\n");
    return 0;
}
