// Native QNX regression: spawn the player entry mode from a daemon worker.
#include "cluster/g30/service_spawn.hpp"
// The offline SDK host is x86/GCC 4.4. Production ARM uses the real ARMv7
// barrier implementation; this process/logging probe uses x86's older builtins.
#if defined(__QNXNTO__) && defined(__i386__) && __GNUC__ == 4 && __GNUC_MINOR__ < 7
static unsigned probe_load(const unsigned* p) {return __sync_fetch_and_add(const_cast<unsigned*>(p),0u);}
static unsigned probe_exchange(unsigned* p,unsigned v) {
    __sync_synchronize();unsigned old=__sync_lock_test_and_set(p,v);__sync_synchronize();return old;
}
#define __atomic_load_n(p,order) probe_load(p)
#define __atomic_store_n(p,v,order) ((void)probe_exchange(p,v))
#define __atomic_exchange_n(p,v,order) probe_exchange(p,v)
#endif
#include "cluster/g30/service_log.hpp"
#include <pthread.h>
#include <stdlib.h>
#include <sys/wait.h>
static const char* executable;
static int result=1;
static void* worker(void*) {
    int output=-1;aa_cluster::ServiceLog logs;
    pid_t child=aa_cluster::spawn_service(executable,false,&output);
    if(child<0) {printf("SPAWN_FAILED errno=%d\n",errno);return 0;}
    if(!logs.start(output)) {kill(child,SIGTERM);return 0;}
    int status=0;pid_t waited;
    do {waited=waitpid(child,&status,0);} while(waited<0&&errno==EINTR);
    logs.finish();
    if(waited==child&&WIFEXITED(status)&&WEXITSTATUS(status)==0)result=0;
    printf("PLAYER_NATIVE_SPAWN result=%d child_status=%d\n",result,status);
    return 0;
}
int main(int argc,char** argv) {
    if(argc==3&&!strcmp(argv[1],"--supervise")) {
        sigset_t mask;struct sigaction action;
        if(fcntl(50,F_GETFD)!=-1||errno!=EBADF)return 10;
        if(sigprocmask(SIG_SETMASK,0,&mask)||sigismember(&mask,SIGTERM))return 11;
        if(sigaction(SIGTERM,0,&action)||action.sa_handler!=SIG_DFL)return 12;
        char byte;if(read(0,&byte,1)!=0)return 13;
        if(atoi(argv[2])!=getppid()||getppid()<=1)return 14;
        puts("PLAYER_NATIVE_CHILD_OK no_extra_fds=1 signals_reset=1 stdin_null=1 owner_correct=1");
        fprintf(stderr,"PLAYER_NATIVE_STDERR_OK\n");
        return 0;
    }
    if(argc!=1||argv[0][0]!='/')return 2;
    alarm(15);executable=argv[0];
    int base=open("/dev/null",O_RDONLY),extra=fcntl(base,F_DUPFD,50);
    if(base<0||extra!=50)return 3;
    signal(SIGTERM,SIG_IGN);sigset_t mask;sigemptyset(&mask);sigaddset(&mask,SIGTERM);
    pthread_sigmask(SIG_BLOCK,&mask,0);
    pthread_t thread;if(pthread_create(&thread,0,worker,0))return 5;
    pthread_join(thread,0);close(extra);close(base);return result;
}
