// Native owner of the player's process, temporary producer address and lease.
// Commands use argv directly; no shell or installed supervisor script is used.
#include "supervisor.hpp"
#include "process.hpp"
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/utsname.h>
#include <fcntl.h>
#include <poll.h>
#include <glob.h>
#include <time.h>
#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

namespace aa_cluster {
typedef nbt::cluster::VideoProfile Profile;
namespace {
// Stable exit statuses used by the daemon and deployment diagnostics.
enum SupervisorExit {
    PreflightFailed = 90,
    LeaseAcquisitionFailed = 91,
    ProducerAddressUnavailable = 92,
    ProducerNeighborPresent = 93,
    ScreenInspectionFailed = 94,
    PlayerStartupFailed = 95,
    CleanupFailed = 96,
    UnknownSecondaryWindow = 97,
    MissingDisplayManager = 98,
    Cancelled = 130
};
enum { MaxDiagnosticBytes = 65536, CommandPollMs = 20, ChildPollUs = 20000 };

volatile sig_atomic_t stop_requested = 0;
void stop_signal(int) {
    stop_requested = 1;
}
uint32_t monotonic_ms() {
    timespec timestamp = {};
    clock_gettime(CLOCK_MONOTONIC, &timestamp);
    return uint32_t(timestamp.tv_sec) * 1000 + timestamp.tv_nsec / 1000000;
}
bool process_alive(pid_t pid) {
    return pid > 1 && (kill(pid, 0) == 0 || errno == EPERM);
}
int exit_code(int status) {
    return WIFEXITED(status) ? WEXITSTATUS(status)
                             : 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
}
bool path_exists(const std::string& path) {
    struct stat metadata;
    return lstat(path.c_str(), &metadata) == 0;
}
bool read_file(const std::string& path, std::string& contents) {
    contents.clear();
    FILE* file = fopen(path.c_str(), "r");
    if (!file) {
        return false;
    }
    char buffer[1024];
    size_t bytes_read;
    bool ok = true;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file))) {
        contents.append(buffer, bytes_read);
        if (contents.size() > MaxDiagnosticBytes) {
            ok = false;
            break;
        }
    }
    ok = ok && !ferror(file);
    fclose(file);
    return ok;
}
bool has_line(const std::string& text, const std::string& wanted) {
    return ("\n" + text + "\n").find("\n" + wanted + "\n") != std::string::npos;
}
std::string line_value(const std::string& text, const char* key) {
    std::string all = "\n" + text + "\n", prefix = "\n" + std::string(key);
    size_t start = all.find(prefix);
    if (start == std::string::npos) {
        return "";
    }
    start += prefix.size();
    return all.substr(start, all.find('\n', start) - start);
}
bool is_decimal(const std::string& s) {
    return !s.empty() && s.find_first_not_of("0123456789") == std::string::npos;
}
bool read_unique_file(const std::string& pattern, std::string& content) {
    glob_t matches = {};
    int rc = glob(pattern.c_str(), GLOB_NOSORT, 0, &matches);
    bool ok = !rc && matches.gl_pathc == 1 && read_file(matches.gl_pathv[0], content);
    globfree(&matches);
    return ok;
}
bool contains_inet_address(const std::string& text, const char* address) {
    std::string token = "inet " + std::string(address);
    for (size_t p = text.find(token); p != std::string::npos; p = text.find(token, p + 1)) {
        size_t end = p + token.size();
        if ((p == 0 || text[p - 1] == ' ' || text[p - 1] == '\t' || text[p - 1] == '\n') &&
            (end == text.size() || text[end] == ' ' || text[end] == '\t' || text[end] == '\n')) {
            return true;
        }
    }
    return false;
}
bool contains_mac_address(const std::string& text) {
    for (size_t p = 0; p < text.size(); ++p) {
        unsigned a, b, c, d, e, f;
        if (sscanf(text.c_str() + p, "%2x:%2x:%2x:%2x:%2x:%2x", &a, &b, &c, &d, &e, &f) == 6) {
            return true;
        }
    }
    return false;
}
// Bound command output and runtime. Always reap the spawned process, including
// timeout/error paths, before closing the final read descriptor.
int capture_command(const char* path, const char* const args[], std::string& output,
                    unsigned timeout_ms) {
    output.clear();
    int output_pipe[2];
    if (pipe(output_pipe)) {
        return -1;
    }
    fcntl(output_pipe[0], F_SETFD, FD_CLOEXEC);
    fcntl(output_pipe[1], F_SETFD, FD_CLOEXEC);
    fcntl(output_pipe[0], F_SETFL, O_NONBLOCK);
    int input = open("/dev/null", O_RDONLY);
    if (input < 0) {
        close(output_pipe[0]);
        close(output_pipe[1]);
        return -1;
    }
    pid_t pid =
        spawn_mapped(path, const_cast<char* const*>(args), input, output_pipe[1], output_pipe[1]);
    close(input);
    close(output_pipe[1]);
    if (pid < 0) {
        close(output_pipe[0]);
        return -1;
    }
    uint32_t started_ms = monotonic_ms();
    int status = 0;
    bool done = false, ok = true;
    while (!done && uint32_t(monotonic_ms() - started_ms) < timeout_ms) {
        char buffer[2048];
        ssize_t bytes_read;
        while ((bytes_read = read(output_pipe[0], buffer, sizeof(buffer))) > 0) {
            output.append(buffer, bytes_read);
            if (output.size() > MaxDiagnosticBytes) {
                ok = false;
                break;
            }
        }
        if (!ok) {
            break;
        }
        pid_t result = waitpid(pid, &status, WNOHANG);
        if (result == pid) {
            done = true;
        } else if (result < 0 && errno != EINTR) {
            ok = false;
            break;
        } else {
            pollfd readable = {output_pipe[0], POLLIN, 0};
            poll(&readable, 1, CommandPollMs);
        }
    }
    if (!done) {
        kill(pid, SIGKILL);
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
        }
        ok = false;
    }
    if (done) {
        char buffer[2048];
        ssize_t bytes_read;
        while ((bytes_read = read(output_pipe[0], buffer, sizeof(buffer))) > 0) {
            output.append(buffer, bytes_read);
            if (output.size() > MaxDiagnosticBytes) {
                ok = false;
                break;
            }
        }
    }
    close(output_pipe[0]);
    return ok ? exit_code(status) : -1;
}
class Supervisor {
    const SupervisorPaths& paths_;
    std::string executable_;
    std::string working_directory_;
    std::string lease_contents_;
    pid_t owner_pid_;
    pid_t child_pid_;
    bool lease_acquired_;
    bool address_acquired_;
    bool renderer_cleaned_;

    int command(const std::string& executable, const char* argument1, const char* argument2,
                std::string& text) {
        const char* args[] = {executable.c_str(), argument1, argument2, 0};
        return capture_command(executable.c_str(), args, text, paths_.command_ms);
    }
    int configure_producer_address(bool add) {
        const char* create[] = {paths_.ifconfig.c_str(),
                                Profile::networkInterface(),
                                "inet",
                                Profile::producerAddress(),
                                "netmask",
                                "255.255.255.255",
                                "alias",
                                0};
        const char* remove[] = {paths_.ifconfig.c_str(),
                                Profile::networkInterface(),
                                "inet",
                                Profile::producerAddress(),
                                "-alias",
                                0};
        std::string text;
        int rc = capture_command(paths_.ifconfig.c_str(), add ? create : remove, text,
                                 paths_.command_ms);
        if (rc) {
            printf("AA_CLUSTER_ALIAS_FAILED add=%u status=%d %s\n", unsigned(add), rc,
                   text.c_str());
        }
        return rc;
    }
    bool has_display_managers() {
        for (unsigned i = 1; i <= Profile::DisplayCount; ++i) {
            char name[32];
            snprintf(name, sizeof(name), "/*/dpy-%u", i);
            std::string text;
            if (!read_unique_file(paths_.screen + name, text)) {
                return false;
            }
            std::string manager = line_value(text, "window manager = ");
            if (!is_decimal(manager) || strtoul(manager.c_str(), 0, 10) == 0) {
                return false;
            }
        }
        return true;
    }
    // An empty output or the known factory navigation window is acceptable.
    // We never remove or take ownership of the factory window.
    bool secondary_output_available() {
        std::string text;
        char display_path[32];
        snprintf(display_path, sizeof(display_path), "/*/dpy-%u",
                 unsigned(Profile::ScreenDisplayId));
        if (!read_unique_file(paths_.screen + display_path, text)) {
            return false;
        }
        if (has_line(text, "scene = (empty)") && has_line(text, "windows = (none)")) {
            return true;
        }
        std::string win = line_value(text, "windows = ");
        if (win.substr(0, 4) != "win-" || !is_decimal(win.substr(4)) ||
            !has_line(text, "scene = " + win)) {
            return false;
        }
        if (!read_unique_file(paths_.screen + "/*/" + win + "/" + win, text)) {
            return false;
        }
        char display_id[48], buffer_size[64], destination[96];
        snprintf(display_id, sizeof(display_id), "display id = %u",
                 unsigned(Profile::ScreenDisplayId));
        snprintf(buffer_size, sizeof(buffer_size), "buffer size = %ux%u",
                 unsigned(Profile::BackingWidth), unsigned(Profile::BackingHeight));
        snprintf(destination, sizeof(destination), "destination rectangle = (0,0 %ux%u)",
                 unsigned(Profile::BackingWidth), unsigned(Profile::BackingHeight));
        if (!has_line(text, std::string("id string = ") + Profile::factoryWindowId()) ||
            !has_line(text, display_id) || !has_line(text, buffer_size) ||
            !has_line(text, "format = SCREEN_FORMAT_RGBA8888") || !has_line(text, destination)) {
            return false;
        }
        printf("FACTORY_WINDOW_RETAINED id=%s\n", Profile::factoryWindowId());
        return true;
    }
    // Claim the shared lock before creating per-process state. Until the owner
    // file is saved, failures remove only the directories just created here.
    bool acquire_lease() {
        if (mkdir(paths_.lock.c_str(), 0700)) {
            return false;
        }
        char suffix[64];
        snprintf(suffix, sizeof(suffix), "%ld-%ld", long(owner_pid_), long(getpid()));
        working_directory_ = paths_.temporary + suffix;
        lease_contents_ = working_directory_ + "\n";
        if (mkdir(working_directory_.c_str(), 0700)) {
            rmdir(paths_.lock.c_str());
            return false;
        }
        int fd = open((paths_.lock + "/owner").c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
        bool saved = fd >= 0 && write(fd, lease_contents_.data(), lease_contents_.size()) ==
                                    ssize_t(lease_contents_.size());
        if (fd >= 0) {
            close(fd);
        }
        if (!saved) {
            unlink((paths_.lock + "/owner").c_str());
            rmdir(working_directory_.c_str());
            rmdir(paths_.lock.c_str());
            return false;
        }
        lease_acquired_ = true;
        return true;
    }

    bool owns_lease() {
        std::string current;
        return lease_acquired_ && read_file(paths_.lock + "/owner", current) &&
               current == lease_contents_;
    }
    bool cleanup() {
        // The child has exited/reaped before its producer address is released.
        if (!renderer_cleaned_) {
            puts("AA_CLUSTER_SERVICE_CLEANUP renderer_pending=1 lease_retained=1");
            return false;
        }
        bool ok = true;
        if (address_acquired_) {
            if (configure_producer_address(false)) {
                ok = false;
            }
            std::string text;
            if (command(paths_.ifconfig, Profile::networkInterface(), 0, text) ||
                contains_inet_address(text, Profile::producerAddress())) {
                ok = false;
            }
            if (ok) {
                address_acquired_ = false;
            }
        }
        // Keep the lease if address removal or ownership verification failed.
        // That prevents another producer from starting after incomplete cleanup.
        if (lease_acquired_) {
            if (!owns_lease()) {
                ok = false;
            }
            if (ok) {
                if (rmdir(working_directory_.c_str()) || unlink((paths_.lock + "/owner").c_str()) ||
                    rmdir(paths_.lock.c_str())) {
                    ok = false;
                }
            }
        }
        printf("AA_CLUSTER_SERVICE_CLEANUP alias_removed=%u\n", unsigned(ok));
        fflush(0);
        return ok;
    }
    int finish(int rc) {
        return cleanup() ? rc : CleanupFailed;
    }

  public:
    Supervisor(const SupervisorPaths& paths, const char* executable, pid_t parent)
        : paths_(paths), executable_(executable), owner_pid_(parent), child_pid_(-1),
          lease_acquired_(false), address_acquired_(false), renderer_cleaned_(true) {}
    int run() {
        struct utsname u;
        std::string text;
        if (uname(&u) || paths_.node != u.nodename || !process_alive(owner_pid_) ||
            access(executable_.c_str(), X_OK)) {
            return PreflightFailed;
        }
        // Version is a diagnostic, not an exact-firmware allowlist.
        if (read_file(paths_.version, text)) {
            printf("AA_CLUSTER_FIRMWARE %s\n", text.c_str());
        } else {
            puts("AA_CLUSTER_FIRMWARE unavailable");
        }
        struct stat flags;
        if (stat(paths_.flags.c_str(), &flags) || !S_ISDIR(flags.st_mode) ||
            access(paths_.flags.c_str(), R_OK | X_OK) ||
            path_exists(paths_.flags + "/APIX_DRIVER_OFF") ||
            path_exists(paths_.flags + "/APIX_DFE_1920")) {
            return PreflightFailed;
        }
        if (command(paths_.ifconfig, Profile::networkInterface(), 0, text) ||
            !contains_inet_address(text, Profile::headUnitAddress())) {
            return PreflightFailed;
        }
        if (!acquire_lease()) {
            return LeaseAcquisitionFailed;
        }
        if (command(paths_.ifconfig, "-a", 0, text) ||
            contains_inet_address(text, Profile::producerAddress())) {
            puts("REFUSE_EXISTING_ALIAS_OR_QUERY_FAILED");
            return finish(ProducerAddressUnavailable);
        }
        // A missing ARP entry may return nonzero. Tool failure still fails closed.
        int arp = command(paths_.arp, "-n", Profile::producerAddress(), text);
        if (arp < 0 || contains_mac_address(text)) {
            puts("REFUSE_ALIAS_NEIGHBOR");
            return finish(ProducerNeighborPresent);
        }
        if (!has_display_managers()) {
            puts("REFUSE_MISSING_FACTORY_DISPLAY_MANAGER");
            return finish(MissingDisplayManager);
        }
        int inspected = command(executable_, "--inspect", 0, text);
        printf("%s", text.c_str());
        if (inspected) {
            return finish(ScreenInspectionFailed);
        }
        if (!has_display_managers() || !secondary_output_available()) {
            puts("REFUSE_UNKNOWN_SECONDARY_WINDOW");
            return finish(UnknownSecondaryWindow);
        }
        if (stop_requested || !process_alive(owner_pid_)) {
            return finish(Cancelled);
        }
        // Recheck immediately before acquiring the address after slow inspection.
        if (command(paths_.ifconfig, "-a", 0, text) ||
            contains_inet_address(text, Profile::producerAddress())) {
            return finish(ProducerAddressUnavailable);
        }
        if (configure_producer_address(true)) {
            return finish(PlayerStartupFailed);
        }
        address_acquired_ = true;
        int input = open("/dev/null", O_RDONLY);
        if (input < 0) {
            return finish(PlayerStartupFailed);
        }
        char* args[] = {const_cast<char*>(executable_.c_str()), const_cast<char*>("--service"), 0};
        // Both streams flow through the daemon's prefixed log relay. No second
        // file is created, and successful sessions are logged live as well.
        child_pid_ = spawn_mapped(executable_.c_str(), args, input, STDOUT_FILENO, STDERR_FILENO);
        close(input);
        if (child_pid_ < 0) {
            return finish(PlayerStartupFailed);
        }
        printf("AA_CLUSTER_SUPERVISOR_READY owner=%ld player=%ld\n", long(owner_pid_),
               long(child_pid_));
        fflush(0);
        // TERM lets the player release native PiP resources. Escalation targets
        // only our child; the producer address remains held until it is reaped.
        bool terminating = false, forced = false;
        uint32_t since = 0;
        int status = 0;
        for (;;) {
            pid_t result = waitpid(child_pid_, &status, WNOHANG);
            if (result == child_pid_) {
                break;
            }
            if (result < 0 && errno != EINTR) {
                return finish(CleanupFailed);
            }
            if (!terminating && (stop_requested || !process_alive(owner_pid_))) {
                kill(child_pid_, SIGTERM);
                terminating = true;
                since = monotonic_ms();
            }
            if (terminating && !forced && uint32_t(monotonic_ms() - since) >= paths_.stop_ms) {
                kill(child_pid_, SIGKILL);
                forced = true;
            }
            usleep(ChildPollUs);
        }
        // Bound remote renderer cleanup in a separate helper. Never restart a
        // decoder while its previous context may still own hardware buffers.
        char child_text[32]; snprintf(child_text, sizeof(child_text), "%ld", long(child_pid_));
        renderer_cleaned_ = command(executable_, "--cleanup-renderer", child_text, text) == 0;
        printf("%s", text.c_str());
        child_pid_ = -1;
        int rc = exit_code(status);
        if (rc) {
            printf("AA_CLUSTER_PLAYER_FAILED status=%d\n", rc);
        }
        return finish(rc);
    }
};
} // namespace
int supervise_player(const char* executable, const char* parent, const SupervisorPaths& paths) {
    if (!executable || executable[0] != '/' || !parent || !is_decimal(parent)) {
        return PreflightFailed;
    }
    setvbuf(stdout, 0, _IOLBF, 0);
    errno = 0;
    long owner = strtol(parent, 0, 10);
    if (errno || owner <= 1 || owner > INT_MAX) {
        return PreflightFailed;
    }
    stop_requested = 0;
    signal(SIGHUP, stop_signal);
    signal(SIGINT, stop_signal);
    signal(SIGTERM, stop_signal);
    signal(SIGPIPE, SIG_IGN);
    umask(077);
    // Same loader paths as the previous installed helper, inherited by children.
    setenv("LD_LIBRARY_PATH", "/opt/mm/lib:/opt/conn/lib/carplay:/opt/conn/lib/carplay/mmedia:/boot/lib:/lib:/usr/lib:/opt/os/lib:/opt/sys/lib:/opt/core/lib:/opt/car/lib:/lib/graphics/omap5430", 1);
    return Supervisor(paths, executable, pid_t(owner)).run();
}
} // namespace aa_cluster
