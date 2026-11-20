#include "tile_process.hpp"
#include <assert.h>
#include <string.h>
#include <stdlib.h>

int main(int argc, char** argv) {
    // The test executable doubles as a real spawned helper, checking the production arguments.
    if (argc > 1 && !strcmp(argv[1], "follow")) {
        assert(argc == 4 && !strcmp(argv[2], "0"));
        assert(atoi(argv[3]) == (int)getppid());
        for (;;) pause();
    }
    const char* log = "/tmp/nbt-tile-process-test.log";
    {
        nbt::TileProcess child;
        child.service(false, argv[0], log);
        assert(!child.attempted() && child.pid() < 0);
        child.service(true, argv[0], log);
        pid_t pid = child.pid(); assert(pid > 0);
        usleep(100000);
        child.service(true, argv[0], log); assert(child.pid() == pid);
        child.service(false, argv[0], log); assert(child.pid() == pid); // helper follows state across disconnects
        assert(kill(pid, 0) == 0);
        child.stop(); assert(child.pid() < 0);
        assert(waitpid(pid, NULL, WNOHANG) < 0 && errno == ECHILD);
    }
    {
        nbt::TileProcess child;
        child.service(true, "/does/not/exist", log); assert(child.attempted() && child.pid() < 0);
        child.service(true, argv[0], log); assert(child.pid() < 0); // no retry loop
    }
    {
        nbt::TileProcess child;
        child.service(true, argv[0], log); pid_t pid = child.pid(); assert(pid > 0);
        kill(pid, SIGKILL); usleep(100000);
        child.service(true, argv[0], log); assert(child.pid() < 0); // crashed child reaped, never respawned
        child.stop();
    }
    {
        nbt::TileProcess child;
        child.service(true,argv[0],log,1);pid_t first=child.pid();assert(first>0);
        kill(first,SIGKILL);usleep(100000);
        child.service(true,argv[0],log,1);assert(child.pid()<0);
        child.service(true,argv[0],log,1);assert(child.pid()<0); // no loop on same contact
        child.service(true,argv[0],log,2);pid_t next=child.pid();assert(next>0);
        child.service(true,argv[0],log,2);assert(child.pid()==next);child.stop();
    }
    unlink(log);
    puts("PASS: automatic launch, no timer limit, single child, disconnect lifetime, shutdown, missing helper, crash");
}
