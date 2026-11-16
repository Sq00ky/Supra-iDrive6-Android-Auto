// Real process spawning/reaping and log relay with a tiny isolated helper.
#include "cluster/video/player_service.hpp"
#include <assert.h>
#include <stdlib.h>
#include <sys/stat.h>
using namespace aa_cluster;
static void script(const char* path, const char* body) {
    FILE* file = fopen(path, "w");
    assert(file);
    fprintf(file, "#!/bin/sh\n%s\n", body);
    fclose(file);
    assert(!chmod(path, 0700));
}
static void wait_for_exit(PlayerService& service, uint32_t now) {
    for (unsigned i = 0; i < 100; ++i) {
        service.tick(true, true, now);
        if (!service.running()) return;
        usleep(10000);
    }
    assert(false && "helper not reaped");
}
int main() {
    char folder[] = "/tmp/cluster-recovery-XXXXXX";
    assert(mkdtemp(folder));
    char path[256];
    snprintf(path, sizeof(path), "%s/kombi-player", folder);
    script(path, "exit 1");
    PlayerService service(path);
    assert(service.tick(true, true, 0));
    wait_for_exit(service, 10);
    assert(!service.tick(true, true, 1009));
    assert(service.tick(true, true, 1010));
    wait_for_exit(service, 1020);
    assert(!service.tick(true, true, 3019));
    // User disables the preference while waiting: no delayed restart.
    assert(!service.tick(true, false, 10000));
    script(path, "exit 96");
    assert(service.tick(true, true, 10001));
    wait_for_exit(service, 10002);
    assert(!service.tick(true, true, 999999)); // Cleanup failure stays latched.
    assert(!service.tick(true, false, 1000000));
    script(path, "trap 'exit 0' TERM\nwhile :; do sleep 0.1; done");
    assert(service.tick(true, true, 1000001));
    usleep(100000);
    service.disconnected();
    wait_for_exit(service, 1000002);
    assert(!service.tick(true, true, 1001001));
    assert(service.tick(true, true, 1001002));
    assert(!service.tick(false, true, 1001003));
    unlink(path);
    rmdir(folder);
    puts("AA_PLAYER_SERVICE_OK runtime recovery, cleanup latch, off, session end");
}
