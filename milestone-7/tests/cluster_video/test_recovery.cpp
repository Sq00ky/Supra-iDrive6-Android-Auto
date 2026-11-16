#include "cluster/video/recovery.hpp"
#include <assert.h>
#include <stdio.h>
using namespace aa_cluster;
int main() {
    Recovery recovery;
    uint32_t now = 0xfffff000u;
    const unsigned delays[] = {1000, 2000, 4000, 8000, 16000, 30000, 30000};
    for (unsigned i = 0; i < 7; ++i) {
        assert(recovery.ready(now));
        recovery.started(now);
        assert(recovery.failed(now, true) == delays[i]);
        assert(!recovery.ready(now + delays[i] - 1));
        now += delays[i];
    }
    recovery.started(now);
    recovery.healthy(now + 30000);
    assert(recovery.failed(now + 30000, true) == 1000);
    recovery.failed(now, false);
    assert(!recovery.ready(now + 1000000));
    recovery = Recovery();
    assert(recovery.ready(now));

    IoDeadline write(0xfffff000u, 3000);
    now = 0xfffff000u;
    for (unsigned i = 0; i < 5; ++i) {
        now += 2000;
        assert(!write.expired(now));
        write.progress(now);
    }
    assert(!write.expired(now + 2999));
    assert(write.expired(now + 3000));
    write.progress(now + 4000);
    assert(write.expired(now + 5000)); // Total 15-second bound, despite progress.
    puts("AA_RECOVERY_OK backoff, stable reset, permanent failure, progress and wraparound");
}
