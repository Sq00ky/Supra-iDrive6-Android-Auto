#pragma once
// QNX 6.5 rejects fork() once a process has multiple threads. Spawn the shell
// directly and map only standard descriptors; never inherit AA USB/TLS/Screen
// handles into the supervisor or its player.
#include <spawn.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

namespace aa_cluster {
inline pid_t spawn_service(const char* script) {
    int input=open("/dev/null",O_RDONLY);
    if(input<0)return -1;
    char owner[32];snprintf(owner,sizeof(owner),"%ld",(long)getpid());
    char* argv[]={const_cast<char*>("sh"),const_cast<char*>(script),owner,0};
    int descriptors[3]={input,STDOUT_FILENO,STDERR_FILENO};
    struct inheritance inherit;
    memset(&inherit,0,sizeof(inherit));
    inherit.flags=SPAWN_SETSIGDEF|SPAWN_SETSIGMASK;
    sigfillset(&inherit.sigdefault);
    sigemptyset(&inherit.sigmask);
    pid_t pid=spawn("/bin/sh",3,descriptors,&inherit,argv,0);
    int error=errno;
    close(input);
    errno=error;
    return pid;
}
}
