#pragma once
#include "process.hpp"
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

namespace aa_cluster {
inline pid_t spawn_service(const char* path,bool legacy_script=false,int* output=0) {
    if(output)*output=-1;
    int input=open("/dev/null",O_RDONLY);
    if(input<0)return -1;
    int logs[2]={-1,-1};
    if(output) {
        if(pipe(logs)) {int error=errno;close(input);errno=error;return -1;}
        fcntl(logs[0],F_SETFD,FD_CLOEXEC);fcntl(logs[1],F_SETFD,FD_CLOEXEC);
    }
    char owner[32];snprintf(owner,sizeof(owner),"%ld",(long)getpid());
    char* native[]={const_cast<char*>(path),const_cast<char*>("--supervise"),owner,0};
    char* legacy[]={const_cast<char*>("sh"),const_cast<char*>(path),owner,0};
    pid_t pid=spawn_mapped(legacy_script?"/bin/sh":path,legacy_script?legacy:native,
                           input,output?logs[1]:STDOUT_FILENO,output?logs[1]:STDERR_FILENO);
    int error=errno;
    close(input);
    if(output) {close(logs[1]);if(pid<0)close(logs[0]);else *output=logs[0];}
    errno=error;
    return pid;
}
}
