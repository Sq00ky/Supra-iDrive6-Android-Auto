#include "cluster/video/service_log.hpp"
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <string>
static void check(bool ok, const char *log_text) {
    if (!ok) {
        fprintf(stderr, "FAIL %s\n", log_text);
        exit(1);
    }
}
static std::string read_log(FILE *file) {
    fflush(file);
    fseek(file, 0, SEEK_SET);
    char data[8192];
    size_t n = fread(data, 1, sizeof(data), file);
    clearerr(file);
    fseek(file, 0, SEEK_END);
    return std::string(data, n);
}
int main() {
    FILE *first_destination = tmpfile();
    FILE *second_destination = tmpfile();
    check(first_destination && second_destination, "temporary log sinks");
    int saved_stdout = dup(STDOUT_FILENO), log_pipe[2];
    check(saved_stdout >= 0 && !pipe(log_pipe), "log pipe");
    fflush(stdout);
    check(dup2(fileno(first_destination), STDOUT_FILENO) >= 0, "first daemon log destination");
    aa_cluster::ServiceLog logs;
    check(logs.start(log_pipe[0]), "start independent log reader");
    const char first_chunk[] = "ready\nsplit ";
    check(write(log_pipe[1], first_chunk, sizeof(first_chunk) - 1) == sizeof(first_chunk) - 1,
          "first chunk");
    bool received_live_output = false;
    for (unsigned i = 0; i < 100; ++i) {
        struct stat status;
        if (!fstat(fileno(first_destination), &status) && status.st_size) {
            received_live_output = true;
            break;
        }
        usleep(10000);
    }
    check(received_live_output, "logs arrive before process exit");
    check(read_log(first_destination) == "[kombi-player] ready\n", "one prefix per complete line");
    // Match the daemon's RAM/USB/off redirect behavior while the child lives.
    fflush(stdout);
    check(dup2(fileno(second_destination), STDOUT_FILENO) >= 0, "switch daemon log destination");
    const char last_chunk[] = "line\n";
    check(write(log_pipe[1], last_chunk, sizeof(last_chunk) - 1) == sizeof(last_chunk) - 1,
          "fragment continuation");
    char long_line[6001];
    memset(long_line, 'x', 6000);
    long_line[6000] = '\n';
    check(write(log_pipe[1], long_line, sizeof(long_line)) == sizeof(long_line), "long diagnostic");
    check(write(log_pipe[1], "last partial", 12) == 12, "trailing partial line");
    close(log_pipe[1]);
    logs.finish();
    std::string log_text = read_log(second_destination);
    check(log_text.find("[kombi-player] split line\n") == 0,
          "partial line follows current destination");
    check(log_text.find(" [line truncated]\n") != std::string::npos && log_text.size() < 2300,
          "bounded long line");
    check(log_text.find("[kombi-player] last partial\n") != std::string::npos,
          "final diagnostics flushed");
    check(read_log(first_destination) == "[kombi-player] ready\n",
          "no stale writes to old RAM log");
    // A descendant holding the write end cannot prevent bounded shutdown.
    check(!pipe(log_pipe) && logs.start(log_pipe[0]), "restart reader");
    logs.finish();
    close(log_pipe[1]);
    fflush(stdout);
    dup2(saved_stdout, STDOUT_FILENO);
    close(saved_stdout);
    fclose(first_destination);
    fclose(second_destination);
    puts("AA_SERVICE_LOG_OK live prefix, fragments, log switching, bounded lines, EOF and "
         "held-open shutdown");
}
