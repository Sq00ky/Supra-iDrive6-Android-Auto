#pragma once
#include "atomic.hpp"
#include <pthread.h>
#include <poll.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>

namespace aa_cluster {
// Drain independently of video decoding/RPC waits. Emit through the daemon's
// current stdout so RAM limits, USB logging and logging-off apply to both.
class ServiceLog {
    int fd;pthread_t thread;bool running,dropping;unsigned stopping,used;
    char line[2048];
    void emit() {
        if(used||dropping) {
            printf("[kombi-player] %.*s%s\n",int(used),line,dropping?" [line truncated]":"");
            fflush(stdout);
        }
        used=0;dropping=false;
    }
    void consume(const char* data,unsigned n) {
        for(unsigned i=0;i<n;++i) {
            if(data[i]=='\n')emit();
            else if(data[i]!='\r'&&!dropping) {
                if(used<sizeof(line))line[used++]=data[i];else dropping=true;
            }
        }
    }
    void run() {
        unsigned final_bytes=0;
        for(;;) {
            char data[4096];ssize_t n=read(fd,data,sizeof(data));
            if(n>0) {
                consume(data,unsigned(n));
                if(atomic_load(&stopping)) {
                    final_bytes+=unsigned(n);if(final_bytes>=65536)break;
                }
                continue;
            }
            if(!n)break;
            if(errno==EINTR)continue;
            if(errno!=EAGAIN || atomic_load(&stopping))break;
            pollfd p={fd,POLLIN,0};poll(&p,1,100);
        }
        emit();close(fd);
    }
    static void* worker(void* self) {static_cast<ServiceLog*>(self)->run();return 0;}
public:
    ServiceLog():fd(-1),thread(),running(false),dropping(false),stopping(0),used(0) {}
    ~ServiceLog() {finish();}
    bool start(int input) {
        finish();fd=input;used=0;dropping=false;atomic_store(&stopping,0);
        if(fd<0)return false;
        if(fcntl(fd,F_SETFL,O_NONBLOCK)||pthread_create(&thread,0,worker,this)) {
            close(fd);fd=-1;return false;
        }
        running=true;return true;
    }
    void finish() {
        if(running) {atomic_store(&stopping,1);pthread_join(thread,0);running=false;fd=-1;}
    }
private:
    ServiceLog(const ServiceLog&);
    ServiceLog& operator=(const ServiceLog&);
};
}
