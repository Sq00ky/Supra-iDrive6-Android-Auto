// Native QNX regression: spawn the player entry mode from a daemon worker.
#include "cluster/video/service_spawn.hpp"
// The offline SDK host is x86/GCC 4.4. Production ARM uses the real ARMv7
// barrier implementation; this process/logging probe uses x86's older builtins.
#if defined(__QNXNTO__) && defined(__i386__) && __GNUC__ == 4 && __GNUC_MINOR__ < 7
static unsigned probe_load(const unsigned *p) {
    return __sync_fetch_and_add(const_cast<unsigned *>(p), 0u);
}
static unsigned probe_exchange(unsigned *p, unsigned v) {
    __sync_synchronize();
    unsigned old = __sync_lock_test_and_set(p, v);
    __sync_synchronize();
    return old;
}
#define __atomic_load_n(p, order) probe_load(p)
#define __atomic_store_n(p, v, order) ((void)probe_exchange(p, v))
#define __atomic_exchange_n(p, v, order) probe_exchange(p, v)
#endif
#include "cluster/video/service_log.hpp"
#include <pthread.h>
#include <stdlib.h>
#include <sys/wait.h>
static const char *probe_executable;
static int probe_result = 1;
static void *spawn_from_worker(void *) {
    int child_output = -1;
    aa_cluster::ServiceLog child_logs;
    pid_t child = aa_cluster::spawn_service(probe_executable, false, &child_output);
    if (child < 0) {
        printf("SPAWN_FAILED errno=%d\n", errno);
        return 0;
    }
    if (!child_logs.start(child_output)) {
        kill(child, SIGTERM);
        return 0;
    }
    int child_status = 0;
    pid_t waited_child;
    do {
        waited_child = waitpid(child, &child_status, 0);
    } while (waited_child < 0 && errno == EINTR);
    child_logs.finish();
    if (waited_child == child && WIFEXITED(child_status) && WEXITSTATUS(child_status) == 0) {
        probe_result = 0;
    }
    printf("PLAYER_NATIVE_SPAWN result=%d child_status=%d\n", probe_result, child_status);
    return 0;
}
int main(int argc, char **argv) {
    if (argc == 3 && !strcmp(argv[1], "--supervise")) {
        sigset_t signal_mask;
        struct sigaction termination_action;
        if (fcntl(50, F_GETFD) != -1 || errno != EBADF) {
            return 10;
        }
        if (sigprocmask(SIG_SETMASK, 0, &signal_mask) || sigismember(&signal_mask, SIGTERM)) {
            return 11;
        }
        if (sigaction(SIGTERM, 0, &termination_action) ||
            termination_action.sa_handler != SIG_DFL) {
            return 12;
        }
        char byte;
        if (read(0, &byte, 1) != 0) {
            return 13;
        }
        if (atoi(argv[2]) != getppid() || getppid() <= 1) {
            return 14;
        }
        puts("PLAYER_NATIVE_CHILD_OK no_extra_fds=1 signals_reset=1 stdin_null=1 owner_correct=1");
        fprintf(stderr, "PLAYER_NATIVE_STDERR_OK\n");
        return 0;
    }
    if (argc != 1 || argv[0][0] != '/') {
        return 2;
    }
    alarm(15);
    probe_executable = argv[0];
    int null_input = open("/dev/null", O_RDONLY),
        inherited_descriptor = fcntl(null_input, F_DUPFD, 50);
    if (null_input < 0 || inherited_descriptor != 50) {
        return 3;
    }
    signal(SIGTERM, SIG_IGN);
    sigset_t signal_mask;
    sigemptyset(&signal_mask);
    sigaddset(&signal_mask, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &signal_mask, 0);
    pthread_t worker_thread;
    if (pthread_create(&worker_thread, 0, spawn_from_worker, 0)) {
        return 5;
    }
    pthread_join(worker_thread, 0);
    close(inherited_descriptor);
    close(null_input);
    return probe_result;
}
